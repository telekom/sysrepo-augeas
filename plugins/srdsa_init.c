/**
 * @file srdsa_init.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief sysrepo DS Augeas plugin init functions
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
 * @brief Free auginfo augnode.
 *
 * @param[in] augnode Augnode to free.
 */
static void
augds_free_info_node(struct augnode *augnode)
{
    uint32_t i;

    for (i = 0; i < augnode->child_count; ++i) {
        augds_free_info_node(&augnode->child[i]);
    }
    free(augnode->child);
}

/**
 * @brief Get pattern to match Augeas labels for this node.
 *
 * @param[in] auginfo Base auginfo structure with the compiled uint64 pattern cache.
 * @param[in] node YANG node with the pattern.
 * @return Compiled pattern.
 */
static const pcre2_code *
augds_init_auginfo_get_pattern(struct auginfo *auginfo, const struct lysc_node *node)
{
    const struct lysc_type *type;
    const struct lysc_type_str *stype;
    const char *pattern;
    int err_code;
    uint32_t compile_opts;
    PCRE2_SIZE err_offset;

    /* get the type */
    if (node->nodetype & LYD_NODE_INNER) {
        assert(lysc_node_child(node)->nodetype & LYD_NODE_TERM);
        type = ((struct lysc_node_leaf *)lysc_node_child(node))->type;
    } else {
        assert(node->nodetype & LYD_NODE_TERM);
        type = ((struct lysc_node_leaf *)node)->type;
    }

    if (type->basetype == LY_TYPE_STRING) {
        /* use the compiled pattern by libyang */
        stype = (const struct lysc_type_str *)type;
        assert(LY_ARRAY_COUNT(stype->patterns) == 1);
        return stype->patterns[0]->code;
    } else if (type->basetype == LY_TYPE_UINT64) {
        /* use the pattern compiled ourselves */
        if (!auginfo->pcode_uint64) {
            /* prepare options and pattern */
            compile_opts = PCRE2_UTF | PCRE2_ANCHORED | PCRE2_DOLLAR_ENDONLY | PCRE2_NO_AUTO_CAPTURE;
#ifdef PCRE2_ENDANCHORED
            compile_opts |= PCRE2_ENDANCHORED;
            pattern = "[0-9]+";
#else
            pattern = "[0-9]+$";
#endif

            /* compile the pattern */
            auginfo->pcode_uint64 = pcre2_compile((PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED, compile_opts, &err_code,
                    &err_offset, NULL);
            if (!auginfo->pcode_uint64) {
                PCRE2_UCHAR err_msg[AUG_PCRE2_MSG_LIMIT] = {0};
                pcre2_get_error_message(err_code, err_msg, AUG_PCRE2_MSG_LIMIT);

                SRPLG_LOG_ERR(srpds_name, "Regular expression \"%s\" is not valid (\"%s\": %s).", pattern,
                        pattern + err_offset, (const char *)err_msg);
                return NULL;
            }
        }
        return auginfo->pcode_uint64;
    }

    AUG_LOG_ERRINT;
    return NULL;
}

/**
 * @brief Init augnodes of schema siblings, recursively.
 *
 * @param[in] auginfo Base auginfo structure.
 * @param[in] mod Module of top-level siblings.
 * @param[in] parent Parent augnode with schema parent to assign to children, NULL if top-level.
 * @param[out] augnode Array of initialized augnodes.
 * @param[out] augnode_count Count of @p augnode.
 * @return SR error code.
 */
static int
augds_init_auginfo_siblings_r(struct auginfo *auginfo, const struct lys_module *mod, struct augnode *parent,
        struct augnode **augnodes, uint32_t *augnode_count)
{
    const struct lysc_node *node = NULL, *node2, *child;
    enum augds_ext_node_type node_type, node_type2;
    const char *data_path, *value_path, *case_data_path;
    struct augnode *anode;
    void *mem;
    uint32_t i, j;
    int r;

    while ((node = lys_getnext(node, parent ? parent->schema : NULL, mod ? mod->compiled : NULL, 0))) {
        /* learn about the node */
        augds_node_get_type(node, &node_type, &data_path, &value_path);
        if (!data_path && (node->nodetype & LYD_NODE_TERM) &&
                (((struct lysc_node_leaf *)node)->type->basetype != LY_TYPE_LEAFREF)) {
            /* non-leafref term nodes without data-path can be skipped */
            continue;
        }

        node2 = NULL;
        if (value_path) {
            /* another schema node sibling (child, if inner node) */
            if (node->nodetype & LYD_NODE_INNER) {
                node2 = lys_find_child(node, mod, value_path, 0, 0, 0);
            } else {
                node2 = lys_find_child(parent ? parent->schema : NULL, mod, value_path, 0, 0, 0);
            }
            if (!node2) {
                AUG_LOG_ERRINT_RET;
            }
        }

        /* allocate new augnode */
        mem = realloc(*augnodes, (*augnode_count + 1) * sizeof **augnodes);
        if (!mem) {
            AUG_LOG_ERRMEM_RET;
        }
        *augnodes = mem;
        anode = &(*augnodes)[*augnode_count];
        ++(*augnode_count);
        memset(anode, 0, sizeof *anode);

        /* fill augnode */
        anode->data_path = data_path;
        anode->value_path = value_path;
        anode->schema = node;
        anode->schema2 = node2;

        if (node_type == AUGDS_EXT_NODE_LABEL) {
            /* get the pattern */
            anode->pcode = augds_init_auginfo_get_pattern(auginfo, node);
        } else if ((node_type == AUGDS_EXT_NODE_NONE) && node->parent && (node->parent->nodetype == LYS_CASE)) {
            /* extra caution, may work for other nodes, too */
            assert(node->nodetype == LYS_CONTAINER);

            /* store the data-path and compiled pattern to use for matching when deciding whether to create this node
             * and hence select the case */
            child = lysc_node_child(node);
            if (child->nodetype == LYS_LIST) {
                /* skip the implicit list */
                child = lysc_node_child(child)->next;
            }
            if (child->nodetype == LYS_CONTAINER) {
                augds_node_get_type(child, &node_type2, &case_data_path, NULL);
                assert(case_data_path);

                /* use the first mandatory child pattern, which is technically the value */
                child = lysc_node_child(child);
                assert(child->flags & LYS_MAND_TRUE);
                anode->pcode = augds_init_auginfo_get_pattern(auginfo, child);
            } else {
                assert(child->nodetype & LYD_NODE_TERM);
                augds_node_get_type(child, &node_type2, &case_data_path, NULL);
                assert(node_type2 == AUGDS_EXT_NODE_VALUE);
                anode->case_data_path = case_data_path;
                anode->pcode = augds_init_auginfo_get_pattern(auginfo, child);
            }
        }

        /* fill augnode children, recursively */
        if ((r = augds_init_auginfo_siblings_r(auginfo, mod, anode, &anode->child, &anode->child_count))) {
            return r;
        }
    }

    /* set all children parents after we have them all */
    for (i = 0; i < *augnode_count; ++i) {
        for (j = 0; j < (*augnodes)[i].child_count; ++j) {
            (*augnodes)[i].child[j].parent = &(*augnodes)[i];
        }
    }

    return SR_ERR_OK;
}

int
augds_init(struct auginfo *auginfo, const struct lys_module *mod, struct augmod **augmod)
{
    int rc = SR_ERR_OK;
    uint32_t i;
    const char *lens;
    char *path = NULL, *value = NULL;
    void *ptr;
    struct augmod *augm = NULL;

    if (!auginfo->aug) {
        /* init augeas with all modules but no loaded files */
        auginfo->aug = aug_init(NULL, NULL, AUG_NO_LOAD | AUG_NO_ERR_CLOSE | AUG_SAVE_BACKUP);
        if ((rc = augds_check_erraug(auginfo->aug))) {
            goto cleanup;
        }

        /* remove all lenses except this one so we are left only with 'incl' and 'excl' for all the lenses */
        aug_rm(auginfo->aug, "/augeas/load/*/lens");
    }

    /* try to find this module in auginfo, it must be there if already initialized */
    for (i = 0; i < auginfo->mod_count; ++i) {
        if (auginfo->mods[i].mod == mod) {
            /* found */
            augm = &auginfo->mods[i];
            goto cleanup;
        }
    }

    /* get lens name */
    if ((rc = augds_get_lens(mod, &lens))) {
        goto cleanup;
    }

    /* set this lens so that it can be loaded */
    if (asprintf(&path, "/augeas/load/%s/lens", lens) == -1) {
        AUG_LOG_ERRMEM_GOTO(rc, cleanup);
    }
    if (asprintf(&value, "@%s", lens) == -1) {
        AUG_LOG_ERRMEM_GOTO(rc, cleanup);
    }
    if (aug_set(auginfo->aug, path, value) == -1) {
        AUG_LOG_ERRAUG_GOTO(auginfo->aug, rc, cleanup);
    }

#ifdef AUG_TEST_INPUT_FILES
    /* for testing, remove all default includes */
    free(path);
    if (asprintf(&path, "/augeas/load/%s/incl", lens) == -1) {
        AUG_LOG_ERRMEM_GOTO(rc, cleanup);
    }
    aug_rm(auginfo->aug, path);

    /* create new file instead of creating a backup and overwriting */
    if (aug_set(auginfo->aug, "/augeas/save", "newfile") == -1) {
        AUG_LOG_ERRAUG_GOTO(auginfo->aug, rc, cleanup);
    }

    /* set only test files to be loaded */
    free(value);
    value = strdup(AUG_TEST_INPUT_FILES);
    i = 1;
    for (ptr = strtok(value, ";"); ptr; ptr = strtok(NULL, ";")) {
        free(path);
        if (asprintf(&path, "/augeas/load/%s/incl[%" PRIu32 "]", lens, i++) == -1) {
            AUG_LOG_ERRMEM_GOTO(rc, cleanup);
        }
        if (aug_set(auginfo->aug, path, ptr) == -1) {
            AUG_LOG_ERRAUG_GOTO(auginfo->aug, rc, cleanup);
        }
    }
#endif

    /* load data to populate parsed files */
    aug_load(auginfo->aug);
    if ((rc = augds_check_erraug(auginfo->aug))) {
        goto cleanup;
    }

    /* create new auginfo module */
    ptr = realloc(auginfo->mods, (auginfo->mod_count + 1) * sizeof *auginfo->mods);
    if (!ptr) {
        AUG_LOG_ERRMEM_GOTO(rc, cleanup);
    }
    auginfo->mods = ptr;
    augm = &auginfo->mods[auginfo->mod_count];
    ++auginfo->mod_count;

    /* fill auginfo module */
    augm->mod = mod;
    augm->toplevel = NULL;
    augm->toplevel_count = 0;
    if ((rc = augds_init_auginfo_siblings_r(auginfo, mod, NULL, &augm->toplevel, &augm->toplevel_count))) {
        goto cleanup;
    }

cleanup:
    free(path);
    free(value);
    if (augmod) {
        *augmod = augm;
    }
    if (rc) {
        augds_destroy(auginfo);
        if (augmod) {
            *augmod = NULL;
        }
    }
    return rc;
}

void
augds_destroy(struct auginfo *auginfo)
{
    struct augmod *mod;
    uint32_t i, j;

    /* free auginfo */
    for (i = 0; i < auginfo->mod_count; ++i) {
        mod = &auginfo->mods[i];
        for (j = 0; j < mod->toplevel_count; ++j) {
            augds_free_info_node(&mod->toplevel[j]);
        }
        free(mod->toplevel);
    }
    free(auginfo->mods);
    auginfo->mods = NULL;
    auginfo->mod_count = 0;

    /* destroy augeas */
    aug_close(auginfo->aug);
    auginfo->aug = NULL;

    /* free compiled patterns */
    pcre2_code_free(auginfo->pcode_uint64);
}
