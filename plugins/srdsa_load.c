/**
 * @file srds_augeas.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief sysrepo DS plugin for augeas-supported configuration files
 *
 * @copyright
 * Copyright (c) 2021 - 2022 Deutsche Telekom AG.
 * Copyright (c) 2021 - 2022 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#define _GNU_SOURCE

#include "srds_augeas.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <augeas.h>
#include <libyang/libyang.h>
#include <sysrepo.h>
#include <sysrepo/plugins_datastore.h>

/**
 * @brief Get last path segment (node) from a path.
 *
 * @param[in] path Augeas data-path.
 * @param[in] skip_special_chars Whether to skip special characters at the beginning of the node.
 * @return Last node.
 */
static const char *
augds_get_path_node(const char *path, int skip_special_chars)
{
    const char *ptr;

    /* get last path segment */
    ptr = strrchr(path, '/');
    ptr = ptr ? ptr + 1 : path;

    if (skip_special_chars && (!strncmp(ptr, "$$", 2) || !strncmp(ptr, "##", 2))) {
        ptr += 2;
    }

    return ptr;
}

/**
 * @brief Learn whether a leaf type is/includes empty.
 *
 * @param[in] schema Schema node of the leaf to check.
 * @return Whether it includes empty or not.
 */
static int
augds_leaf_is_empty(const struct lysc_node *schema)
{
    const struct lysc_node_leaf *leaf;
    struct lysc_type **types;
    LY_ARRAY_COUNT_TYPE u;

    assert(schema->nodetype & LYD_NODE_TERM);
    leaf = (struct lysc_node_leaf *)schema;

    if (leaf->type->basetype == LY_TYPE_EMPTY) {
        return 1;
    } else if (leaf->type->basetype == LY_TYPE_UNION) {
        types = ((struct lysc_type_union *)leaf->type)->types;
        LY_ARRAY_FOR(types, u) {
            if (types[u]->basetype == LY_TYPE_EMPTY) {
                return 1;
            }
        }
    }

    return 0;
}

/**
 * @brief Create single YANG node and append to existing data.
 *
 * @param[in] schema Schema node of the data node.
 * @param[in] val_str String value of the data node.
 * @param[in] parent Optional parent of the data node.
 * @param[in,out] first First top-level sibling, is updated.
 * @param[out] node Optional created node.
 * @return SR error code.
 */
static int
augds_aug2yang_augnode_create_node(const struct lysc_node *schema, const char *val_str, struct lyd_node *parent,
        struct lyd_node **first, struct lyd_node **node)
{
    int rc = SR_ERR_OK;
    struct lyd_node *new_node;

    /* create and append the node to the parent */
    if (schema->nodetype & LYD_NODE_TERM) {
        if (!val_str && !(schema->flags & LYS_MAND_TRUE) && !augds_leaf_is_empty(schema)) {
            /* optional node without value, do not create */
            goto cleanup;
        }

        /* term node */
        if (lyd_new_term(parent, schema->module, schema->name, val_str, 0, &new_node)) {
            AUG_LOG_ERRLY_GOTO(schema->module->ctx, rc, cleanup);
        }
    } else if (schema->nodetype == LYS_LIST) {
        /* list node */
        if (lyd_new_list(parent, schema->module, schema->name, 0, &new_node, val_str)) {
            AUG_LOG_ERRLY_GOTO(schema->module->ctx, rc, cleanup);
        }
    } else {
        /* container node */
        assert(schema->nodetype == LYS_CONTAINER);
        if (lyd_new_inner(parent, schema->module, schema->name, 0, &new_node)) {
            AUG_LOG_ERRLY_GOTO(schema->module->ctx, rc, cleanup);
        }
        if (val_str) {
            /* we also have the value for the first child */
            if (lyd_new_term(new_node, schema->module, lysc_node_child(schema)->name, val_str, 0, NULL)) {
                AUG_LOG_ERRLY_GOTO(schema->module->ctx, rc, cleanup);
            }
        }
    }

    if (!parent) {
        /* append to top-level siblings */
        if (lyd_insert_sibling(*first, new_node, first)) {
            AUG_LOG_ERRLY_GOTO(schema->module->ctx, rc, cleanup);
        }
    }

    if (node) {
        *node = new_node;
    }

cleanup:
    return rc;
}

/**
 * @brief Check whether the extension path node and augeas label node match.
 *
 * @param[in] ext_node Extension path node.
 * @param[in] label_node Augeas label node.
 * @param[out] node_type Optional type of @p ext_node.
 * @return Whether the nodes are equal or not.
 */
static int
augds_ext_label_node_equal(const char *ext_node, const char *label_node, enum augds_ext_node_type *node_type)
{
    /* handle special ext path node characters */
    if (!strncmp(ext_node, "$$", 2)) {
        /* matches everything */
        if (node_type) {
            *node_type = AUGDS_EXT_NODE_LABEL;
        }
        return 1;
    }

    if (node_type) {
        *node_type = AUGDS_EXT_NODE_VALUE;
    }
    return !strcmp(ext_node, label_node);
}

/**
 * @brief CHeck whether an Augeas label matches a compiled pattern.
 *
 * @param[in] pcode Compiled PCRE2 pattern.
 * @param[in] label_node Augeas label node to match.
 * @param[out] match Set if pattern matches the label.
 * @return SR error code.
 */
static int
augds_pattern_label_match(const pcre2_code *pcode, const char *label_node, int *match)
{
    pcre2_match_data *match_data;
    uint32_t match_opts;
    int r;

    *match = 0;

    match_data = pcre2_match_data_create_from_pattern(pcode, NULL);
    if (!match_data) {
        AUG_LOG_ERRMEM_RET;
    }

    match_opts = PCRE2_ANCHORED;
#ifdef PCRE2_ENDANCHORED
    /* PCRE2_ENDANCHORED was added in PCRE2 version 10.30 */
    match_opts |= PCRE2_ENDANCHORED;
#endif

    /* evaluate */
    r = pcre2_match(pcode, (PCRE2_SPTR)label_node, PCRE2_ZERO_TERMINATED, 0, match_opts, match_data, NULL);
    pcre2_match_data_free(match_data);
    if ((r != PCRE2_ERROR_NOMATCH) && (r < 0)) {
        PCRE2_UCHAR pcre2_errmsg[AUG_PCRE2_MSG_LIMIT] = {0};
        pcre2_get_error_message(r, pcre2_errmsg, AUG_PCRE2_MSG_LIMIT);

        SRPLG_LOG_ERR(srpds_name, "PCRE2 match error (%s).", (const char *)pcre2_errmsg);
        return SR_ERR_SYS;
    } else if (r == 1) {
        *match = 1;
    }

    return SR_ERR_OK;
}

/**
 * @brief Get parent augnode structure of the node referenced by the leafref.
 *
 * @param[in] augnode Augnode structure of the leafref.
 * @param[in] parent YANG data parent of the leafref.
 * @param[out] ext_node_match Data path of the recursive Augeas label to match.
 * @param[out] augnode_list Augnode of the leafref target parent list.
 * @param[out] list_parent Parent YANG data node of the leafref target parent list.
 * @return SR error code.
 */
static int
augds_aug2yang_augnode_leafref_parent(const struct augnode *augnode, const struct lyd_node *parent,
        const char **ext_node_match, struct augnode **augnode_list, struct lyd_node **list_parent)
{
    int rc = SR_ERR_OK;
    const struct lysc_node_leaf *sleaf;
    const struct lysc_type_leafref *lref;
    struct lyd_node *lref_list;
    struct augnode *an;
    const char *path;
    struct ly_set *set = NULL;

    assert(augnode->schema->nodetype == LYS_LEAF);
    sleaf = (struct lysc_node_leaf *)augnode->schema;

    assert(sleaf->type->basetype == LY_TYPE_LEAFREF);
    lref = (struct lysc_type_leafref *)sleaf->type;

    /* get path starting at the parent */
    path = lyxp_get_expr(lref->path);
    assert(!strncmp(path, "../", 3));
    path += 3;

    /* find the target */
    if (lyd_find_xpath(parent, path, &set)) {
        rc = SR_ERR_LY;
        goto cleanup;
    }
    assert(set->count);

    /* get the target parent list */
    lref_list = lyd_parent(set->dnodes[0]);

    /* find its augnode structure */
    for (an = augnode->parent; an; an = an->parent) {
        if (an->schema == lref_list->schema) {
            /* assume the first child is the recursive node */
            assert(an->child && an->child[0].data_path);
            *ext_node_match = augds_get_path_node(an->child[0].data_path, 0);
            *augnode_list = an;
            break;
        }
    }
    assert(an);

    /* return its parent */
    *list_parent = lyd_parent(lref_list);

cleanup:
    ly_set_free(set, NULL);
    return rc;
}

static int augds_aug2yang_augnode_labels_r(augeas *aug, struct augnode *augnodes, uint32_t augnode_count,
        const char *parent_label, char **label_matches, int label_count, struct lyd_node *parent, struct lyd_node **first);

/**
 * @brief Append converted augeas data for specific recursive labels to YANG data. Convert all data handled by a YANG
 * module using the context in the augeas handle.
 *
 * @param[in] aug Augeas handle.
 * @param[in] augnode Augnode of the recursive leafref reference.
 * @param[in] parent_label Augeas data parent label (absolute path).
 * @param[in,out] label_matches Labels matched for @p parent_label, used ones are freed and set to NULL.
 * @param[in] label_count Count of @p label_matches.
 * @param[in] parent YANG data current parent to append to.
 * @return SR error code.
 */
static int
augds_aug2yang_augnode_recursive_labels_r(augeas *aug, const struct augnode *augnode, const char *parent_label,
        char **label_matches, int label_count, struct lyd_node *parent)
{
    int rc = SR_ERR_OK, j;
    const char *ext_node, *label_node;
    char *label, *label_node_d = NULL, idx_str[22];
    struct lyd_node *parent2, *new_node;
    struct augnode *an_list;

    assert(parent);

    /* leaf for recursive children */
    assert(((struct lysc_node_leaf *)augnode->schema)->type->basetype == LY_TYPE_LEAFREF);

    /* find the augnode and data parent of the list that is recursively referenced */
    if ((rc = augds_aug2yang_augnode_leafref_parent(augnode, parent, &ext_node, &an_list, &parent2))) {
        goto cleanup;
    }
    assert((an_list->schema->nodetype == LYS_LIST) && an_list->schema->parent);
    assert(an_list->next_idx && !strcmp(lysc_node_child(an_list->schema)->name, "_r-id"));

    for (j = 0; j < label_count; ++j) {
        label = label_matches[j];
        if (!label) {
            continue;
        }

        label_node = augds_get_label_node(label, &label_node_d);
        if (!augds_ext_label_node_equal(ext_node, label_node, NULL)) {
            /* not a match */
            goto next_iter;
        }

        /* create the new list instance */
        sprintf(idx_str, "%" PRIu64, an_list->next_idx++);
        if ((rc = augds_aug2yang_augnode_create_node(an_list->schema, idx_str, parent2, NULL, &new_node))) {
            goto cleanup;
        }

        /* recursively handle all children of this data node */
        if ((rc = augds_aug2yang_augnode_labels_r(aug, an_list->child, an_list->child_count, parent_label,
                &label_matches[j], 1, new_node, NULL))) {
            goto cleanup;
        }

        /* create the leafref reference to the new recursive list */
        if ((rc = augds_aug2yang_augnode_create_node(augnode->schema, idx_str, parent, NULL, NULL))) {
            goto cleanup;
        }

next_iter:
        free(label_node_d);
        label_node_d = NULL;
    }

cleanup:
    free(label_node_d);
    return rc;
}

/**
 * @brief Append converted augeas data for specific labels to YANG data. Convert all data handled by a YANG module
 * using the context in the augeas handle.
 *
 * @param[in] aug Augeas handle.
 * @param[in] augnodes Array of augnodes to transform.
 * @param[in] augnode_count Count of @p augnodes.
 * @param[in] parent_label Augeas data parent label (absolute path).
 * @param[in,out] label_matches Labels matched for @p parent_label, used ones are freed and set to NULL.
 * @param[in] label_count Count of @p label_matches.
 * @param[in] parent YANG data current parent to append to, may be NULL.
 * @param[in,out] first YANG data first top-level sibling.
 * @return SR error code.
 */
static int
augds_aug2yang_augnode_labels_r(augeas *aug, struct augnode *augnodes, uint32_t augnode_count, const char *parent_label,
        char **label_matches, int label_count, struct lyd_node *parent, struct lyd_node **first)
{
    int rc = SR_ERR_OK, j, m;
    uint32_t i;
    uint64_t local_idx, *idx_p;
    const char *value, *value2, *ext_node, *label_node;
    char *label, *label_node_d = NULL, *pos_str = NULL, idx_str[22];
    enum augds_ext_node_type node_type;
    struct lyd_node *new_node, *parent2;

    for (i = 0; i < augnode_count; ++i) {
        if (augnodes[i].data_path) {
            ext_node = augds_get_path_node(augnodes[i].data_path, 0);

            /* handle all matching labels */
            for (j = 0; j < label_count; ++j) {
                label = label_matches[j];
                if (!label) {
                    continue;
                }

                label_node = augds_get_label_node(label, &label_node_d);
                if (!augds_ext_label_node_equal(ext_node, label_node, &node_type)) {
                    /* not a match */
                    goto next_iter;
                }

                value = NULL;
                value2 = NULL;
                switch (node_type) {
                case AUGDS_EXT_NODE_VALUE:
                    if (augnodes[i].schema->nodetype & LYD_NODE_TERM) {
                        /* get value for a term node */
                        if (aug_get(aug, label, &value) != 1) {
                            AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
                        }
                    }
                    break;
                case AUGDS_EXT_NODE_LABEL:
                    /* make sure it matches the label */
                    if ((rc = augds_pattern_label_match(augnodes[i].pcode, label_node, &m))) {
                        goto cleanup;
                    }
                    if (!m) {
                        goto next_iter;
                    }

                    /* use the label directly */
                    value = label_node;
                    break;
                case AUGDS_EXT_NODE_REC_LIST:
                case AUGDS_EXT_NODE_NONE:
                case AUGDS_EXT_NODE_REC_LREF:
                    /* not sure what happens */
                    assert(0);
                    rc = SR_ERR_INTERNAL;
                    goto cleanup;
                }
                if (augnodes[i].value_path) {
                    /* we will also use the value */
                    if (aug_get(aug, label, &value2) != 1) {
                        AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
                    }
                }

                /* create and append the primary node */
                if ((rc = augds_aug2yang_augnode_create_node(augnodes[i].schema, value, parent, first, &new_node))) {
                    goto cleanup;
                }

                if (augnodes[i].value_path) {
                    /* also create and append the second node */
                    parent2 = (augnodes[i].schema->nodetype & LYD_NODE_TERM) ? parent : new_node;
                    if ((rc = augds_aug2yang_augnode_create_node(augnodes[i].schema2, value2, parent2, first, NULL))) {
                        goto cleanup;
                    }
                }

                /* recursively handle all children of this data node */
                if ((rc = augds_aug2yang_augnode_r(aug, augnodes[i].child, augnodes[i].child_count, label, new_node,
                        first))) {
                    goto cleanup;
                }

                /* label match used, forget it */
                free(label);
                label_matches[j] = NULL;

                if (augnodes[i].schema->nodetype == LYS_LEAF) {
                    /* match was found for a leaf, there can be no more matches */
                    break;
                }

next_iter:
                free(label_node_d);
                label_node_d = NULL;
                free(pos_str);
                pos_str = NULL;
            }
        } else if ((augnodes[i].schema->nodetype == LYS_LIST) && !augnodes[i].schema->parent) {
            /* top-level list node with value being the file path */
            assert(!strcmp(lysc_node_child(augnodes[i].schema)->name, "config-file"));
            assert(!strncmp(parent_label, "/files", 6));
            if ((rc = augds_aug2yang_augnode_create_node(augnodes[i].schema, parent_label + 6, parent, first,
                    &new_node))) {
                goto cleanup;
            }

            /* recursively handle all children of this data node */
            if ((rc = augds_aug2yang_augnode_labels_r(aug, augnodes[i].child, augnodes[i].child_count, parent_label,
                    label_matches, label_count, new_node, first))) {
                goto cleanup;
            }
        } else if ((augnodes[i].schema->nodetype == LYS_LIST) && augnodes[i].schema->parent) {
            /* implicit list with generated key index */
            if (!strcmp(lysc_node_child(augnodes[i].schema)->name, "_id")) {
                /* use local index */
                local_idx = 1;
                idx_p = &local_idx;
            } else {
                /* this key will be referenced recursively, keep global index */
                assert(!strcmp(lysc_node_child(augnodes[i].schema)->name, "_r-id"));
                augnodes[i].next_idx = 1;
                idx_p = &augnodes[i].next_idx;
            }
            for (j = 0; j < label_count; ++j) {
                if (!label_matches[j]) {
                    continue;
                }

                sprintf(idx_str, "%" PRIu64, (*idx_p)++);
                if ((rc = augds_aug2yang_augnode_create_node(augnodes[i].schema, idx_str, parent, first, &new_node))) {
                    goto cleanup;
                }

                /* recursively handle all children of this data node */
                if ((rc = augds_aug2yang_augnode_labels_r(aug, augnodes[i].child, augnodes[i].child_count, parent_label,
                        &label_matches[j], 1, new_node, first))) {
                    goto cleanup;
                }

                if (!lyd_child_no_keys(new_node)) {
                    /* no children matched, free */
                    lyd_free_tree(new_node);
                    --(*idx_p);
                }
            }
        } else if (augnodes[i].schema->nodetype == LYS_LEAF) {
            /* this is a leafref, handle all recursive Augeas data */
            if ((rc = augds_aug2yang_augnode_recursive_labels_r(aug, &augnodes[i], parent_label, label_matches,
                    label_count, parent))) {
                goto cleanup;
            }
        } else {
            /* create a container */
            assert(augnodes[i].schema->nodetype == LYS_CONTAINER);
            if ((rc = augds_aug2yang_augnode_create_node(augnodes[i].schema, NULL, parent, first, &new_node))) {
                goto cleanup;
            }

            /* recursively handle all children of this data node */
            if ((rc = augds_aug2yang_augnode_labels_r(aug, augnodes[i].child, augnodes[i].child_count, parent_label,
                    label_matches, label_count, new_node, first))) {
                goto cleanup;
            }
        }
    }

cleanup:
    free(label_node_d);
    free(pos_str);
    return rc;
}

int
augds_aug2yang_augnode_r(augeas *aug, struct augnode *augnodes, uint32_t augnode_count, const char *parent_label,
        struct lyd_node *parent, struct lyd_node **first)
{
    int rc = SR_ERR_OK, i, label_count = 0;
    char *path = NULL, **label_matches = NULL;

    if (!augnode_count) {
        /* nothing to do */
        goto cleanup;
    }

    /* get all matching augeas labels at this depth, skip comments */
    if (asprintf(&path, "%s/*[label() != '#comment' and label() != '#scomment']", parent_label) == -1) {
        AUG_LOG_ERRMEM_GOTO(rc, cleanup);
    }
    label_count = aug_match(aug, path, &label_matches);
    if (label_count == -1) {
        AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
    }

    /* transform augeas context data to YANG data */
    if ((rc = augds_aug2yang_augnode_labels_r(aug, augnodes, augnode_count, parent_label, label_matches, label_count,
            parent, first))) {
        goto cleanup;
    }

    /* check for non-processed augeas data */
    for (i = 0; i < label_count; ++i) {
        if (label_matches[i]) {
            SRPLG_LOG_WRN(srpds_name, "Non-processed augeas data \"%s\".", label_matches[i]);
            free(label_matches[i]);
        }
    }
    free(label_matches);
    label_matches = NULL;
    label_count = 0;

cleanup:
    free(path);
    for (i = 0; i < label_count; ++i) {
        free(label_matches[i]);
    }
    free(label_matches);
    return rc;
}
