/**
 * @file srds_augeas.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief sysrepo DS plugin for augeas-supported configuration files
 *
 * @copyright
 * Copyright (c) 2021 Deutsche Telekom AG.
 * Copyright (c) 2021 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#define _GNU_SOURCE

#include <sysrepo.h>
#include <sysrepo/plugins_datastore.h>

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include <augeas.h>
#include <libyang/libyang.h>

#define srpds_name "augeas DS"  /**< plugin name */

#define AUG_LOG_ERRINT SRPLG_LOG_ERR(srpds_name, "Internal error (%s:%d).", __FILE__, __LINE__)
#define AUG_LOG_ERRMEM SRPLG_LOG_ERR(srpds_name, "Memory allocation failed (%s:%d).", __FILE__, __LINE__)

#define AUG_LOG_ERRINT_RET AUG_LOG_ERRINT;return SR_ERR_INTERNAL
#define AUG_LOG_ERRINT_GOTO(rc, label) AUG_LOG_ERRINT;rc = SR_ERR_INTERNAL; goto label
#define AUG_LOG_ERRMEM_RET AUG_LOG_ERRMEM;return SR_ERR_NO_MEMORY
#define AUG_LOG_ERRMEM_GOTO(rc, label) AUG_LOG_ERRMEM;rc = SR_ERR_NO_MEMORY; goto label
#define AUG_LOG_ERRAUG_GOTO(augeas, rc, label) rc = augds_check_erraug(augeas);assert(rc);goto label
#define AUG_LOG_ERRLY_GOTO(ctx, rc, label) augds_log_errly(ctx);rc = SR_ERR_LY;goto label

enum augds_ext_node_type {
    AUGDS_EXT_NODE_VALUE,       /**< matches specific augeas label value */
    AUGDS_EXT_NODE_POSITION,    /**< matches specific augeas label position, starts with '##' */
    AUGDS_EXT_NODE_LABEL_VALUE  /**< matches any augeas label with value being the label, starts with '$$' */
};

static struct auginfo {
    augeas *aug;    /**< augeas handle */

    struct augnode {
        const char *ext_path;           /**< data-path of the augeas-extension in the schema node */
        const struct lysc_node *schema; /**< schema node */
        struct augnode *child;          /**< array of children of this node */
        uint32_t child_count;           /**< number of children */
    } *toplevel;    /**< array of top-level nodes */
    uint32_t toplevel_count;    /**< top-level node count */
} auginfo;

/**
 * @brief Free agingo augnode.
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
 * @brief Get augeas lens name from a YANG module.
 *
 * @param[in] mod YANG module to use.
 * @return Augeas lens name.
 * @return NULL on error.
 */
static char *
augds_get_lens(const struct lys_module *mod)
{
    char *lens;

    lens = strdup(mod->name);
    if (!lens) {
        return NULL;
    }

    lens[0] = toupper(lens[0]);
    return lens;
}

/**
 * @brief Check for augeas errors.
 *
 * @param[in] aug Augeas handle.
 * @return SR error code to return.
 */
static int
augds_check_erraug(augeas *aug)
{
    const char *aug_err_mmsg, *aug_err_details;

    if (!aug) {
        SRPLG_LOG_ERR(srpds_name, "Augeas init failed.");
        return SR_ERR_OPERATION_FAILED;
    }

    if (aug_error(aug) == AUG_NOERROR) {
        /* no error */
        return SR_ERR_OK;
    }

    if (aug_error(aug) == AUG_ENOMEM) {
        /* memory error */
        AUG_LOG_ERRMEM;
        return SR_ERR_NO_MEMORY;
    }

    /* complex augeas error */
    aug_err_mmsg = aug_error_minor_message(aug);
    aug_err_details = aug_error_details(aug);
    SRPLG_LOG_ERR(srpds_name, "Augeas init failed (%s%s%s%s%s).", aug_error_message(aug),
            aug_err_mmsg ? "; " : "", aug_err_mmsg ? aug_err_mmsg : "", aug_err_details ? "; " : "",
            aug_err_details ? aug_err_details : "");
    return SR_ERR_OPERATION_FAILED;
}

/**
 * @brief Log libyang errors.
 *
 * @param[in] ly_ctx Context to read errors from.
 */
static void
augds_log_errly(const struct ly_ctx *ly_ctx)
{
    struct ly_err_item *e;

    e = ly_err_first(ly_ctx);
    if (!e) {
        SRPLG_LOG_ERR(srpds_name, "Unknown libyang error.");
        return;
    }

    do {
        if (e->level == LY_LLWRN) {
            SRPLG_LOG_WRN(srpds_name, e->msg);
        } else {
            assert(e->level == LY_LLERR);
            SRPLG_LOG_ERR(srpds_name, e->msg);
        }

        e = e->next;
    } while (e);

    ly_err_clean((struct ly_ctx *)ly_ctx, NULL);
}

/**
 * @brief Get augeas-extension data-path for this node.
 *
 * @param[in] node Schema node to use.
 * @return Augeas extension path, NULL if none defined.
 */
static const char *
augds_get_ext_path(const struct lysc_node *node)
{
    LY_ARRAY_COUNT_TYPE u;
    const struct lysc_ext *ext;

    LY_ARRAY_FOR(node->exts, u) {
        ext = node->exts[u].def;
        if (!strcmp(ext->module->name, "augeas-extension") && !strcmp(ext->name, "data-path")) {
            return node->exts[u].argument;
        }
    }

    return NULL;
}

/**
 * @brief Get last path segment (node) from a path.
 *
 * @param[in] path Augeas path.
 * @return Last node.
 */
static const char *
augds_get_path_node(const char *path)
{
    const char *ptr;

    /* find last path segment */
    ptr = strrchr(path, '/');
    return ptr ? ptr + 1 : path;
}

/**
 * @brief Get last path segment (node) position.
 *
 * @param[in] path Augeas path.
 * @return Last node position.
 */
static char *
augds_get_path_node_position(const char *path)
{
    size_t len;
    const char *ptr;
    char *str;

    len = strlen(path);
    if (path[len - 1] != ']') {
        /* single instance */
        return strdup("1");
    }

    /* find predicate beginning */
    for (ptr = (path + len) - 2; *ptr != '['; --ptr) {
        assert(isdigit(*ptr));
    }

    /* print position to string */
    if (asprintf(&str, "%d", atoi(ptr + 1)) == -1) {
        return NULL;
    }
    return str;
}

/**
 * @brief Init augnodes of schema siblings, recursively.
 *
 * @param[in] mod Module of top-level siblings.
 * @param[in] parent Parent of child siblings.
 * @param[out] augnode Array of initialized augnodes.
 * @param[out] augnode_count Count of @p augnode.
 * @return SR error code.
 */
static int
augds_init_auginfo_siblings_r(const struct lys_module *mod, const struct lysc_node *parent, struct augnode **augnodes,
        uint32_t *augnode_count)
{
    const struct lysc_node *node = NULL;
    const char *ext_path;
    struct augnode *anode;
    void *mem;
    int r;

    while ((node = lys_getnext(node, parent, mod ? mod->compiled : NULL, 0))) {
        if (lysc_is_key(node)) {
            /* keys can be skipped, their extension path is on their list */
            continue;
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
        ext_path = augds_get_ext_path(node);
        if (ext_path) {
            anode->ext_path = ext_path;
        } else if (!(node->nodetype & LYD_NODE_INNER)) {
            SRPLG_LOG_WRN(srpds_name, "Module \"%s\" node \"%s\" without augeas path extension.",
                    node->module->name, node->name);
        }
        anode->schema = node;

        /* fill augnode children, recursively */
        if ((r = augds_init_auginfo_siblings_r(NULL, node, &anode->child, &anode->child_count))) {
            return r;
        }
    }

    return SR_ERR_OK;
}

static int
srpds_aug_init(const struct lys_module *mod, sr_datastore_t ds, const char *owner, const char *group, mode_t perm)
{
    int rc = SR_ERR_OK;
    char *lens = NULL, *path = NULL;

    assert(perm);

    if (ds != SR_DS_STARTUP) {
        SRPLG_LOG_ERR(srpds_name, "Only startup datastore is supported by this DS plugin.");
        return SR_ERR_UNSUPPORTED;
    }

    /* init augeas with all modules but no loaded files */
    auginfo.aug = aug_init(NULL, NULL, AUG_NO_LOAD | AUG_NO_ERR_CLOSE);
    if ((rc = augds_check_erraug(auginfo.aug))) {
        goto cleanup;
    }

    /* get lens name */
    lens = augds_get_lens(mod);
    if (!lens) {
        AUG_LOG_ERRMEM_GOTO(rc, cleanup);
    }

    /* remove all lenses except this one */
    if (asprintf(&path, "/augeas/load/*[label() != '%s']", lens) == -1) {
        AUG_LOG_ERRMEM_GOTO(rc, cleanup);
    }
    aug_rm(auginfo.aug, path);

#ifdef AUG_TEST_INPUT_FILE
    /* for testing, remove all default includes */
    free(path);
    if (asprintf(&path, "/augeas/load/%s/incl", lens) == -1) {
        AUG_LOG_ERRMEM_GOTO(rc, cleanup);
    }
    aug_rm(auginfo.aug, path);

    /* set only this single test file to be loaded */
    if (aug_set(auginfo.aug, path, AUG_TEST_INPUT_FILE) == -1) {
        AUG_LOG_ERRAUG_GOTO(auginfo.aug, rc, cleanup);
    }
#endif

    /* build auginfo structure */
    if ((rc = augds_init_auginfo_siblings_r(mod, NULL, &auginfo.toplevel, &auginfo.toplevel_count))) {
        goto cleanup;
    }

    /* keep owner/group/perms as they are */
    (void)owner;
    (void)group;
    (void)perm;

cleanup:
    free(lens);
    free(path);
    if (rc) {
        aug_close(auginfo.aug);
        auginfo.aug = NULL;
    }
    return rc;
}

static int
srpds_aug_destroy(const struct lys_module *mod, sr_datastore_t ds)
{
    uint32_t i;

    (void)mod;
    (void)ds;

    /* free auginfo */
    for (i = 0; i < auginfo.toplevel_count; ++i) {
        augds_free_info_node(&auginfo.toplevel[i]);
    }
    free(auginfo.toplevel);
    auginfo.toplevel = NULL;
    auginfo.toplevel_count = 0;

    /* destroy augeas */
    aug_close(auginfo.aug);
    auginfo.aug = NULL;

    return SR_ERR_OK;
}

static int
srpds_aug_store(const struct lys_module *mod, sr_datastore_t ds, const struct lyd_node *mod_data)
{
    /* get current data */
    /* TODO */
}

static void
srpds_aug_recover(const struct lys_module *mod, sr_datastore_t ds)
{
    /* TODO */
}

static int
augds_aug2yang_augnode_create_node(const struct lysc_node *schema, const char *val_str, struct lyd_node *parent,
        struct lyd_node **first, struct lyd_node **node)
{
    int rc = SR_ERR_OK;

    /* create and append the node to the parent */
    if (schema->nodetype & LYD_NODE_TERM) {
        /* term node */
        if (lyd_new_term(parent, schema->module, schema->name, val_str, 0, node)) {
            AUG_LOG_ERRLY_GOTO(schema->module->ctx, rc, cleanup);
        }
    } else if (schema->nodetype == LYS_LIST) {
        /* list node */
        if (lyd_new_list(parent, schema->module, schema->name, 0, node, val_str)) {
            AUG_LOG_ERRLY_GOTO(schema->module->ctx, rc, cleanup);
        }
    } else {
        /* container node */
        assert(schema->nodetype == LYS_CONTAINER);
        if (lyd_new_inner(parent, schema->module, schema->name, 0, node)) {
            AUG_LOG_ERRLY_GOTO(schema->module->ctx, rc, cleanup);
        }
    }

    if (!parent) {
        /* append to top-level siblings */
        if (lyd_insert_sibling(*first, *node, first)) {
            AUG_LOG_ERRLY_GOTO(schema->module->ctx, rc, cleanup);
        }
    }

cleanup:
    return rc;
}

static int
augds_ext_label_node_equal(const char *ext_node, const char *label_node, enum augds_ext_node_type *node_type)
{
    size_t len;

    /* handle special ext path node characters */
    if (!strncmp(ext_node, "$$", 2)) {
        /* matches everything */
        if (node_type) {
            *node_type = AUGDS_EXT_NODE_LABEL_VALUE;
        }
        return 1;
    } else if (!strncmp(ext_node, "##", 2)) {
        if (node_type) {
            *node_type = AUGDS_EXT_NODE_POSITION;
        }
        ext_node += 2;
    } else {
        if (node_type) {
            *node_type = AUGDS_EXT_NODE_VALUE;
        }
    }

    /* get length to compare */
    len = strlen(label_node);
    if (label_node[len - 1] == ']') {
        /* ignore position predicate */
        len = strrchr(label_node, '[') - label_node;
    }

    if (strlen(ext_node) != len) {
        return 0;
    }

    return !strncmp(ext_node, label_node, len);
}

static int augds_aug2yang_augnode_r(augeas *aug, const struct augnode *augnodes, uint32_t augnode_count,
        const char *parent_label, struct lyd_node *parent, struct lyd_node **first);

/**
 * @brief Append converted augeas data for specific labels to YANG data. Convert all data handled by a YANG module
 * using the context in the augeas handle.
 *
 * @param[in] aug Augeas handle.
 * @param[in] augnodes Array of augnodes to transform.
 * @param[in] augnode_count Count of @p augnodes.
 * @param[in] parent_label Augeas data parent label (absolute path).
 * @param[in,out] label_matches Labels matched for @p parent_label, used ones are freed.
 * @param[in,out] label_count Count of @p label_matches, is updated.
 * @param[in,out] parent YANG data current parent to append to, may be NULL.
 * @param[in,out] first YANG data first top-level sibling.
 * @return SR error code.
 */
static int
augds_aug2yang_augnode_labels_r(augeas *aug, const struct augnode *augnodes, uint32_t augnode_count, const char *parent_label,
        char ***label_matches, int *label_count, struct lyd_node *parent, struct lyd_node **first)
{
    int rc = SR_ERR_OK, j;
    uint32_t i, k;
    const char *value, *ext_node, *label_node;
    char *label, *pos_str = NULL;
    enum augds_ext_node_type node_type;
    struct lyd_node *new_node;

    if (!*label_count) {
        /* nothing to match */
        goto cleanup;
    }

    for (i = 0; i < augnode_count; ++i) {
        if (augnodes[i].ext_path) {
            ext_node = augds_get_path_node(augnodes[i].ext_path);

            /* handle all matching labels */
            j = 0;
            while (j < *label_count) {
                label = (*label_matches)[j];
                label_node = augds_get_path_node(label);
                if (!augds_ext_label_node_equal(ext_node, label_node, &node_type)) {
                    /* not a match */
                    ++j;
                    continue;
                }

                switch (node_type) {
                case AUGDS_EXT_NODE_VALUE:
                    /* get value */
                    if (aug_get(aug, label, &value) != 1) {
                        AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
                    }

                    if (!value || !strlen(value)) {
                        /* no value, skip */
                        ++j;
                        continue;
                    }
                    break;
                case AUGDS_EXT_NODE_POSITION:
                    /* get position */
                    pos_str = augds_get_path_node_position(label);
                    value = pos_str;
                    break;
                case AUGDS_EXT_NODE_LABEL_VALUE:
                    /* make sure it matches no following paths */
                    for (k = i + 1; k < augnode_count; ++k) {
                        if (augds_ext_label_node_equal(augds_get_path_node(augnodes[k].ext_path), label_node, NULL)) {
                            break;
                        }
                    }
                    if (k < augnode_count) {
                        /* more specific match, skip */
                        ++j;
                        continue;
                    }

                    /* use the label directly */
                    value = label_node;
                    break;
                }

                /* create and append the term node */
                if ((rc = augds_aug2yang_augnode_create_node(augnodes[i].schema, value, parent, first, &new_node))) {
                    goto cleanup;
                }

                /* recursively handle all children of this data node */
                if ((rc = augds_aug2yang_augnode_r(aug, augnodes[i].child, augnodes[i].child_count, label, new_node,
                        first))) {
                    goto cleanup;
                }

                /* label match used, forget it */
                free(label);
                --(*label_count);
                if (j < *label_count) {
                    memmove((*label_matches) + j, (*label_matches) + j + 1, ((*label_count) - j) * sizeof **label_matches);
                } else if (!*label_count) {
                    free(*label_matches);
                    *label_matches = NULL;
                }

                /* loop cleanup */
                free(pos_str);
                pos_str = NULL;

                if (augnodes[i].schema->nodetype == LYS_LEAF) {
                    /* match was found for a leaf, there can be no more matches */
                    break;
                }
            }
        } else {
            /* create and append the inner node */
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
    free(pos_str);
    return rc;
}

static int
augds_aug2yang_augnode_r(augeas *aug, const struct augnode *augnodes, uint32_t augnode_count, const char *parent_label,
        struct lyd_node *parent, struct lyd_node **first)
{
    int rc = SR_ERR_OK, i, label_count = 0;
    char *path = NULL, **label_matches = NULL;

    if (!augnode_count) {
        /* nothing to do */
        goto cleanup;
    }

    /* get all matching augeas labels at this depth */
    if (asprintf(&path, "%s/*", parent_label) == -1) {
        AUG_LOG_ERRMEM_GOTO(rc, cleanup);
    }
    label_count = aug_match(aug, path, &label_matches);
    if (label_count == -1) {
        AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
    }

    /* transform augeas context data to YANG data */
    if ((rc = augds_aug2yang_augnode_labels_r(aug, augnodes, augnode_count, parent_label, &label_matches, &label_count,
            parent, first))) {
        goto cleanup;
    }

    /* check for non-processed augeas data */
    for (i = 0; i < label_count; ++i) {
        SRPLG_LOG_WRN(srpds_name, "Non-processed augeas data \"%s\".", label_matches[i]);
        free(label_matches[i]);
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

static int
srpds_aug_load(const struct lys_module *mod, sr_datastore_t ds, const char **xpaths, uint32_t xpath_count,
        struct lyd_node **mod_data)
{
    int rc = SR_ERR_OK, i, file_count = 0;
    char **file_matches = NULL, *path = NULL;
    const char *aug_file;

    *mod_data = NULL;

    /* load data */
    aug_load(auginfo.aug);
    if ((rc = augds_check_erraug(auginfo.aug))) {
        goto cleanup;
    }

    /* get parsed file path nodes */
    file_count = aug_match(auginfo.aug, "/augeas/files//path", &file_matches);
    if (file_count == -1) {
        AUG_LOG_ERRAUG_GOTO(auginfo.aug, rc, cleanup);
    }

    /* get all their values and append their YANG data */
    for (i = 0; i < file_count; ++i) {
        /* get augeas path of data from the current file */
        if (aug_get(auginfo.aug, file_matches[i], &aug_file) != 1) {
            AUG_LOG_ERRINT_GOTO(rc, cleanup);
        }

        /* transform augeas context data to YANG data */
        if ((rc = augds_aug2yang_augnode_r(auginfo.aug, auginfo.toplevel, auginfo.toplevel_count, aug_file,
                NULL, mod_data))) {
            goto cleanup;
        }
    }

cleanup:
    free(path);
    for (i = 0; i < file_count; ++i) {
        free(file_matches[i]);
    }
    free(file_matches);
    if (rc) {
        lyd_free_siblings(*mod_data);
        *mod_data = NULL;
    }
    return rc;
}

static int
srpds_aug_copy(const struct lys_module *mod, sr_datastore_t trg_ds, sr_datastore_t src_ds)
{
    (void)mod;
    (void)trg_ds;
    (void)src_ds;

    AUG_LOG_ERRINT_RET;
}

static int
srpds_aug_update_differ(const struct lys_module *old_mod, const struct lyd_node *old_mod_data,
        const struct lys_module *new_mod, const struct lyd_node *new_mod_data, int *differ)
{
    LY_ERR lyrc;

    (void)old_mod;

    /* check for data difference */
    lyrc = lyd_compare_siblings(new_mod_data, old_mod_data, LYD_COMPARE_FULL_RECURSION | LYD_COMPARE_DEFAULTS);
    if (lyrc && (lyrc != LY_ENOT)) {
        augds_log_errly(new_mod->ctx);
        return SR_ERR_LY;
    }

    if (lyrc == LY_ENOT) {
        *differ = 1;
    } else {
        *differ = 0;
    }
    return SR_ERR_OK;
}

static int
srpds_aug_candidate_modified(const struct lys_module *mod, int *modified)
{
    (void)mod;
    (void)modified;

    AUG_LOG_ERRINT_RET;
}

static int
srpds_aug_candidate_reset(const struct lys_module *mod)
{
    (void)mod;

    AUG_LOG_ERRINT_RET;
}

static int
srpds_aug_access_set(const struct lys_module *mod, sr_datastore_t ds, const char *owner, const char *group, mode_t perm)
{
    assert(mod && (owner || group || perm));

    /* TODO */
}

static int
srpds_aug_access_get(const struct lys_module *mod, sr_datastore_t ds, char **owner, char **group, mode_t *perm)
{
    if (owner) {
        *owner = NULL;
    }
    if (group) {
        *group = NULL;
    }

    /* TODO */
}

static int
srpds_aug_access_check(const struct lys_module *mod, sr_datastore_t ds, int *read, int *write)
{
    /* TODO */
}

SRPLG_DATASTORE = {
    .name = srpds_name,
    .init_cb = srpds_aug_init,
    .destroy_cb = srpds_aug_destroy,
    .store_cb = srpds_aug_store,
    .recover_cb = srpds_aug_recover,
    .load_cb = srpds_aug_load,
    .copy_cb = srpds_aug_copy,
    .update_differ_cb = srpds_aug_update_differ,
    .candidate_modified_cb = srpds_aug_candidate_modified,
    .candidate_reset_cb = srpds_aug_candidate_reset,
    .access_set_cb = srpds_aug_access_set,
    .access_get_cb = srpds_aug_access_get,
    .access_check_cb = srpds_aug_access_check,
};
