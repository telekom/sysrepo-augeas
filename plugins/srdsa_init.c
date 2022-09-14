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
    uint32_t i, j;

    for (i = 0; i < augnode->case_count; ++i) {
        for (j = 0; j < augnode->cases[i].pattern_count; ++j) {
            free(augnode->cases[i].patterns[j].groups);
        }
        free(augnode->cases[i].patterns);
    }
    free(augnode->cases);

    for (i = 0; i < augnode->pattern_count; ++i) {
        free(augnode->patterns[i].groups);
    }
    free(augnode->patterns);

    for (i = 0; i < augnode->child_count; ++i) {
        augds_free_info_node(&augnode->child[i]);
    }
    free(augnode->child);
}

/**
 * @brief Add a new case to an array.
 *
 * @param[in] anode Augnode to modify.
 * @param[in] data_path Case data path to store.
 * @param[out] acase Added case.
 * @return SR error value.
 */
static int
augds_init_auginfo_add_case(struct augnode *anode, const char *data_path, struct augnode_case **acase)
{
    void *mem;

    mem = realloc(anode->cases, (anode->case_count + 1) * sizeof *anode->cases);
    if (!mem) {
        AUG_LOG_ERRMEM_RET;
    }
    anode->cases = mem;
    *acase = &anode->cases[anode->case_count];
    memset(*acase, 0, sizeof **acase);

    (*acase)->data_path = data_path;
    ++anode->case_count;
    return SR_ERR_OK;
}

/**
 * @brief Add a new pattern to an array.
 *
 * @param[in] pcode Compiled PCRE2 pattern code to store.
 * @param[in] inverted Whether the match is inverted or not.
 * @param[in,out] patterns Array of patterns to add to.
 * @param[in,out] pattern_count Count of @p patterns.
 * @return SR error value.
 */
static int
augds_init_auginfo_add_pattern(const pcre2_code *pcode, uint32_t inverted, struct augnode_pattern **patterns,
        uint32_t *pattern_count)
{
    void *mem;

    /* add pattern */
    mem = realloc(*patterns, (*pattern_count + 1) * sizeof **patterns);
    if (!mem) {
        AUG_LOG_ERRMEM_RET;
    }
    *patterns = mem;

    /* add one group */
    (*patterns)[*pattern_count].groups = malloc(sizeof *(*patterns)[*pattern_count].groups);
    if (!(*patterns)[*pattern_count].groups) {
        AUG_LOG_ERRMEM_RET;
    }

    (*patterns)[*pattern_count].groups[0].pcode = pcode;
    (*patterns)[*pattern_count].groups[0].inverted = inverted;

    (*patterns)[*pattern_count].group_count = 1;
    ++(*pattern_count);
    return SR_ERR_OK;
}

/**
 * @brief Add a new pattern to an array with multiple separate PCRE2 patterns.
 *
 * @param[in] ly_patterns Array of libyang compiled patterns.
 * @param[in,out] patterns Array of patterns to add to.
 * @param[in,out] pattern_count Count of @p patterns.
 * @return SR error value.
 */
static int
augds_init_auginfo_add_pattern2(struct lysc_pattern **ly_patterns, struct augnode_pattern **patterns,
        uint32_t *pattern_count)
{
    void *mem;
    LY_ARRAY_COUNT_TYPE u;

    mem = realloc(*patterns, (*pattern_count + 1) * sizeof **patterns);
    if (!mem) {
        AUG_LOG_ERRMEM_RET;
    }
    *patterns = mem;

    /* add all the patterns as separate groups */
    (*patterns)[*pattern_count].groups = calloc(LY_ARRAY_COUNT(ly_patterns), sizeof *(*patterns)[*pattern_count].groups);
    if (!(*patterns)[*pattern_count].groups) {
        AUG_LOG_ERRMEM_RET;
    }

    LY_ARRAY_FOR(ly_patterns, u) {
        (*patterns)[*pattern_count].groups[u].pcode = ly_patterns[u]->code;
        (*patterns)[*pattern_count].groups[u].inverted = ly_patterns[u]->inverted;
    }

    (*patterns)[*pattern_count].group_count = LY_ARRAY_COUNT(ly_patterns);
    ++(*pattern_count);
    return SR_ERR_OK;
}

/**
 * @brief Get pattern to match Augeas labels for this node.
 *
 * @param[in] auginfo Base auginfo structure with the compiled uint64 pattern cache.
 * @param[in] node YANG node with the pattern.
 * @param[in,out] patterns Array of patterns to add to.
 * @param[in,out] pattern_count Count of @p patterns.
 * @return SR error code.
 */
static int
augds_init_auginfo_get_pattern(struct auginfo *auginfo, const struct lysc_node *node, struct augnode_pattern **patterns,
        uint32_t *pattern_count)
{
    const struct lysc_type *type;
    const struct lysc_type_str *stype;
    const struct lysc_type_union *utype;
    const char *pattern;
    int err_code, rc;
    uint32_t compile_opts;
    PCRE2_SIZE err_offset;
    LY_ARRAY_COUNT_TYPE u;

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
        if ((rc = augds_init_auginfo_add_pattern2(stype->patterns, patterns, pattern_count))) {
            return rc;
        }
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
                return SR_ERR_INTERNAL;
            }
        }

        if ((rc = augds_init_auginfo_add_pattern(auginfo->pcode_uint64, 0, patterns, pattern_count))) {
            return rc;
        }
    } else if (type->basetype == LY_TYPE_UNION) {
        /* use patterns from all the string types */
        utype = (const struct lysc_type_union *)type;
        LY_ARRAY_FOR(utype->types, u) {
            type = utype->types[u];
            if (type->basetype == LY_TYPE_STRING) {
                stype = (const struct lysc_type_str *)type;
                if ((rc = augds_init_auginfo_add_pattern2(stype->patterns, patterns, pattern_count))) {
                    return rc;
                }
            } else {
                AUG_LOG_ERRINT_RET;
            }
        }
    } else {
        AUG_LOG_ERRINT_RET;
    }

    return SR_ERR_OK;
}

/**
 * @brief Init augnode of a schema node with case parent, which requires special metainformation
 * (data-path and pattern to match) to determine whether it will be created or not.
 *
 * @param[in] auginfo Base auginfo structure.
 * @param[in] node Schema node with the value or one of its descendants.
 * @param[in,out] anode Augnode to finish initializing.
 * @param[in,out] acase Current augnode case to add to.
 * @return SR error value.
 */
static int
augds_init_auginfo_case(struct auginfo *auginfo, const struct lysc_node *node, struct augnode *anode,
        struct augnode_case **acase)
{
    int rc = SR_ERR_OK;
    const struct lysc_node *iter;
    enum augds_ext_node_type node_type;
    const char *data_path, *value_path;

    assert(node);

    /* skip any keys */
    while (lysc_is_key(node)) {
        node = node->next;
    }

    /* get data path of the node, if any */
    augds_node_get_type(node, &node_type, &data_path, &value_path);

    if (data_path) {
        /* we have the node required to exist */
        if (!*acase) {
            if ((rc = augds_init_auginfo_add_case(anode, data_path, acase))) {
                return rc;
            }
        }

        if (node->nodetype == LYS_CONTAINER) {
            /* use the value-path node pattern */
            if (value_path) {
                iter = NULL;
                while ((iter = lys_getnext(iter, node, NULL, 0))) {
                    if (!strcmp(value_path, iter->name)) {
                        rc = augds_init_auginfo_get_pattern(auginfo, iter, &(*acase)->patterns, &(*acase)->pattern_count);
                        break;
                    }
                }
                assert(iter);
            }
        } else {
            /* use term node pattern of the value */
            assert((node->nodetype & LYD_NODE_TERM) && (node_type == AUGDS_EXT_NODE_VALUE));
            rc = augds_init_auginfo_get_pattern(auginfo, node, &(*acase)->patterns, &(*acase)->pattern_count);
        }

        return rc;
    }

    assert(node->nodetype & (LYS_LIST | LYS_CONTAINER | LYS_CHOICE));
    if (node->nodetype & (LYS_LIST | LYS_CONTAINER)) {
        /* go into an implicit list/container */
        rc = augds_init_auginfo_case(auginfo, lysc_node_child(node), anode, acase);
    } else if (node->nodetype == LYS_CHOICE) {
        /* use patterns of all the data in the nested choice, recursively */
        LY_LIST_FOR(lysc_node_child(node), iter) {
            if ((rc = augds_init_auginfo_case(auginfo, lysc_node_child(iter), anode, acase))) {
                break;
            }
        }
    }

    return rc;
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
    const struct lysc_node *node = NULL, *node2;
    enum augds_ext_node_type node_type;
    const char *data_path, *value_path;
    struct augnode *anode;
    struct augnode_case *acase;
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
            augds_init_auginfo_get_pattern(auginfo, node, &anode->patterns, &anode->pattern_count);
        } else if ((node_type == AUGDS_EXT_NODE_NONE) && node->parent && (node->parent->nodetype == LYS_CASE)) {
            /* special case handling to be able to properly load these data, there can be more suitable cases if
             * there is a nested choice */
            acase = NULL;
            augds_init_auginfo_case(auginfo, node, anode, &acase);
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
