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

enum augds_diff_op {
    AUGDS_OP_UNKNOWN = 0,
    AUGDS_OP_CREATE,
    AUGDS_OP_DELETE,
    AUGDS_OP_REPLACE,
    AUGDS_OP_NONE
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
 * @brief Get last path segment (node) from a path.
 *
 * @param[in] path Augeas path.
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
 * @brief Check for augeas errors.
 *
 * @param[in] aug Augeas handle.
 * @return SR error code to return.
 */
static int
augds_check_erraug(augeas *aug)
{
    int rc = SR_ERR_OK, i, label_count = 0;
    const char *aug_err_mmsg, *aug_err_details, *data_error, *data_error_msg;
    char **label_matches = NULL;

    if (!aug) {
        SRPLG_LOG_ERR(srpds_name, "Augeas init failed.");
        return SR_ERR_OPERATION_FAILED;
    }

    if (aug_error(aug) == AUG_ENOMEM) {
        /* memory error */
        AUG_LOG_ERRMEM;
        return SR_ERR_NO_MEMORY;
    } else if (aug_error(aug) != AUG_NOERROR) {
        /* complex augeas error */
        aug_err_mmsg = aug_error_minor_message(aug);
        aug_err_details = aug_error_details(aug);
        SRPLG_LOG_ERR(srpds_name, "Augeas init failed (%s%s%s%s%s).", aug_error_message(aug),
                aug_err_mmsg ? "; " : "", aug_err_mmsg ? aug_err_mmsg : "", aug_err_details ? "; " : "",
                aug_err_details ? aug_err_details : "");
        return SR_ERR_OPERATION_FAILED;
    }

    /* check for data error */
    label_count = aug_match(aug, "/augeas/files//error//.", &label_matches);
    if (label_count == -1) {
        AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
    } else if (!label_count) {
        /* no error */
        goto cleanup;
    }

    /* data error */
    assert(!strcmp(augds_get_path_node(label_matches[0], 0), "error"));
    if (aug_get(aug, label_matches[0], &data_error) != 1) {
        AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
    }

    /* data error message */
    data_error_msg = NULL;
    for (i = 1; i < label_count; ++i) {
        if (!strcmp(augds_get_path_node(label_matches[i], 0), "message")) {
            if (aug_get(aug, label_matches[i], &data_error_msg) != 1) {
                AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
            }
            break;
        }
    }

    assert(data_error);
    if (data_error_msg) {
        SRPLG_LOG_ERR(srpds_name, "Augeas data error \"%s\" (%s).", data_error, data_error_msg);
    } else {
        SRPLG_LOG_ERR(srpds_name, "Augeas data error \"%s\".", data_error);
    }
    rc = SR_ERR_OPERATION_FAILED;

cleanup:
    for (i = 0; i < label_count; ++i) {
        free(label_matches[i]);
    }
    free(label_matches);
    return rc;
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
    auginfo.aug = aug_init(NULL, NULL, AUG_NO_LOAD | AUG_NO_ERR_CLOSE | AUG_SAVE_BACKUP);
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

    /* create new file instead of creating a backup and overwriting */
    if (aug_set(auginfo.aug, "/augeas/save", "newfile") == -1) {
        AUG_LOG_ERRAUG_GOTO(auginfo.aug, rc, cleanup);
    }

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

static enum augds_diff_op
augds_diff_str2op(const char *str)
{
    switch (str[0]) {
    case 'c':
        assert(!strcmp(str, "create"));
        return AUGDS_OP_CREATE;
    case 'd':
        assert(!strcmp(str, "delete"));
        return AUGDS_OP_DELETE;
    case 'r':
        assert(!strcmp(str, "replace"));
        return AUGDS_OP_REPLACE;
    case 'n':
        assert(!strcmp(str, "none"));
        return AUGDS_OP_NONE;
    }

    AUG_LOG_ERRINT;
    return 0;
}

/**
 * @brief Learn operation of a diff node.
 *
 * @param[in] diff_node Diff node.
 * @return Diff node operation.
 * @return 0 if no operation is defined on the node.
 */
static enum augds_diff_op
augds_diff_get_op(const struct lyd_node *diff_node)
{
    struct lyd_meta *meta;

    LY_LIST_FOR(diff_node->meta, meta) {
        if (!strcmp(meta->name, "operation") && !strcmp(meta->annotation->module->name, "yang")) {
            return augds_diff_str2op(lyd_get_meta_value(meta));
        }
    }

    return 0;
}

static const char *
augds_strchr_back(const char *start, const char *s, int c)
{
    const char *ptr;

    for (ptr = s; ptr[0] != c; --ptr) {
        if (ptr == start) {
            return NULL;
        }
    }

    return ptr;
}

static int
augds_node_yang2aug(const struct lyd_node *node, const char *parent_aug_path, char **aug_path, const char **aug_value)
{
    int rc = SR_ERR_OK, path_seg_len;
    const char *ext_path, *parent_ext_path, *parent_node, *start, *end, *node_name, *path_segment;
    char *result = NULL, *tmp;
    enum augds_ext_node_type node_type;

    /* node extension data-path */
    ext_path = augds_get_ext_path(node->schema);
    if (!ext_path) {
        /* nothing to set in augeas data */
        *aug_path = NULL;
        *aug_value = NULL;
        goto cleanup;
    }

    /* parent last node */
    parent_ext_path = node->parent ? augds_get_ext_path(node->parent->schema) : NULL;
    parent_node = parent_ext_path ? augds_get_path_node(parent_ext_path, 1) : NULL;

    end = ext_path + strlen(ext_path);
    do {
        start = augds_strchr_back(ext_path, end - 1, '/');
        if (start) {
            /* skip slash */
            ++start;
        } else {
            /* first node, last processed */
            start = ext_path;
        }

        /* handle special ext path node characters */
        if (!strncmp(start, "$$", 2)) {
            node_type = AUGDS_EXT_NODE_LABEL_VALUE;
            node_name = start + 2;
        } else if (!strncmp(start, "##", 2)) {
            node_type = AUGDS_EXT_NODE_POSITION;
            node_name = start + 2;
        } else {
            node_type = AUGDS_EXT_NODE_VALUE;
            node_name = start;
        }

        if (parent_node && ((signed)strlen(parent_node) == end - node_name) &&
                !strncmp(parent_node, node_name, strlen(parent_node))) {
            /* remaining ext path prefix is the ext path of the parent */
            assert(parent_aug_path);
            path_segment = parent_aug_path;
            path_seg_len = strlen(path_segment);

            /* full path processed, exit loop */
            start = ext_path;
        } else {
            switch (node_type) {
            case AUGDS_EXT_NODE_LABEL_VALUE:
                /* YANG data value as augeas label */
                if (node->schema->nodetype == LYS_LIST) {
                    assert(lyd_child(node)->schema->flags & LYS_KEY);
                    path_segment = lyd_get_value(lyd_child(node));
                } else {
                    path_segment = lyd_get_value(node);
                }
                path_seg_len = strlen(path_segment);
                break;
            case AUGDS_EXT_NODE_POSITION:
            case AUGDS_EXT_NODE_VALUE:
                /* ext path node (YANG schema node name) as augeas label */
                path_segment = node_name;
                path_seg_len = end - node_name;
                break;
            }
        }

        if (!result) {
            /* last node being processed (first iteration), set augeas value */
            switch (node_type) {
            case AUGDS_EXT_NODE_LABEL_VALUE:
            case AUGDS_EXT_NODE_POSITION:
                *aug_value = NULL;
                break;
            case AUGDS_EXT_NODE_VALUE:
                if (node->schema->nodetype == LYS_LIST) {
                    assert(lyd_child(node)->flags & LYS_KEY);
                    *aug_value = lyd_get_value(lyd_child(node));
                } else {
                    *aug_value = lyd_get_value(node);
                }
                break;
            }
        }

        /* add path segment */
        if (asprintf(&tmp, "%.*s%s%s", path_seg_len, path_segment, result ? "/" : "", result ? result : "") == -1) {
            AUG_LOG_ERRMEM_GOTO(rc, cleanup);
        }
        free(result);
        result = tmp;

        /* next iter */
        end = start - 1;
    } while (start != ext_path);

cleanup:
    if (rc) {
        free(result);
        *aug_path = NULL;
    } else {
        *aug_path = result;
    }
    return rc;
}

static int
augds_yang2aug_diff_siblings_r(augeas *aug, const struct lyd_node *diff, const char *parent_path,
        enum augds_diff_op parent_op)
{
    int rc = SR_ERR_OK;
    enum augds_diff_op cur_op;
    char *aug_path = NULL;
    const char *aug_value;

    LY_LIST_FOR(diff, diff) {
        /* generate augeas path */
        if ((rc = augds_node_yang2aug(diff, parent_path, &aug_path, &aug_value))) {
            goto cleanup;
        }

        /* get node operation */
        cur_op = augds_diff_get_op(diff);
        if (!cur_op) {
            cur_op = parent_op;
        }
        assert(cur_op);

        if (aug_path) {
            switch (cur_op) {
            case AUGDS_OP_CREATE:
            case AUGDS_OP_REPLACE:
                /* set the augeas data */
                if (aug_set(aug, aug_path, aug_value) == -1) {
                    AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
                }
                break;
            case AUGDS_OP_DELETE:
                /* remove the augeas data */
                if (aug_rm(aug, aug_path) == 0) {
                    AUG_LOG_ERRINT_GOTO(rc, cleanup);
                }

                /* all descendants were deleted, too */
                continue;
            case AUGDS_OP_NONE:
                /* nothing to do */
                break;
            case AUGDS_OP_UNKNOWN:
                AUG_LOG_ERRINT_RET;
            }
        } /* else no corresponding augeas data for the YANG node */

        /* process children recursively */
        if ((rc = augds_yang2aug_diff_siblings_r(aug, lyd_child_no_keys(diff), aug_path, cur_op))) {
            goto cleanup;
        }

        free(aug_path);
        aug_path = NULL;
    }

cleanup:
    free(aug_path);
    return rc;
}

static int srpds_aug_load(const struct lys_module *mod, sr_datastore_t ds, const char **xpaths, uint32_t xpath_count,
        struct lyd_node **mod_data);

static int
srpds_aug_store(const struct lys_module *mod, sr_datastore_t ds, const struct lyd_node *mod_data)
{
    int rc = SR_ERR_OK, i, label_count;
    struct lyd_node *cur_data = NULL, *diff = NULL;
    char *lens_name = NULL, *path = NULL, **label_matches = NULL;
    const char *value;

    /* get current data */
    if ((rc = srpds_aug_load(mod, ds, NULL, 0, &cur_data))) {
        goto cleanup;
    }

    /* get diff with the updated data */
    if (lyd_diff_siblings(cur_data, mod_data, 0, &diff)) {
        AUG_LOG_ERRLY_GOTO(mod->ctx, rc, cleanup);
    }

    /* set augeas context for relative paths */
    lens_name = augds_get_lens(mod);
    if (asprintf(&path, "/augeas/files//*[lens='@%s']/path", lens_name) == -1) {
        AUG_LOG_ERRMEM_GOTO(rc, cleanup);
    }
    label_count = aug_match(auginfo.aug, path, &label_matches);
    if (label_count == -1) {
        AUG_LOG_ERRAUG_GOTO(auginfo.aug, rc, cleanup);
    } else if (!label_count) {
        AUG_LOG_ERRINT_GOTO(rc, cleanup);
    }
    /* TODO what about the other files? */
    if (aug_get(auginfo.aug, label_matches[0], &value) != 1) {
        AUG_LOG_ERRAUG_GOTO(auginfo.aug, rc, cleanup);
    }
    if (aug_set(auginfo.aug, "/augeas/context", value) == -1) {
        AUG_LOG_ERRAUG_GOTO(auginfo.aug, rc, cleanup);
    }

    /* apply diff to augeas data */
    if ((rc = augds_yang2aug_diff_siblings_r(auginfo.aug, diff, NULL, 0))) {
        goto cleanup;
    }

    /* store new augeas data */
    if (aug_save(auginfo.aug) == -1) {
        AUG_LOG_ERRAUG_GOTO(auginfo.aug, rc, cleanup);
    }

cleanup:
    lyd_free_siblings(cur_data);
    lyd_free_siblings(diff);
    free(lens_name);
    free(path);
    for (i = 0; i < label_count; ++i) {
        free(label_matches[i]);
    }
    free(label_matches);
    return rc;
}

static void
srpds_aug_recover(const struct lys_module *mod, sr_datastore_t ds)
{
    /* TODO */
}

/**
 * @brief Create single YANG node and append to existing data.
 *
 * @param[in] schema Schema node of the data node.
 * @param[in] val_str String value of the data node.
 * @param[in] parent Optional parent of the data node.
 * @param[in,out] first First top-level sibling, is updated.
 * @param[out] node Created node.
 * @return SR error code.
 */
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
 * @param[in] parent YANG data current parent to append to, may be NULL.
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
            ext_node = augds_get_path_node(augnodes[i].ext_path, 0);

            /* handle all matching labels */
            j = 0;
            while (j < *label_count) {
                label = (*label_matches)[j];
                label_node = augds_get_path_node(label, 0);
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
                        /* no value */
                        goto label_used;
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
                        if (augds_ext_label_node_equal(augds_get_path_node(augnodes[k].ext_path, 0), label_node, NULL)) {
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

label_used:
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

/**
 * @brief Append converted augeas data to YANG data. Convert all data handled by a YANG module
 * using the context in the augeas handle.
 *
 * @param[in] aug Augeas handle.
 * @param[in] augnodes Array of augnodes to transform.
 * @param[in] augnode_count Count of @p augnodes.
 * @param[in] parent_label Augeas data parent label (absolute path).
 * @param[in] parent YANG data current parent to append to, may be NULL.
 * @param[in,out] first YANG data first top-level sibling.
 * @return SR error code.
 */
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
