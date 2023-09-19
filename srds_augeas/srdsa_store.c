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
 * @brief Transform string to diff operation.
 *
 * @param[in] str String to transform.
 * @return Diff operation.
 */
static enum augds_diff_op
augds_diff_str2op(const char *str)
{
    switch (str[0]) {
    case 'c':
        assert(!strcmp(str, "create"));
        return AUGDS_OP_INSERT;
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
 * @brief Check whether a YANG diff node carries the Augeas value relevant for a moved YANG user-ord list to be used
 * in Augeas data.
 *
 * @param[in] diff_node Diff node to examine.
 * @return Whether it is the relevant YANG node with Augeas value or not.
 */
static int
augds_diff_node_has_move_value(const struct lyd_node *diff_node)
{
    const struct lyd_node *parent, *child;

    if ((diff_node->schema->nodetype == LYS_LEAFLIST) && lysc_is_userordered(diff_node->schema)) {
        /* special move OP for user-ordered leaf-lists */
        return 1;
    }

    if (diff_node->schema->nodetype != LYS_LEAF) {
        /* must carry some value */
        return 0;
    }

    for (child = diff_node, parent = lyd_parent(diff_node); parent; child = parent, parent = lyd_parent(parent)) {
        /* check that the child is the first relevant schema child of the parent */
        if (child != lyd_child_no_keys(parent)) {
            break;
        }

        if (lysc_is_userordered(parent->schema)) {
            /* first DFS descendant with a value of a user-ordered list */
            return 1;
        }
    }

    return 0;
}

enum augds_diff_op
augds_diff_get_op(const struct lyd_node *diff_node, enum augds_diff_op parent_op)
{
    struct lyd_meta *meta;
    enum augds_diff_op op = 0;
    int inherited = 0;

    /* try to find our OP */
    LY_LIST_FOR(diff_node->meta, meta) {
        if (!strcmp(meta->name, "operation") && !strcmp(meta->annotation->module->name, "yang")) {
            op = augds_diff_str2op(lyd_get_meta_value(meta));
            break;
        }
    }

    if (!op) {
        /* inherit OP, but not move */
        if (parent_op == AUGDS_OP_MOVE) {
            op = AUGDS_OP_NONE;
        } else {
            op = parent_op;
        }
        inherited = 1;
    }

    if (inherited && (op == AUGDS_OP_REPLACE)) {
        if (augds_diff_node_has_move_value(diff_node)) {
            /* special move OP */
            op = AUGDS_OP_MOVE;
        } else if (diff_node->schema->nodetype == LYS_LEAF) {
            /* another leaf descendant of a user-ordered list that has no operation - it was not modified */
            op = AUGDS_OP_NONE;
        }
    }

    assert(op);
    return op;
}

/**
 * @brief Get Augeas value from a diff node.
 *
 * @param[in] diff_node Diff node to use.
 * @param[in] diff_data Data tree with @p diff_node change applied or not depending on the operation, needed to
 * correctly learn @p value.
 * @param[out] value Value associated with @p diff_node.
 * @param[out] diff_node2 Optional second YANG diff node if the value is not found in @p diff_node directly.
 * @return SR error code.
 */
static int
augds_store_get_value(const struct lyd_node *diff_node, const struct lyd_node *diff_data, const char **value,
        struct lyd_node **diff_node2)
{
    int rc = SR_ERR_OK;
    char *path = NULL;
    void *mem;
    const struct lysc_node *schild;
    struct lyd_node *child = NULL;
    size_t len;
    LY_ERR r;

    if (diff_node->schema->nodetype & (LYS_CONTAINER | LYS_LIST)) {
        /* try to find the node with value in diff, but it may be only in data */
        schild = lysc_node_child(diff_node->schema);

        if (lyd_child(diff_node) && (lyd_child(diff_node)->schema == schild)) {
            /* node is in diff */
            child = lyd_child(diff_node);
        } else {
            /* get container path */
            path = lyd_path(diff_node, LYD_PATH_STD, NULL, 0);
            if (!path) {
                AUG_LOG_ERRMEM_GOTO(rc, cleanup);
            }

            /* append first child name */
            len = strlen(path);
            mem = realloc(path, len + 1 + strlen(schild->name) + 1);
            if (!mem) {
                AUG_LOG_ERRMEM_GOTO(rc, cleanup);
            }
            path = mem;
            sprintf(path + len, "/%s", schild->name);

            /* get it from the diff data */
            r = lyd_find_path(diff_data, path, 0, &child);
            if (r == LY_EINCOMPLETE) {
                /* we do not care */
                child = NULL;
            } else if (r && (r != LY_ENOTFOUND)) {
                AUG_LOG_ERRLY_GOTO(LYD_CTX(diff_data), rc, cleanup);
            }
        }
        *value = augds_get_term_value(child);
    } else {
        /* just get the value of the term node */
        assert(diff_node->schema->nodetype & LYD_NODE_TERM);
        *value = augds_get_term_value(diff_node);
    }

    if (diff_node2) {
        *diff_node2 = child;
    }

cleanup:
    free(path);
    return rc;
}

/**
 * @brief Find node instance in another data tree.
 *
 * @param[in] node Node to find.
 * @param[in] data Data to search in.
 * @param[out] data_node Found node in @p data.
 * @return SR error code.
 */
static int
augds_store_find_inst(const struct lyd_node *node, const struct lyd_node *data, struct lyd_node **data_node)
{
    int rc = SR_ERR_OK;
    char *path = NULL;

    /* generate node path */
    path = lyd_path(node, LYD_PATH_STD, NULL, 0);
    if (!path) {
        AUG_LOG_ERRMEM_GOTO(rc, cleanup);
    }

    /* find it in the other data tree */
    if (lyd_find_path(data, path, 0, data_node)) {
        AUG_LOG_ERRLY_GOTO(LYD_CTX(data), rc, cleanup);
    }

cleanup:
    free(path);
    return rc;
}

/**
 * @brief Get Augeas label index from a node.
 *
 * @param[in] diff_node Diff node to use.
 * @param[in] aug_label Augeas label, if differs from @p diff_node name. Used to identify duplicate label instances.
 * @param[in] diff_data Data tree with @p diff_node change applied or not depending on the operation, needed to
 * correctly learn @p aug_index.
 * @param[out] aug_index Augeas label index associated with @p diff_node, none if 0.
 * @return SR error code.
 */
static int
augds_store_label_index(const struct lyd_node *diff_node, const char *aug_label, const struct lyd_node *diff_data,
        uint32_t *aug_index)
{
    int rc = SR_ERR_OK, len;
    const struct lysc_node_leaf *sleaf = NULL;
    struct lyd_node *data_node, *node;
    struct ly_set *set = NULL;
    uint32_t i;
    char *path = NULL;
    const char *dpath;
    enum augds_ext_node_type type;

    *aug_index = 0;

    assert(diff_node->schema->nodetype & (LYS_CONTAINER | LYS_LIST | LYD_NODE_TERM));
    assert((diff_node->schema->nodetype != LYS_CONTAINER) || !aug_label ||
            lysc_node_child(diff_node->schema)->flags & LYS_MAND_TRUE);

    if (diff_node->schema->nodetype == LYD_NODE_TERM) {
        sleaf = (struct lysc_node_leaf *)diff_node->schema;
    } else if ((diff_node->schema->nodetype & (LYS_CONTAINER | LYS_LIST)) && aug_label) {
        sleaf = (struct lysc_node_leaf *)lysc_node_child(diff_node->schema);
        assert(sleaf->nodetype == LYS_LEAF);
    }
    if (sleaf && (sleaf->type->basetype == LY_TYPE_UINT64)) {
        /* sequential Augeas type, has no index */
        goto cleanup;
    }

    /* get the node in data */
    if ((rc = augds_store_find_inst(diff_node, diff_data, &data_node))) {
        goto cleanup;
    }

    /* get path to all the relevant instances */
    if ((lyd_parent(data_node)->schema->nodetype == LYS_LIST) && !strcmp(LYD_NAME(lyd_first_sibling(data_node)), "_id")) {
        /* implicit lists have no data-path meaning they are not present in Augeas data so we must take all these
         * YANG data list instances into consideration */
        path = lyd_path(lyd_parent(data_node), LYD_PATH_STD_NO_LAST_PRED, NULL, 0);
        if (!path) {
            AUG_LOG_ERRMEM_GOTO(rc, cleanup);
        }

        /* append the last node */
        len = strlen(path);
        path = realloc(path, len + 1 + strlen(LYD_NAME(data_node)) + 1);
        if (!path) {
            AUG_LOG_ERRMEM_GOTO(rc, cleanup);
        }
        sprintf(path + len, "/%s", LYD_NAME(data_node));
    } else {
        /* assume the node has data-path */
        augds_node_get_type(data_node->schema, &type, &dpath, NULL);
        assert(dpath);
        path = lyd_path(data_node, LYD_PATH_STD_NO_LAST_PRED, NULL, 0);
        if (!path) {
            AUG_LOG_ERRMEM_GOTO(rc, cleanup);
        }
    }

    /* find all relevant instances of this schema node */
    if (lyd_find_xpath(diff_data, path, &set)) {
        AUG_LOG_ERRLY_GOTO(LYD_CTX(diff_data), rc, cleanup);
    }

    /* even if there are only succeeding instances, we need the index */
    *aug_index = 1;
    for (i = 0; i < set->count; ++i) {
        node = set->dnodes[i];
        if (data_node == node) {
            /* we have found all preceding instances */
            break;
        }

        if (aug_label) {
            /* check for different Augeas label */
            if ((node->schema->nodetype == LYS_CONTAINER) && lyd_child(node) && strcmp(lyd_get_value(lyd_child(node)), aug_label)) {
                continue;
            } else if ((node->schema->nodetype & LYD_NODE_TERM) && strcmp(lyd_get_value(node), aug_label)) {
                continue;
            }
        }

        ++(*aug_index);
    }

    if (i == set->count) {
        /* our instance not found */
        AUG_LOG_ERRINT_GOTO(rc, cleanup);
    }

cleanup:
    free(path);
    ly_set_free(set, NULL);
    return rc;
}

static int augds_store_path(const struct lyd_node *diff_node, const char *parent_aug_path, const char *data_path,
        enum augds_ext_node_type node_type, struct lyd_node *diff_data, char **aug_path);

/**
 * @brief Get Augeas path for a YANG diff node with recursive leafref reference.
 *
 * @param[in] diff_node Diff node.
 * @param[in] parent_aug_path Augeas path of the YANG data parent of @p diff_node.
 * @param[in] diff_data Pre-diff data tree.
 * @param[out] aug_path Augeas path to store.
 * @return SR error code.
 */
static int
augds_store_recursive_path(const struct lyd_node *diff_node, const char *parent_aug_path, struct lyd_node *diff_data,
        char **aug_path)
{
    int rc = SR_ERR_OK, len;
    const struct lysc_node *snode;
    enum augds_ext_node_type node_type;
    struct lyd_node *data_parent;
    const struct lyd_node *iter;
    const char *data_path;
    char path[512] = {0}, *start = &path[511], *parent_path = NULL, *cur_parent_path, *aug_path2;
    struct ly_set *set = NULL;

    /* find the leafref */
    LYSC_TREE_DFS_BEGIN(diff_node->schema, snode) {
        if ((snode->nodetype == LYS_LEAF) && (((struct lysc_node_leaf *)snode)->type->basetype == LY_TYPE_LEAFREF)) {
            break;
        }
        LYSC_TREE_DFS_END(diff_node->schema, snode);
    }
    /* it must be found and assume there is always only one leafref so we have the correct */
    assert(snode);

    /* build relative data path to the leafref */
    do {
        if (!(snode->nodetype & (LYS_CASE | LYS_CHOICE))) {
            if (start[0]) {
                /* slash */
                if (start == path) {
                    AUG_LOG_ERRINT_GOTO(rc, cleanup);
                }

                --start;
                start[0] = '/';
            }

            /* node name */
            len = strlen(snode->name);
            if (start - path < len) {
                AUG_LOG_ERRINT_GOTO(rc, cleanup);
            }

            start -= len;
            memcpy(start, snode->name, len);

        }

        snode = snode->parent;
    } while (snode != lyd_parent(diff_node)->schema);

    /* get the data parent to evaluate paths from */
    if ((rc = augds_store_find_inst(diff_node, diff_data, &data_parent))) {
        goto cleanup;
    }
    data_parent = lyd_parent(data_parent);

    iter = diff_node;
    cur_parent_path = (char *)parent_aug_path;
    while (1) {
        /* try to find a leafref referencing this instance */
        if (asprintf(&parent_path, "%s[.='%s']", start, lyd_get_value(lyd_child(iter))) == -1) {
            AUG_LOG_ERRMEM_GOTO(rc, cleanup);
        }
        if (lyd_find_xpath(data_parent, parent_path, &set)) {
            AUG_LOG_ERRLY_GOTO(LYD_CTX(diff_node), rc, cleanup);
        }
        if (!set->count) {
            /* no reference */
            goto cleanup;
        }
        assert(set->count == 1);

        /* generate path for the recursive node */
        for (iter = lyd_parent(set->dnodes[0]); iter->schema != diff_node->schema; iter = lyd_parent(iter)) {
            augds_node_get_type(iter->schema, &node_type, &data_path, NULL);
            if ((rc = augds_store_path(iter, cur_parent_path, data_path, node_type, diff_data, &aug_path2))) {
                goto cleanup;
            }

            if (aug_path2) {
                free(*aug_path);
                *aug_path = cur_parent_path = aug_path2;
            }
        }

        /* next iter */
        free(parent_path);
        parent_path = NULL;
    }

cleanup:
    free(parent_path);
    ly_set_free(set, NULL);
    return rc;
}

/**
 * @brief Get Augeas path for a YANG diff node.
 *
 * @param[in] diff_node Diff node.
 * @param[in] parent_aug_path Augeas path of the YANG data parent of @p diff_node.
 * @param[in] data_path Augeas data-path of @p diff_node.
 * @param[in] node_type Node type of @p diff_node.
 * @param[in,out] diff_data Pre-diff data tree, @p diff_node change is applied.
 * @param[out] aug_path Augeas path to store.
 * @return SR error code.
 */
static int
augds_store_path(const struct lyd_node *diff_node, const char *parent_aug_path, const char *data_path,
        enum augds_ext_node_type node_type, struct lyd_node *diff_data, char **aug_path)
{
    int rc = SR_ERR_OK;
    const char *label;
    char index_str[24];
    uint32_t aug_index;

    *aug_path = NULL;

    if (!diff_node) {
        /* there is no node so no path */
        goto cleanup;
    }

    /* get Augeas label with index */
    switch (node_type) {
    case AUGDS_EXT_NODE_VALUE:
        /* ext data path (YANG schema node name) as Augeas label */
        label = data_path;
        if ((rc = augds_store_label_index(diff_node, NULL, diff_data, &aug_index))) {
            goto cleanup;
        }
        break;
    case AUGDS_EXT_NODE_LABEL:
        /* YANG data value as Augeas label */
        if ((rc = augds_store_get_value(diff_node, diff_data, &label, NULL))) {
            goto cleanup;
        }
        if ((rc = augds_store_label_index(diff_node, label, diff_data, &aug_index))) {
            goto cleanup;
        }
        break;
    case AUGDS_EXT_NODE_REC_LIST:
        /* recursive list, append all parents to the path */
        rc = augds_store_recursive_path(diff_node, parent_aug_path, diff_data, aug_path);
        goto cleanup;
    case AUGDS_EXT_NODE_NONE:
    case AUGDS_EXT_NODE_REC_LREF:
        /* no path */
        goto cleanup;
    }

    /* finally generate Augeas path */
    if (aug_index) {
        sprintf(index_str, "[%" PRIu32 "]", aug_index);
    } else {
        strcpy(index_str, "");
    }
    if (asprintf(aug_path, "%s%s%s%s", parent_aug_path ? parent_aug_path : "", parent_aug_path ? "/" : "", label,
            index_str) == -1) {
        AUG_LOG_ERRMEM_GOTO(rc, cleanup);
    }

cleanup:
    return rc;
}

/**
 * @brief Get Augeas value for a YANG diff node.
 *
 * @param[in] diff_node Diff node.
 * @param[in] value_path Augeas value-yang-path extension value.
 * @param[in] node_type Node type of @p diff_node.
 * @param[in,out] diff_data Pre-diff data tree, @p diff_node change is applied.
 * @param[out] aug_value Augeas value to store.
 * @param[out] diff_node2 Second YANG diff node if both reference a single Augeas node (label/value).
 * @return SR error code.
 */
static int
augds_store_value(const struct lyd_node *diff_node, const char *value_path, enum augds_ext_node_type node_type,
        struct lyd_node *diff_data, const char **aug_value, struct lyd_node **diff_node2)
{
    int rc = SR_ERR_OK;

    *aug_value = NULL;
    *diff_node2 = NULL;

    if (!diff_node) {
        /* there is no node so no value */
        goto cleanup;
    }

    /* get Augeas value */
    if (value_path) {
        /* value is stored in a different YANG node (it may not exist if no value was set) */
        if (diff_node->schema->nodetype & LYD_NODE_INNER) {
            lyd_find_path(diff_node, value_path, 0, diff_node2);
        } else {
            lyd_find_path(lyd_parent(diff_node), value_path, 0, diff_node2);
        }
        *aug_value = augds_get_term_value(*diff_node2);
    } else if ((diff_node->schema->nodetype == LYS_LEAF) && (node_type != AUGDS_EXT_NODE_LABEL)) {
        /* get value from the YANG leaf node, but only if it is not the label */
        if ((rc = augds_store_get_value(diff_node, diff_data, aug_value, diff_node2))) {
            goto cleanup;
        }
    }

cleanup:
    return rc;
}

/**
 * @brief Check whether a node is a user-ordered list.
 *
 * @param[in] node Node to examine.
 * @return Whether it is a user-ord list or not.
 */
static int
augds_store_is_userord_list(const struct lyd_node *node)
{
    if (!node) {
        return 0;
    }

    if ((node->schema->nodetype == LYS_LIST) && lysc_is_userordered(node->schema)) {
        return 1;
    }
    return 0;
}

/**
 * @brief Get Augeas anchor for a diff node in YANG data.
 *
 * @param[in] diff_data_node Diff node from diff data.
 * @param[out] anchor YANG data anchor for Augeas operations, NULL if the only item.
 * @param[out] aug_before Whether the new Augeas label should be inserted before or after @p anchor.
 * @return SR error code.
 */
static int
augds_store_anchor(const struct lyd_node *diff_data_node, struct lyd_node **anchor, int *aug_before)
{
    enum augds_ext_node_type node_type;
    int anchor_child = 0;
    const char *key_name = "";

    assert(lyd_parent(diff_data_node));

    if (augds_store_is_userord_list(lyd_parent(diff_data_node))) {
        /* learn key name of the parent list */
        key_name = LYD_NAME(lyd_first_sibling(diff_data_node));
    }

    if (!strcmp(key_name, "_id") || !strcmp(key_name, "_r-id")) {
        /* nodes with data-paths are nested in the implicit user-ordered lists */
        diff_data_node = lyd_parent(diff_data_node);
        anchor_child = 1;
    } else {
        augds_node_get_type(diff_data_node->schema, &node_type, NULL, NULL);
        switch (node_type) {
        case AUGDS_EXT_NODE_VALUE:
        case AUGDS_EXT_NODE_LABEL:
            break;
        case AUGDS_EXT_NODE_NONE:
        case AUGDS_EXT_NODE_REC_LIST:
        case AUGDS_EXT_NODE_REC_LREF:
            /* some uninteresting implicit node, does not need an anchor */
            *anchor = NULL;
            *aug_before = 0;
            return SR_ERR_OK;
        }
    }

    if (diff_data_node->prev->next && (!anchor_child || augds_store_is_userord_list(diff_data_node->prev))) {
        /* previous instance */
        *anchor = anchor_child ? lyd_child_no_keys(diff_data_node->prev) : diff_data_node->prev;
        *aug_before = 0;

        augds_node_get_type((*anchor)->schema, &node_type, NULL, NULL);
        switch (node_type) {
        case AUGDS_EXT_NODE_VALUE:
        case AUGDS_EXT_NODE_LABEL:
            /* okay, we can use it as an anchor */
            return SR_ERR_OK;
        case AUGDS_EXT_NODE_NONE:
        case AUGDS_EXT_NODE_REC_LIST:
        case AUGDS_EXT_NODE_REC_LREF:
            /* use another anchor, there is no (suitable) preceding anchor */
            break;
        }
    }

    if (diff_data_node->next && (!anchor_child || augds_store_is_userord_list(diff_data_node->next))) {
        /* next instance */
        *anchor = anchor_child ? lyd_child_no_keys(diff_data_node->next) : diff_data_node->next;
        *aug_before = 1;

        /* check the anchor */
        augds_node_get_type((*anchor)->schema, &node_type, NULL, NULL);
        switch (node_type) {
        case AUGDS_EXT_NODE_VALUE:
        case AUGDS_EXT_NODE_LABEL:
            /* fine */
            break;
        case AUGDS_EXT_NODE_NONE:
        case AUGDS_EXT_NODE_REC_LIST:
        case AUGDS_EXT_NODE_REC_LREF:
            /* not suitable */
            *anchor = NULL;
            *aug_before = 0;
            break;
        }
    } else {
        /* the only instance */
        *anchor = NULL;
        *aug_before = 1;
    }

    return SR_ERR_OK;
}

/**
 * @brief Get the last label without index from an Augeas path.
 *
 * @param[in] aug_path Augeas path.
 * @param[out] aug_label Augeas label.
 * @return SR error code.
 */
static int
augds_store_diff_path_label(const char *aug_path, char **aug_label)
{
    char *ptr;

    /* get the last label */
    ptr = strrchr(aug_path, '/');
    if (ptr) {
        *aug_label = strdup(ptr + 1);
    } else {
        *aug_label = strdup(aug_path);
    }
    if (!*aug_label) {
        AUG_LOG_ERRMEM_RET;
    }

    /* remove index */
    ptr = strrchr(*aug_label, '[');
    if (ptr) {
        ptr[0] = '\0';
    }

    return SR_ERR_OK;
}

/**
 * @brief Generate the same path with one higher index.
 *
 * @param[in] aug_path Augeas path.
 * @param[out] aug_path2 New Augeas path.
 * @return SR error code.
 */
static int
augds_store_diff_path_next_idx(const char *aug_path, char **aug_path2)
{
    const char *ptr;
    char *p;
    uint32_t idx;

    /* find predicate start */
    ptr = strrchr(aug_path, '[');
    assert(ptr);
    ++ptr;

    /* get current index */
    idx = strtoul(ptr, &p, 10);
    assert(p[0] = ']');

    /* print new path */
    if (asprintf(aug_path2, "%.*s%" PRIu32 "]", (int)(ptr - aug_path), aug_path, idx + 1) == -1) {
        AUG_LOG_ERRMEM_RET;
    }
    return SR_ERR_OK;
}

/**
 * @brief Process path for it to be ready for use in Augeas API.
 *
 * @param[in] aug Augeas context.
 * @param[in,out] aug_path Path to process, is updated.
 * @param[out] path_d Dynamic path if @p aug_path had to be changed.
 * @return SR error code.
 */
static int
augds_store_diff_apply_prepare_path(augeas *aug, const char **aug_path, char **path_d)
{
    int rc = SR_ERR_OK, j = 0;
    const char *val;
    uint32_t i;

    *path_d = NULL;

    if (!*aug_path) {
        /* nothing to do */
        goto cleanup;
    }

    /* relative paths starting with numbers or '$' are not interpreted properly, use full absolute path instead */
    if (isdigit((*aug_path)[0]) || ((*aug_path)[0] == '$')) {
        if (aug_get(aug, "/augeas/context", &val) != 1) {
            AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
        }
        *path_d = malloc(strlen(val) + 1 + strlen(*aug_path) + 1);
        if (!*path_d) {
            AUG_LOG_ERRMEM_GOTO(rc, cleanup);
        }
        j = sprintf(*path_d, "%s/", val);
    }

    /* encode special characters */
    for (i = 0; (*aug_path)[i]; ++i) {
        switch ((*aug_path)[i]) {
        case ',':
            /* alloc memory */
            if (!j) {
                *path_d = malloc(strlen(*aug_path) + 2);
            } else {
                *path_d = realloc(*path_d, strlen(*path_d) + 2);
            }
            if (!*path_d) {
                AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
            }

            if (!j && i) {
                /* copy path start */
                memcpy(*path_d, *aug_path, i);
                j = i;
            }

            /* encode char */
            (*path_d)[j++] = '\\';
            (*path_d)[j++] = (*aug_path)[i];
            break;
        default:
            if (j) {
                /* copy char */
                (*path_d)[j++] = (*aug_path)[i];
            }
            break;
        }
    }
    if (*path_d) {
        (*path_d)[j] = '\0';
        *aug_path = *path_d;
    }

cleanup:
    return rc;
}

/**
 * @brief Apply single diff node on Augeas data.
 *
 * @param[in] aug Augeas context.
 * @param[in] op Operation to apply.
 * @param[in] aug_path Augeas path in the data.
 * @param[in] aug_path_anchor Augeas path of the anchor of @p aug_path.
 * @param[in] aug_before Augeas flag where to store @p aug_path relatively to @p aug_path_anchor.
 * @param[in] aug_value Augeas value in the data for @p aug_path.
 * @param[in] aug_moved_back Only for move @p op, whether the label was moved back or forward in Augeas data.
 * @param[out] applied_r Set if all the descendants were applied recursively, too.
 * @return SR error code.
 */
static int
augds_store_diff_apply(augeas *aug, enum augds_diff_op op, const char *aug_path, const char *aug_path_anchor,
        int aug_before, const char *aug_value, int aug_moved_back, int *applied_r)
{
    int rc = SR_ERR_OK;
    char *aug_label = NULL, *aug_path2 = NULL, *anchor_d = NULL, *path_d = NULL;

    if (applied_r) {
        *applied_r = 0;
    }

    if (!aug_path) {
        /* nothing to do */
        goto cleanup;
    }

    /* process paths */
    if ((rc = augds_store_diff_apply_prepare_path(aug, &aug_path, &path_d))) {
        goto cleanup;
    }
    if ((rc = augds_store_diff_apply_prepare_path(aug, &aug_path_anchor, &anchor_d))) {
        goto cleanup;
    }

    switch (op) {
    case AUGDS_OP_INSERT:
        if (aug_path_anchor) {
            /* get the label from the full path */
            if ((rc = augds_store_diff_path_label(aug_path, &aug_label))) {
                goto cleanup;
            }

            /* insert the label */
            if (aug_insert(aug, aug_path_anchor, aug_label, aug_before) == -1) {
                AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
            }
        } /* else is the only instance */

        /* set its value */
        if (aug_set(aug, aug_path, aug_value) == -1) {
            AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
        }
        break;
    case AUGDS_OP_REPLACE:
        /* set the augeas data */
        if (aug_set(aug, aug_path, aug_value) == -1) {
            AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
        }
        break;
    case AUGDS_OP_RENAME:
        /* remove the index as it is not needed and not interpreted as index */
        if ((rc = augds_store_diff_path_label(aug_path, &aug_label))) {
            goto cleanup;
        }

        /* rename labels in augeas data */
        if (aug_rename(aug, aug_path_anchor, aug_label) == -1) {
            AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
        }
        break;
    case AUGDS_OP_MOVE:
        /* get the label from the full path */
        if ((rc = augds_store_diff_path_label(aug_path, &aug_label))) {
            goto cleanup;
        }

        /* insert the label */
        if (aug_insert(aug, aug_path_anchor, aug_label, aug_before) == -1) {
            AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
        }

        /* generate the path for the other label */
        if ((rc = augds_store_diff_path_next_idx(aug_path, &aug_path2))) {
            goto cleanup;
        }

        /* replace the created path with descendants by the previous one */
        if (aug_mv(aug, aug_moved_back ? aug_path2 : aug_path, aug_moved_back ? aug_path : aug_path2) == -1) {
            AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
        }
        break;
    case AUGDS_OP_DELETE:
        /* remove the augeas data */
        if (aug_rm(aug, aug_path) == 0) {
            AUG_LOG_ERRINT_GOTO(rc, cleanup);
        }

        /* all descendants were deleted, too */
        if (applied_r) {
            *applied_r = 1;
        }
        break;
    case AUGDS_OP_NONE:
        /* nothing to do */
        break;
    case AUGDS_OP_UNKNOWN:
        AUG_LOG_ERRINT_RET;
    }

cleanup:
    free(path_d);
    free(anchor_d);
    free(aug_label);
    free(aug_path2);
    return rc;
}

/**
 * @brief Find anchor data node for a diff node.
 *
 * @param[in] diff_node Diff node.
 * @param[in] data_sibling Sibling of @p diff_node in diff data.
 * @param[out] data_anchor YANG anchor in YANG data, NULL if none.
 * @param[out] before Relative position of node to anchor.
 * @return SR error code.
 */
static int
augds_store_find_anchor(const struct lyd_node *diff_node, const struct lyd_node *data_sibling,
        struct lyd_node **data_anchor, int *before)
{
    int rc = SR_ERR_OK;
    struct lyd_meta *meta;
    const char *meta_val;

    assert(diff_node->schema == data_sibling->schema);

    /* learn the previous instance key/value */
    if (diff_node->schema->nodetype == LYS_LIST) {
        meta = lyd_find_meta(diff_node->meta, NULL, "yang:key");
    } else {
        meta = lyd_find_meta(diff_node->meta, NULL, "yang:value");
    }
    if (!meta) {
        /* parent was created with all these nested user-ord list instances, they are in the correct order */
        if (data_sibling->prev->next && (data_sibling->prev->schema == diff_node->schema)) {
            /* preceding instance */
            *data_anchor = data_sibling->prev;
            *before = 0;
        } else if (data_sibling->next && (data_sibling->next->schema == diff_node->schema)) {
            /* following instance */
            *data_anchor = data_sibling->next;
            *before = 1;
        } else {
            /* the only instance */
            *data_anchor = NULL;
            *before = 0;
        }
        goto cleanup;
    }

    /* find the anchor */
    meta_val = lyd_get_meta_value(meta);
    if (!strlen(meta_val)) {
        /* first instance */
        assert(lyd_parent(data_sibling));
        meta_val = NULL;
        *before = 1;
    } else {
        *before = 0;
    }
    if (lyd_find_sibling_val(data_sibling, diff_node->schema, meta_val, 0, data_anchor)) {
        AUG_LOG_ERRLY_GOTO(LYD_CTX(data_sibling), rc, cleanup);
    }

cleanup:
    return rc;
}

/**
 * @brief Learn the direction of a move operation.
 *
 * @param[in] diff_data_node Diff node in diff data.
 * @param[in] anchor New anchor of @p diff_data_node in diff data.
 * @param[out] aug_moved_back Whether the move direction is back or forward.
 * @return SR error code.
 */
static int
augds_store_move_direction(const struct lyd_node *diff_data_node, const struct lyd_node *anchor, int *aug_moved_back)
{
    uint32_t new_id, anchor_id;
    char *ptr;

    assert(lyd_parent(diff_data_node) && lysc_is_userordered(lyd_parent(diff_data_node)->schema));

    /* learn indices of the parent user-ord list keys */
    new_id = strtoul(lyd_get_value(lyd_child(lyd_parent(diff_data_node))), &ptr, 10);
    assert(!ptr[0]);
    anchor_id = strtoul(lyd_get_value(lyd_child(lyd_parent(anchor))), &ptr, 10);
    assert(!ptr[0]);

    /* decide direction based on the indices, really naive check but should work for most if not all move operations
     * based on the libyang diff user-ord algorithm and the generated ascending indices */
    assert(new_id != anchor_id);
    if (new_id > anchor_id) {
        *aug_moved_back = 1;
    } else {
        *aug_moved_back = 0;
    }
    return SR_ERR_OK;
}

/**
 * @brief Update diff data by applying the single diff change.
 *
 * @param[in] diff_node Diff node to apply.
 * @param[in] op Operation of @p diff_node.
 * @param[in,out] diff_data Diff data, are updated.
 * @param[out] diff_data_node Optional node from @p diff_data that @p diff_node was applied on (not applicable for deletion).
 * @return SR error code.
 */
static int
augds_store_diff_data_update(const struct lyd_node *diff_node, enum augds_diff_op op, struct lyd_node *diff_data,
        struct lyd_node **diff_data_node)
{
    int rc = SR_ERR_OK, before;
    struct lyd_node *data_node = NULL, *data_parent, *anchor;
    char *path = NULL;

    assert(!diff_data_node || (op == AUGDS_OP_INSERT));

    switch (op) {
    case AUGDS_OP_INSERT:
        /* find our parent, cannot be top-level */
        assert(lyd_parent(diff_node));
        if ((rc = augds_store_find_inst(lyd_parent(diff_node), diff_data, &data_parent))) {
            goto cleanup;
        }

        /* duplicate the tree and append it to diff_data directly */
        if (lyd_dup_single(diff_node, (struct lyd_node_inner *)data_parent, LYD_DUP_NO_META, &data_node)) {
            AUG_LOG_ERRLY_GOTO(LYD_CTX(diff_node), rc, cleanup);
        }

        /* the operations are for Augeas data, for YANG data we need to re-learn them properly */
        if (lysc_is_userordered(diff_node->schema)) {
            /* find anchor */
            if ((rc = augds_store_find_anchor(diff_node, data_node, &anchor, &before))) {
                goto cleanup;
            }

            /* may already be at the right place */
            if (anchor && (anchor != data_node)) {
                /* move the instance */
                if (before) {
                    if (lyd_insert_before(anchor, data_node)) {
                        AUG_LOG_ERRLY_GOTO(LYD_CTX(data_node), rc, cleanup);
                    }
                } else {
                    if (lyd_insert_after(anchor, data_node)) {
                        AUG_LOG_ERRLY_GOTO(LYD_CTX(data_node), rc, cleanup);
                    }
                }
            }
        }
        break;
    case AUGDS_OP_DELETE:
        /* find the node in diff_data */
        if ((rc = augds_store_find_inst(diff_node, diff_data, &data_node))) {
            goto cleanup;
        }

        /* remove the node (tree) */
        lyd_free_tree(data_node);
        data_node = NULL;
        break;
    case AUGDS_OP_REPLACE:
        if (diff_node->schema->nodetype == LYS_CONTAINER) {
            /* inherited from parent user-ord list, ignore */
            break;
        }
    /* fallthrough */
    case AUGDS_OP_RENAME:
    case AUGDS_OP_MOVE:
        /* find the node in diff_data */
        if ((rc = augds_store_find_inst(diff_node, diff_data, &data_node))) {
            goto cleanup;
        }

        if (lysc_is_userordered(diff_node->schema)) {
            /* find anchor */
            if ((rc = augds_store_find_anchor(diff_node, data_node, &anchor, &before))) {
                goto cleanup;
            }

            /* may already be at the right place */
            if (anchor && (anchor != data_node)) {
                /* move the instance */
                if (before) {
                    if (lyd_insert_before(anchor, data_node)) {
                        AUG_LOG_ERRLY_GOTO(LYD_CTX(data_node), rc, cleanup);
                    }
                } else {
                    if (lyd_insert_after(anchor, data_node)) {
                        AUG_LOG_ERRLY_GOTO(LYD_CTX(data_node), rc, cleanup);
                    }
                }
            }
        } else {
            /* update the value */
            if (lyd_change_term_canon(data_node, lyd_get_value(diff_node))) {
                AUG_LOG_ERRLY_GOTO(LYD_CTX(diff_data), rc, cleanup);
            }
        }
        break;
    case AUGDS_OP_NONE:
        /* nothing to do, just find the node if necessary */
        if (diff_data_node) {
            if ((rc = augds_store_find_inst(diff_node, diff_data, &data_node))) {
                goto cleanup;
            }
        }
        break;
    case AUGDS_OP_UNKNOWN:
        AUG_LOG_ERRINT_GOTO(rc, cleanup);
    }

cleanup:
    free(path);
    if (!rc && diff_data_node) {
        *diff_data_node = data_node;
    }
    return rc;
}

/**
 * @brief Generate Augeas path for an anchor. Handle situation when there are changes in YANG data that
 * are yet to be performed in Augeas data that would result in moved index.
 *
 * @param[in] anchor YANG data anchor.
 * @param[in] aug_before Whether the anchor is before the YANG data node.
 * @param[in] parent_path Augeas path of the YANG data diff parent.
 * @param[in] diff_data After-diff data tree.
 * @param[in] aug_path Augeas path of the YANG data diff node.
 * @param[out] aug_anchor_path Generated path of the Augeas data anchor.
 * @return SR error code.
 */
static int
augds_store_diff_insert_anchor_path(const struct lyd_node *anchor, int aug_before, const char *parent_path,
        struct lyd_node *diff_data, const char *aug_path, char **aug_anchor_path)
{
    int rc = SR_ERR_OK;
    char *label1 = NULL, *label2 = NULL;
    const char *dpath;
    enum augds_ext_node_type type;

    augds_node_get_type(anchor->schema, &type, &dpath, NULL);
    if ((rc = augds_store_path(anchor, parent_path, dpath, type, diff_data, aug_anchor_path))) {
        goto cleanup;
    }

    if (aug_before) {
        /* the anchor is before the new node so its index may be wrong */
        if ((rc = augds_store_diff_path_label(aug_path, &label1))) {
            goto cleanup;
        }
        if ((rc = augds_store_diff_path_label(*aug_anchor_path, &label2))) {
            goto cleanup;
        }
        if (!strcmp(label1, label2)) {
            /* labels are the same meaning the generated index is one higher than it should */
            free(*aug_anchor_path);
            *aug_anchor_path = strdup(aug_path);
            if (!*aug_anchor_path) {
                AUG_LOG_ERRMEM_GOTO(rc, cleanup);
            }
        }
    }

cleanup:
    free(label1);
    free(label2);
    return rc;
}

int
augds_store_diff_r(augeas *aug, const struct lyd_node *diff_node, const char *parent_path, enum augds_diff_op parent_op,
        struct lyd_node *diff_data)
{
    int rc = SR_ERR_OK, applied_r = 0, aug_before = 0, aug_moved_back = 0, mand_child = 0;
    enum augds_diff_op cur_op, cur_op2;
    enum augds_ext_node_type node_type, type2;
    char *aug_path = NULL, *aug_anchor_path = NULL, *path = NULL;
    const char *aug_value, *data_path, *value_path, *dpath2;
    struct lyd_node *diff_data_node, *anchor, *diff_node2;
    const struct lyd_node *diff_path_node, *diff_node_child;
    const struct lysc_node *schild;

    /* get node operation and learn about the node */
    cur_op = augds_diff_get_op(diff_node, parent_op);
    augds_node_get_type(diff_node->schema, &node_type, &data_path, &value_path);

    if ((cur_op != AUGDS_OP_DELETE) && (cur_op != AUGDS_OP_MOVE) && (diff_node->schema->nodetype == LYS_CONTAINER)) {
        schild = lysc_node_child(diff_node->schema);
        augds_node_get_type(schild, &type2, &dpath2, NULL);
        if (!dpath2 && (schild->nodetype == LYS_LEAF) && (lyd_child(diff_node)->schema == schild)) {
            /* postpone applying this op until the child is being processed */
            mand_child = 1;
        }
    }

    if ((diff_node->schema->nodetype == LYS_LEAF) && !data_path && (lyd_parent(diff_node)->schema->nodetype == LYS_CONTAINER) &&
            (lysc_node_child(lyd_parent(diff_node)->schema) == diff_node->schema)) {
        /* this is the mandatory child leaf checked before, use the parent container for Augeas path */
        diff_path_node = lyd_parent(diff_node);
        augds_node_get_type(diff_path_node->schema, &node_type, &data_path, &value_path);

        if (cur_op == AUGDS_OP_REPLACE) {
            /* check for special case of Augeas label leaf changing value, which results in rename Augeas op */
            augds_node_get_type(diff_path_node->schema, &type2, NULL, NULL);
            if (type2 == AUGDS_EXT_NODE_LABEL) {
                cur_op = AUGDS_OP_RENAME;
            }
        }
    } else {
        if ((diff_node->schema->nodetype == LYS_LEAF) && (node_type == AUGDS_EXT_NODE_LABEL) && (cur_op == AUGDS_OP_REPLACE)) {
            /* special leaf that stores only the label, without value, so the label has been renamed */
            assert(!strcmp(data_path, "$$"));
            cur_op = AUGDS_OP_RENAME;
        }

        /* just use the node for the path */
        diff_path_node = diff_node;
    }

    switch (cur_op) {
    case AUGDS_OP_REPLACE:
    case AUGDS_OP_NONE:
        /* update diff data by applying this diff BEFORE Augeas path (index) is generated */
        if ((rc = augds_store_diff_data_update(diff_node, cur_op, diff_data, NULL))) {
            goto cleanup;
        }

        /* generate Augeas path and value for the diff node */
        if ((rc = augds_store_path(diff_path_node, parent_path, data_path, node_type, diff_data, &aug_path))) {
            goto cleanup;
        }
        if ((rc = augds_store_value(diff_path_node, value_path, node_type, diff_data, &aug_value, &diff_node2))) {
            goto cleanup;
        }
        break;
    case AUGDS_OP_INSERT:
        /* update diff data by applying this diff BEFORE Augeas path (index) is generated */
        if ((rc = augds_store_diff_data_update(diff_node, cur_op, diff_data, &diff_data_node))) {
            goto cleanup;
        }
        if (diff_node != diff_path_node) {
            diff_data_node = lyd_parent(diff_data_node);
        }

        /* generate Augeas path and value for the diff node */
        if ((rc = augds_store_path(diff_path_node, parent_path, data_path, node_type, diff_data, &aug_path))) {
            goto cleanup;
        }
        if ((rc = augds_store_value(diff_path_node, value_path, node_type, diff_data, &aug_value, &diff_node2))) {
            goto cleanup;
        }

        /* creating data where the order matters, find the anchor */
        if ((rc = augds_store_anchor(diff_data_node, &anchor, &aug_before))) {
            goto cleanup;
        }

        if (anchor) {
            /* generate Augeas path for the anchor */
            if ((rc = augds_store_diff_insert_anchor_path(anchor, aug_before, parent_path, diff_data, aug_path,
                    &aug_anchor_path))) {
                goto cleanup;
            }
        }
        break;
    case AUGDS_OP_DELETE:
        /* generate Augeas path and value for the diff node */
        if ((rc = augds_store_path(diff_path_node, parent_path, data_path, node_type, diff_data, &aug_path))) {
            goto cleanup;
        }
        if ((rc = augds_store_value(diff_path_node, value_path, node_type, diff_data, &aug_value, &diff_node2))) {
            goto cleanup;
        }
        break;
    case AUGDS_OP_RENAME:
        /* find the diff node in data with the previous value */
        if ((rc = augds_store_find_inst(diff_path_node, diff_data, &diff_data_node))) {
            goto cleanup;
        }

        /* generate Augeas path for the diff node with the previous value */
        augds_node_get_type(diff_data_node->schema, &type2, &dpath2, NULL);
        if ((rc = augds_store_path(diff_data_node, parent_path, dpath2, type2, diff_data, &aug_anchor_path))) {
            goto cleanup;
        }

        /* update diff data by applying this diff BEFORE Augeas path (index) is generated */
        if ((rc = augds_store_diff_data_update(diff_node, cur_op, diff_data, NULL))) {
            goto cleanup;
        }

        /* generate Augeas path and value for the diff node */
        if ((rc = augds_store_path(diff_path_node, parent_path, data_path, node_type, diff_data, &aug_path))) {
            goto cleanup;
        }
        if ((rc = augds_store_value(diff_path_node, value_path, node_type, diff_data, &aug_value, &diff_node2))) {
            goto cleanup;
        }
        break;
    case AUGDS_OP_MOVE:
        /* parent node is the user-ord list, was already applied (moved) in YANG data */

        /* generate Augeas path and value for the diff node */
        if ((rc = augds_store_path(diff_path_node, parent_path, data_path, node_type, diff_data, &aug_path))) {
            goto cleanup;
        }
        if ((rc = augds_store_value(diff_path_node, value_path, node_type, diff_data, &aug_value, &diff_node2))) {
            goto cleanup;
        }

        /* find the diff node in data */
        if ((rc = augds_store_find_inst(diff_path_node, diff_data, &diff_data_node))) {
            goto cleanup;
        }

        /* creating data where the order matters, find the anchor */
        if ((rc = augds_store_anchor(diff_data_node, &anchor, &aug_before))) {
            goto cleanup;
        }
        assert(anchor);

        /* generate Augeas path for the anchor */
        if ((rc = augds_store_diff_insert_anchor_path(anchor, aug_before, parent_path, diff_data, aug_path,
                &aug_anchor_path))) {
            goto cleanup;
        }

        /* learn the direction of the move */
        if ((rc = augds_store_move_direction(diff_data_node, anchor, &aug_moved_back))) {
            goto cleanup;
        }
        break;
    case AUGDS_OP_UNKNOWN:
        AUG_LOG_ERRINT_GOTO(rc, cleanup);
    }

    if (!mand_child) {
        /* apply */
        if ((rc = augds_store_diff_apply(aug, cur_op, aug_path, aug_anchor_path, aug_before, aug_value,
                aug_moved_back, &applied_r))) {
            goto cleanup;
        }

        if (diff_node2) {
            /* process the other value-yang-path node, too */
            cur_op2 = augds_diff_get_op(diff_node2, parent_op);
            if (cur_op2 && (cur_op2 != cur_op)) {
                /* different operation must be applied */
                switch (cur_op2) {
                case AUGDS_OP_INSERT:
                    /* inserting Augeas value simply means setting it */
                    cur_op2 = AUGDS_OP_REPLACE;
                    break;
                case AUGDS_OP_REPLACE:
                case AUGDS_OP_NONE:
                    /* operation is fine */
                    break;
                case AUGDS_OP_DELETE:
                    /* deleting YANG node representing Augeas value means setting Augeas value to NULL */
                    cur_op2 = AUGDS_OP_REPLACE;
                    aug_value = NULL;
                    break;
                case AUGDS_OP_RENAME:
                case AUGDS_OP_MOVE:
                case AUGDS_OP_UNKNOWN:
                    AUG_LOG_ERRINT_GOTO(rc, cleanup);
                }

                /* apply #2 */
                if ((rc = augds_store_diff_apply(aug, cur_op2, aug_path, aug_anchor_path, aug_before, aug_value,
                        aug_moved_back, NULL))) {
                    goto cleanup;
                }
            }
        }

        /* process all children normally */
        diff_node_child = lyd_child_no_keys(diff_node);
    } else {
        /* do not apply this container but the child instead */
        diff_node_child = lyd_child_no_keys(diff_node);
        if ((rc = augds_store_diff_r(aug, diff_node_child, parent_path, parent_op, diff_data))) {
            goto cleanup;
        }

        /* process all following children normally */
        diff_node_child = diff_node_child->next;
    }

    if ((cur_op == AUGDS_OP_REPLACE) && lysc_is_userordered(diff_node->schema)) {
        /* move all the rescendants that are not part of the diff */
        path = lyd_path(diff_node, LYD_PATH_STD, NULL, 0);
        lyd_find_path(diff_data, path, 0, &anchor);
        assert(anchor);
        LY_LIST_FOR(lyd_child_no_keys(anchor), anchor) {
            if ((rc = augds_store_diff_r(aug, anchor, aug_path ? aug_path : parent_path, cur_op, diff_data))) {
                goto cleanup;
            }
        }
    }

    if (!applied_r) {
        /* process children recursively */
        LY_LIST_FOR(diff_node_child, diff_node_child) {
            if ((rc = augds_store_diff_r(aug, diff_node_child, aug_path ? aug_path : parent_path, cur_op, diff_data))) {
                goto cleanup;
            }
        }
    }

    if (cur_op == AUGDS_OP_DELETE) {
        /* update diff data by applying this diff AFTER Augeas path (index) is generated and children processed */
        if ((rc = augds_store_diff_data_update(diff_node, cur_op, diff_data, NULL))) {
            goto cleanup;
        }
    }

cleanup:
    free(aug_path);
    free(aug_anchor_path);
    free(path);
    return rc;
}
