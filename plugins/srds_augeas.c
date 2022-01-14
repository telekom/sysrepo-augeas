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
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <augeas.h>
#include <libyang/libyang.h>

#define srpds_name "augeas DS"  /**< plugin name */

#define AUG_FILE_BACKUP_SUFFIX ".augsave"

#define AUG_LOG_ERRINT SRPLG_LOG_ERR(srpds_name, "Internal error (%s:%d).", __FILE__, __LINE__)
#define AUG_LOG_ERRMEM SRPLG_LOG_ERR(srpds_name, "Memory allocation failed (%s:%d).", __FILE__, __LINE__)

#define AUG_LOG_ERRINT_RET AUG_LOG_ERRINT;return SR_ERR_INTERNAL
#define AUG_LOG_ERRINT_GOTO(rc, label) AUG_LOG_ERRINT;rc = SR_ERR_INTERNAL; goto label
#define AUG_LOG_ERRMEM_RET AUG_LOG_ERRMEM;return SR_ERR_NO_MEMORY
#define AUG_LOG_ERRMEM_GOTO(rc, label) AUG_LOG_ERRMEM;rc = SR_ERR_NO_MEMORY; goto label
#define AUG_LOG_ERRAUG_GOTO(augeas, rc, label) rc = augds_check_erraug(augeas);assert(rc);goto label
#define AUG_LOG_ERRLY_GOTO(ctx, rc, label) augds_log_errly(ctx);rc = SR_ERR_LY;goto label

enum augds_ext_node_type {
    AUGDS_EXT_NODE_VALUE,           /**< matches specific augeas node value */
    AUGDS_EXT_NODE_POSITION,        /**< matches specific augeas node position, starts with '##' */
    AUGDS_EXT_NODE_LABEL            /**< matches any augeas node with value being the label, is string '$$' */
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

    struct augmod {
        const struct lys_module *mod;       /**< libyang module */
        struct augnode {
            const char *data_path;          /**< data-path of the augeas-extension in the schema node */
            const char *value_path;         /**< value-yang-path of the augeas-extension in the schema node */
            const struct lysc_node *schema; /**< schema node */
            const struct lysc_node *schema2;    /**< optional second node if the data-path references 2 YANG nodes */
            struct augnode *child;          /**< array of children of this node */
            uint32_t child_count;           /**< number of children */
        } *toplevel;    /**< array of top-level nodes */
        uint32_t toplevel_count;    /**< top-level node count */
    } *mods;            /**< array of all loaded libyang/augeas modules */
    uint32_t mod_count; /**< module count */
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
 * @brief Get UID of a user or vice versa.
 *
 * @param[in,out] uid UID to search for or the found UID.
 * @param[in,out] user User to search for or the found user.
 * @return SR error code.
 */
static int
augds_get_pwd(uid_t *uid, char **user)
{
    int rc = SR_ERR_OK, r;
    struct passwd pwd, *pwd_p;
    char *buf = NULL, *mem;
    ssize_t buflen = 0;

    assert(uid && user);

    do {
        if (!buflen) {
            /* learn suitable buffer size */
            buflen = sysconf(_SC_GETPW_R_SIZE_MAX);
            if (buflen == -1) {
                buflen = 2048;
            }
        } else {
            /* enlarge buffer */
            buflen += 2048;
        }

        /* allocate some buffer */
        mem = realloc(buf, buflen);
        if (!mem) {
            SRPLG_LOG_ERR(srpds_name, "Memory allocation failed.");
            rc = SR_ERR_NO_MEMORY;
            goto cleanup;
        }
        buf = mem;

        if (*user) {
            /* user -> UID */
            r = getpwnam_r(*user, &pwd, buf, buflen, &pwd_p);
        } else {
            /* UID -> user */
            r = getpwuid_r(*uid, &pwd, buf, buflen, &pwd_p);
        }
    } while (r && (r == ERANGE));
    if (r) {
        if (*user) {
            SRPLG_LOG_ERR(srpds_name, "Retrieving user \"%s\" passwd entry failed (%s).", *user, strerror(r));
        } else {
            SRPLG_LOG_ERR(srpds_name, "Retrieving UID \"%lu\" passwd entry failed (%s).", (unsigned long int)*uid, strerror(r));
        }
        rc = SR_ERR_INTERNAL;
        goto cleanup;
    } else if (!pwd_p) {
        if (*user) {
            SRPLG_LOG_ERR(srpds_name, "Retrieving user \"%s\" passwd entry failed (No such user).", *user);
        } else {
            SRPLG_LOG_ERR(srpds_name, "Retrieving UID \"%lu\" passwd entry failed (No such UID).", (unsigned long int)*uid);
        }
        rc = SR_ERR_NOT_FOUND;
        goto cleanup;
    }

    if (*user) {
        /* assign UID */
        *uid = pwd.pw_uid;
    } else {
        /* assign user */
        *user = strdup(pwd.pw_name);
        if (!*user) {
            SRPLG_LOG_ERR(srpds_name, "Memory allocation failed.");
            rc = SR_ERR_NO_MEMORY;
            goto cleanup;
        }
    }

    /* success */

cleanup:
    free(buf);
    return rc;
}

/**
 * @brief Get GID of a group or vice versa.
 *
 * @param[in,out] gid GID to search for or the found GID.
 * @param[in,out] group Group to search for or the found group.
 * @return SR error code.
 */
static int
augds_get_grp(gid_t *gid, char **group)
{
    int rc = SR_ERR_OK, r;
    struct group grp, *grp_p;
    char *buf = NULL, *mem;
    ssize_t buflen = 0;

    assert(gid && group);

    do {
        if (!buflen) {
            /* learn suitable buffer size */
            buflen = sysconf(_SC_GETGR_R_SIZE_MAX);
            if (buflen == -1) {
                buflen = 2048;
            }
        } else {
            /* enlarge buffer */
            buflen += 2048;
        }

        /* allocate some buffer */
        mem = realloc(buf, buflen);
        if (!mem) {
            SRPLG_LOG_ERR(srpds_name, "Memory allocation failed.");
            rc = SR_ERR_NO_MEMORY;
            goto cleanup;
        }
        buf = mem;

        if (*group) {
            /* group -> GID */
            r = getgrnam_r(*group, &grp, buf, buflen, &grp_p);
        } else {
            /* GID -> group */
            r = getgrgid_r(*gid, &grp, buf, buflen, &grp_p);
        }
    } while (r && (r == ERANGE));
    if (r) {
        if (*group) {
            SRPLG_LOG_ERR(srpds_name, "Retrieving group \"%s\" grp entry failed (%s).", *group, strerror(r));
        } else {
            SRPLG_LOG_ERR(srpds_name, "Retrieving GID \"%lu\" grp entry failed (%s).", (unsigned long int)*gid, strerror(r));
        }
        rc = SR_ERR_INTERNAL;
        goto cleanup;
    } else if (!grp_p) {
        if (*group) {
            SRPLG_LOG_ERR(srpds_name, "Retrieving group \"%s\" grp entry failed (No such group).", *group);
        } else {
            SRPLG_LOG_ERR(srpds_name, "Retrieving GID \"%lu\" grp entry failed (No such GID).", (unsigned long int)*gid);
        }
        rc = SR_ERR_NOT_FOUND;
        goto cleanup;
    }

    if (*group) {
        /* assign GID */
        *gid = grp.gr_gid;
    } else {
        /* assign group */
        *group = strdup(grp.gr_name);
        if (!*group) {
            SRPLG_LOG_ERR(srpds_name, "Memory allocation failed.");
            rc = SR_ERR_NO_MEMORY;
            goto cleanup;
        }
    }

cleanup:
    free(buf);
    return rc;
}

/**
 * @brief Change owner/permissions of a file.
 *
 * @param[in] path Path to the file.
 * @param[in] owner Owner to set, keep previous if not set.
 * @param[in] group Group to set, keep previous if not set.
 * @param[in] perm Permissions to set, keep previous if 0.
 * @return SR error code.
 */
static int
augds_chmodown(const char *path, const char *owner, const char *group, mode_t perm)
{
    int rc;
    uid_t uid = -1;
    gid_t gid = -1;

    assert(path);

    if (perm) {
        if (perm > 00777) {
            SRPLG_LOG_ERR(srpds_name, "Invalid permissions 0%.3o.", perm);
            return SR_ERR_INVAL_ARG;
        } else if (perm & 00111) {
            SRPLG_LOG_ERR(srpds_name, "Setting execute permissions has no effect.");
            return SR_ERR_INVAL_ARG;
        }
    }

    /* we are going to change the owner */
    if (owner && (rc = augds_get_pwd(&uid, (char **)&owner))) {
        return rc;
    }

    /* we are going to change the group */
    if (group && (rc = augds_get_grp(&gid, (char **)&group))) {
        return rc;
    }

    /* apply owner changes, if any */
    if (chown(path, uid, gid) == -1) {
        SRPLG_LOG_ERR(srpds_name, "Changing owner of \"%s\" failed (%s).", path, strerror(errno));
        if ((errno == EACCES) || (errno == EPERM)) {
            rc = SR_ERR_UNAUTHORIZED;
        } else {
            rc = SR_ERR_INTERNAL;
        }
        return rc;
    }

    /* apply permission changes, if any */
    if (perm && (chmod(path, perm) == -1)) {
        SRPLG_LOG_ERR(srpds_name, "Changing permissions (mode) of \"%s\" failed (%s).", path, strerror(errno));
        if ((errno == EACCES) || (errno == EPERM)) {
            rc = SR_ERR_UNAUTHORIZED;
        } else {
            rc = SR_ERR_INTERNAL;
        }
        return rc;
    }

    return SR_ERR_OK;
}

/**
 * @brief Get augeas lens name from a YANG module.
 *
 * @param[in] mod YANG module to use.
 * @param[out] lens Augeas lens name.
 * @return SR error code to return.
 */
static int
augds_get_lens(const struct lys_module *mod, const char **lens)
{
    LY_ARRAY_COUNT_TYPE u;
    const struct lysc_ext *ext;

    LY_ARRAY_FOR(mod->compiled->exts, u) {
        ext = mod->compiled->exts[u].def;
        if (!strcmp(ext->module->name, "augeas-extension") && !strcmp(ext->name, "augeas-mod-name")) {
            *lens = mod->compiled->exts[u].argument;
            return SR_ERR_OK;
        }
    }

    AUG_LOG_ERRINT_RET;
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
    int rc = SR_ERR_OK, i, label_count = 0, len, new_len;
    const char *aug_err_mmsg, *aug_err_details, *data_error, *value;
    char **label_matches = NULL, *msg = NULL;

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

    if (label_count == 1) {
        /* no more information */
        SRPLG_LOG_ERR(srpds_name, "Augeas data error \"%s\".", data_error);
        rc = SR_ERR_OPERATION_FAILED;
        goto cleanup;
    }

    /* create the error message with all the information */
    if ((len = asprintf(&msg, "Augeas data error \"%s\".", data_error)) == -1) {
        AUG_LOG_ERRMEM_GOTO(rc, cleanup);
    }
    for (i = 1; i < label_count; ++i) {
        /* get value */
        if (aug_get(aug, label_matches[i], &value) != 1) {
            AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
        }

        /* alloc memory */
        new_len = len + 2 + strlen(augds_get_path_node(label_matches[i], 0)) + 2 + strlen(value);
        msg = realloc(msg, new_len + 1);

        /* append */
        sprintf(msg + len, "\n\t%s: %s", augds_get_path_node(label_matches[i], 0), value);
        len = new_len;
    }

    SRPLG_LOG_ERR(srpds_name, msg);
    rc = SR_ERR_OPERATION_FAILED;

cleanup:
    for (i = 0; i < label_count; ++i) {
        free(label_matches[i]);
    }
    free(label_matches);
    free(msg);
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
 * @param[out] data_path Augeas extension data-path value, NULL if none defined.
 * @param[out] value_path Optional augeas extension value-yang-path value, NULL if none defined.
 */
static void
augds_get_ext_path(const struct lysc_node *node, const char **data_path, const char **value_path)
{
    LY_ARRAY_COUNT_TYPE u;
    const struct lysc_ext *ext;

    *data_path = NULL;
    if (value_path) {
        *value_path = NULL;
    }

    LY_ARRAY_FOR(node->exts, u) {
        ext = node->exts[u].def;
        if (!strcmp(ext->module->name, "augeas-extension")) {
            if (!strcmp(ext->name, "data-path")) {
                *data_path = node->exts[u].argument;
            } else if (value_path && !strcmp(ext->name, "value-yang-path")) {
                *value_path = node->exts[u].argument;
            }
        }
    }
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
 * @param[in] parent Parent of child siblings, NULL if top-level.
 * @param[out] augnode Array of initialized augnodes.
 * @param[out] augnode_count Count of @p augnode.
 * @return SR error code.
 */
static int
augds_init_auginfo_siblings_r(const struct lys_module *mod, const struct lysc_node *parent, struct augnode **augnodes,
        uint32_t *augnode_count)
{
    const struct lysc_node *node = NULL, *node2;
    const char *data_path, *value_path;
    struct augnode *anode;
    void *mem;
    int r;

    while ((node = lys_getnext(node, parent, mod ? mod->compiled : NULL, 0))) {
        if (lysc_is_key(node)) {
            /* keys can be skipped, their extension path is on their list */
            continue;
        }

        /* get the data-path from the extension */
        augds_get_ext_path(node, &data_path, &value_path);
        if (!data_path && !(node->nodetype & LYD_NODE_INNER)) {
            /* term nodes can be skipped */
            continue;
        }

        node2 = NULL;
        if (value_path) {
            /* another schema node sibling (list key sibling, if list) */
            if (node->nodetype == LYS_LIST) {
                node2 = lys_find_child(node, mod, value_path, 0, 0, 0);
            } else {
                node2 = lys_find_child(parent, mod, value_path, 0, 0, 0);
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

        /* fill augnode children, recursively */
        if ((r = augds_init_auginfo_siblings_r(mod, node, &anode->child, &anode->child_count))) {
            return r;
        }
    }

    return SR_ERR_OK;
}

/**
 * @brief Destroy augeas structure.
 */
static void
augds_destroy(void)
{
    struct augmod *mod;
    uint32_t i, j;

    /* free auginfo */
    for (i = 0; i < auginfo.mod_count; ++i) {
        mod = &auginfo.mods[i];
        for (j = 0; j < mod->toplevel_count; ++j) {
            augds_free_info_node(&mod->toplevel[j]);
        }
        free(mod->toplevel);
    }
    free(auginfo.mods);
    auginfo.mods = NULL;
    auginfo.mod_count = 0;

    /* destroy augeas */
    aug_close(auginfo.aug);
    auginfo.aug = NULL;
}

/**
 * @brief Initialize augeas structure for a YANG module.
 *
 * @param[in] mod YANG module.
 * @param[out] augmod Optional created/found augmod structure for @p mod.
 * @return SR error code.
 */
static int
augds_init(const struct lys_module *mod, struct augmod **augmod)
{
    int rc = SR_ERR_OK;
    uint32_t i;
    const char *lens;
    char *path = NULL, *value = NULL;
    void *ptr;
    struct augmod *augm = NULL;

    if (!auginfo.aug) {
        /* init augeas with all modules but no loaded files */
        auginfo.aug = aug_init(NULL, NULL, AUG_NO_LOAD | AUG_NO_ERR_CLOSE | AUG_SAVE_BACKUP);
        if ((rc = augds_check_erraug(auginfo.aug))) {
            goto cleanup;
        }

        /* remove all lenses except this one so we are left only with 'incl' and 'excl' for all the lenses */
        aug_rm(auginfo.aug, "/augeas/load/*/lens");
    }

    /* try to find this module in auginfo, it must be there if already initialized */
    for (i = 0; i < auginfo.mod_count; ++i) {
        if (auginfo.mods[i].mod == mod) {
            /* found */
            augm = &auginfo.mods[i];
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
    if (aug_set(auginfo.aug, path, value) == -1) {
        AUG_LOG_ERRAUG_GOTO(auginfo.aug, rc, cleanup);
    }

#ifdef AUG_TEST_INPUT_FILES
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

    /* set only test files to be loaded */
    free(value);
    value = strdup(AUG_TEST_INPUT_FILES);
    i = 1;
    for (ptr = strtok(value, ";"); ptr; ptr = strtok(NULL, ";")) {
        free(path);
        if (asprintf(&path, "/augeas/load/%s/incl[%" PRIu32 "]", lens, i++) == -1) {
            AUG_LOG_ERRMEM_GOTO(rc, cleanup);
        }
        if (aug_set(auginfo.aug, path, ptr) == -1) {
            AUG_LOG_ERRAUG_GOTO(auginfo.aug, rc, cleanup);
        }
    }
#endif

    /* load data to populate parsed files */
    aug_load(auginfo.aug);
    if ((rc = augds_check_erraug(auginfo.aug))) {
        goto cleanup;
    }

    /* create new auginfo module */
    ptr = realloc(auginfo.mods, (auginfo.mod_count + 1) * sizeof *auginfo.mods);
    if (!ptr) {
        AUG_LOG_ERRMEM_GOTO(rc, cleanup);
    }
    auginfo.mods = ptr;
    augm = &auginfo.mods[auginfo.mod_count];
    ++auginfo.mod_count;

    /* fill auginfo module */
    augm->mod = mod;
    augm->toplevel = NULL;
    augm->toplevel_count = 0;
    if ((rc = augds_init_auginfo_siblings_r(mod, NULL, &augm->toplevel, &augm->toplevel_count))) {
        goto cleanup;
    }

cleanup:
    free(path);
    free(value);
    if (augmod) {
        *augmod = augm;
    }
    if (rc) {
        augds_destroy();
        if (augmod) {
            *augmod = NULL;
        }
    }
    return rc;
}

static int
srpds_aug_init(const struct lys_module *mod, sr_datastore_t ds, const char *owner, const char *group, mode_t perm)
{
    (void)mod;
    (void)owner;
    (void)group;
    (void)perm;

    /* keep owner/group/perms as they are */

    if (ds != SR_DS_STARTUP) {
        SRPLG_LOG_ERR(srpds_name, "Only startup datastore is supported by this DS plugin.");
        return SR_ERR_UNSUPPORTED;
    }

    /* no initialization tasks to perform, the config files must already exist */

    return SR_ERR_OK;
}

static int
srpds_aug_destroy(const struct lys_module *mod, sr_datastore_t ds)
{
    (void)mod;
    (void)ds;

    /* destroy the cache, nothing else, keep the config files as they are */
    augds_destroy();

    return SR_ERR_OK;
}

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

/**
 * @brief Look for a character in a string, searching backwards.
 *
 * @param[in] start Start of the whole string.
 * @param[in] s Pointer to @p start where to start the search.
 * @param[in] c Character to search for.
 * @return Pointer to the first character occurence;
 * @return NULL of the character was not found.
 */
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

/**
 * @brief Transform a YANG data node into augeas data.
 *
 * @param[in] node YANG data node.
 * @param[in] parent_aug_path Augeas path of the YANG data parent of @p node.
 * @param[out] aug_path Augeas path to store.
 * @param[out] aug_value Augeas value to store.
 * @param[out] node2 Second YANG data node if both reference single Augeas node.
 * @return SR error code.
 */
static int
augds_node_yang2aug(const struct lyd_node *node, const char *parent_aug_path, char **aug_path, const char **aug_value,
        struct lyd_node **node2)
{
    int rc = SR_ERR_OK, path_seg_len;
    const char *data_path, *value_path;
    const char *parent_data_path = NULL, *parent_node, *start, *end, *node_name, *path_segment, *index;
    char *result = NULL, *tmp;
    enum augds_ext_node_type node_type;

    *node2 = NULL;

    /* node extension data-path */
    augds_get_ext_path(node->schema, &data_path, &value_path);
    if (!data_path) {
        /* nothing to set in augeas data */
        *aug_path = NULL;
        *aug_value = NULL;
        goto cleanup;
    }

    /* parent last node */
    if (node->parent) {
        augds_get_ext_path(node->parent->schema, &parent_data_path, NULL);
    }
    parent_node = parent_data_path ? augds_get_path_node(parent_data_path, 1) : NULL;

    end = data_path + strlen(data_path);
    do {
        start = augds_strchr_back(data_path, end - 1, '/');
        if (start) {
            /* skip slash */
            ++start;
        } else {
            /* first node, last processed */
            start = data_path;
        }

        /* handle special ext path node characters */
        if (!strncmp(start, "$$", 2)) {
            node_type = AUGDS_EXT_NODE_LABEL;
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
            start = data_path;
        } else {
            switch (node_type) {
            case AUGDS_EXT_NODE_LABEL:
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

        index = NULL;
        if (!result) {
            /* last node being processed (first iteration), set augeas value */
            if (value_path) {
                /* value is stored in a different YANG node (it may not exist is no value was set) */
                lyd_find_path((node->schema->nodetype == LYS_LIST) ? node : lyd_parent(node), value_path, 0, node2);
                *aug_value = lyd_get_value(*node2);
            } else {
                switch (node_type) {
                case AUGDS_EXT_NODE_LABEL:
                case AUGDS_EXT_NODE_POSITION:
                    *aug_value = NULL;
                    break;
                case AUGDS_EXT_NODE_VALUE:
                    if (node->schema->nodetype == LYS_LIST) {
                        assert(lyd_child(node)->schema->flags & LYS_KEY);
                        *aug_value = lyd_get_value(lyd_child(node));
                    } else {
                        *aug_value = lyd_get_value(node);
                    }
                    break;
                }
            }

            /* also needs an index */
            if (node_type == AUGDS_EXT_NODE_POSITION) {
                if (node->schema->nodetype == LYS_LIST) {
                    assert(lyd_child(node)->schema->flags & LYS_KEY);
                    assert(!strcmp(LYD_NAME(lyd_child(node)), "_id"));
                    index = lyd_get_value(lyd_child(node));
                } else {
                    assert(!strcmp(LYD_NAME(node), "_id"));
                    index = lyd_get_value(node);
                }
            }
        }

        /* add path segment */
        if (asprintf(&tmp, "%.*s%s%s%s%s%s", path_seg_len, path_segment, result ? "/" : "", result ? result : "",
                index ? "[" : "", index ? index : "", index ? "]" : "") == -1) {
            AUG_LOG_ERRMEM_GOTO(rc, cleanup);
        }
        free(result);
        result = tmp;

        /* next iter */
        end = start - 1;
    } while (start != data_path);

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
augds_yang2aug_diff_apply(augeas *aug, enum augds_diff_op op, const char *aug_path, const char *aug_value, int *applied_r)
{
    int rc = SR_ERR_OK;

    if (applied_r) {
        *applied_r = 0;
    }

    if (!aug_path) {
        /* nothing to do */
        goto cleanup;
    }

    switch (op) {
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
    return rc;
}

/**
 * @brief Apply YANG data diff to augeas data performing the changes for the subtree, recursively.
 *
 * @param[in] aug Augeas context.
 * @param[in] diff YANG data diff subtree to apply.
 * @param[in] parent_path Augeas path of the YANG data diff parent of @p diff.
 * @param[in] parent_op YANG data diff parent operation, 0 if none.
 * @return SR error code.
 */
static int
augds_yang2aug_diff_r(augeas *aug, const struct lyd_node *diff, const char *parent_path, enum augds_diff_op parent_op)
{
    int rc = SR_ERR_OK, applied_r;
    enum augds_diff_op cur_op, cur_op2;
    char *aug_path = NULL;
    const char *aug_value;
    struct lyd_node *diff2;

    /* generate augeas path */
    if ((rc = augds_node_yang2aug(diff, parent_path, &aug_path, &aug_value, &diff2))) {
        goto cleanup;
    }

    /* get node operation */
    cur_op = augds_diff_get_op(diff);
    if (!cur_op) {
        cur_op = parent_op;
    }
    assert(cur_op);

    /* apply */
    if ((rc = augds_yang2aug_diff_apply(aug, cur_op, aug_path, aug_value, &applied_r))) {
        goto cleanup;
    }

    if (diff2) {
        /* process the other node, too */
        cur_op2 = augds_diff_get_op(diff2);
        if (cur_op2 && (cur_op2 != cur_op)) {
            /* different operation must be applied */
            if ((rc = augds_yang2aug_diff_apply(aug, cur_op2, aug_path, aug_value, NULL))) {
                goto cleanup;
            }
        }
    }

    if (!applied_r) {
        /* process children recursively */
        LY_LIST_FOR(lyd_child_no_keys(diff), diff) {
            if ((rc = augds_yang2aug_diff_r(aug, diff, aug_path, cur_op))) {
                goto cleanup;
            }
        }
    }

cleanup:
    free(aug_path);
    return rc;
}

static int srpds_aug_load(const struct lys_module *mod, sr_datastore_t ds, const char **xpaths, uint32_t xpath_count,
        struct lyd_node **mod_data);

/**
 * @brief Get all config files parsed by an augeas lens.
 *
 * @param[in] aug Augeas context.
 * @param[in] mod Augeas lens YANG module.
 * @param[in] fs_path Whether to skip "/files" for each returned file.
 * @param[out] files Array of config file paths.
 * @param[out] file_count Count of @p files.
 * @return SR error code.
 */
static int
augds_get_config_files(augeas *aug, const struct lys_module *mod, int fs_path, const char ***files, uint32_t *file_count)
{
    int rc = SR_ERR_OK, i, label_count;
    char *path = NULL, **label_matches = NULL;
    const char *value, *lens_name;
    void *mem;

    *files = NULL;
    *file_count = 0;

    /* get all the path labels */
    augds_get_lens(mod, &lens_name);
    if (asprintf(&path, "/augeas/files//*[lens='@%s']/path", lens_name) == -1) {
        AUG_LOG_ERRMEM_GOTO(rc, cleanup);
    }
    label_count = aug_match(aug, path, &label_matches);
    if (label_count == -1) {
        AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
    } else if (!label_count) {
        AUG_LOG_ERRINT_GOTO(rc, cleanup);
    }

    /* get all the parsed files */
    for (i = 0; i < label_count; ++i) {
        if (aug_get(aug, label_matches[i], &value) != 1) {
            AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
        }
        assert(!strncmp(value, "/files/", 7));

        mem = realloc(*files, (*file_count + 1) * sizeof **files);
        if (!mem) {
            AUG_LOG_ERRMEM_GOTO(rc, cleanup);
        }
        *files = mem;

        (*files)[*file_count] = fs_path ? value + 6 : value;
        ++(*file_count);
    }

cleanup:
    free(path);
    for (i = 0; i < label_count; ++i) {
        free(label_matches[i]);
    }
    free(label_matches);
    if (rc) {
        free(*files);
        *files = NULL;
        *file_count = 0;
    }
    return rc;
}

static int
srpds_aug_store(const struct lys_module *mod, sr_datastore_t ds, const struct lyd_node *mod_data)
{
    int rc = SR_ERR_OK;
    struct lyd_node *cur_data = NULL, *diff = NULL, *root;
    struct ly_set *set = NULL;
    char *aug_file = NULL;
    uint32_t i;

    /* init */
    if ((rc = augds_init(mod, NULL))) {
        goto cleanup;
    }

    /* get current data */
    if ((rc = srpds_aug_load(mod, ds, NULL, 0, &cur_data))) {
        goto cleanup;
    }

    /* get diff with the updated data */
    if (lyd_diff_siblings(cur_data, mod_data, 0, &diff)) {
        AUG_LOG_ERRLY_GOTO(mod->ctx, rc, cleanup);
    }
    if (!diff) {
        /* no changes */
        goto cleanup;
    }

    /* get all the changed files */
    if (lyd_find_xpath(diff, "/*/config-file", &set)) {
        AUG_LOG_ERRLY_GOTO(LYD_CTX(diff), rc, cleanup);
    }

    for (i = 0; i < set->count; ++i) {
        /* get augeas file path */
        if (asprintf(&aug_file, "/files%s", lyd_get_value(set->dnodes[i])) == -1) {
            AUG_LOG_ERRMEM_GOTO(rc, cleanup);
        }

        /* set augeas context for relative paths */
        if (aug_set(auginfo.aug, "/augeas/context", aug_file) == -1) {
            AUG_LOG_ERRAUG_GOTO(auginfo.aug, rc, cleanup);
        }

        /* apply diff to augeas data */
        root = lyd_parent(set->dnodes[i]);
        if ((rc = augds_yang2aug_diff_r(auginfo.aug, root, NULL, augds_diff_get_op(root)))) {
            goto cleanup;
        }

        free(aug_file);
        aug_file = NULL;
    }

    /* store new augeas data */
    if (aug_save(auginfo.aug) == -1) {
        AUG_LOG_ERRAUG_GOTO(auginfo.aug, rc, cleanup);
    }

cleanup:
    lyd_free_siblings(cur_data);
    lyd_free_siblings(diff);
    ly_set_free(set, NULL);
    free(aug_file);
    return rc;
}

/**
 * @brief Check file existence.
 *
 * @param[in] path Path to the file.
 * @return Whether the file exists or not.
 */
static int
augds_file_exists(const char *path)
{
    int ret;

    errno = 0;
    ret = access(path, F_OK);
    if ((ret == -1) && (errno != ENOENT)) {
        SRPLG_LOG_WRN(srpds_name, "Failed to check existence of the file \"%s\" (%s).", path, strerror(errno));
        return 0;
    }

    if (ret) {
        assert(errno == ENOENT);
        return 0;
    }
    return 1;
}

/**
 * @brief Copy file contents to another file.
 *
 * @param[in] to Target file to copy to.
 * @param[in] from Source file to copy from.
 * @return SR error code.
 */
static int
augds_cp_path(const char *to, const char *from)
{
    int rc = SR_ERR_OK, fd_to = -1, fd_from = -1;
    char *out_ptr, buf[4096];
    ssize_t nread, nwritten;

    /* open "from" file */
    fd_from = open(from, O_RDONLY, 0);
    if (fd_from < 0) {
        SRPLG_LOG_ERR(srpds_name, "Opening \"%s\" failed (%s).", from, strerror(errno));
        rc = SR_ERR_SYS;
        goto cleanup;
    }

    /* open "to" */
    fd_to = open(to, O_WRONLY | O_TRUNC, 0);
    if (fd_to < 0) {
        SRPLG_LOG_ERR(srpds_name, "Opening \"%s\" failed (%s).", to, strerror(errno));
        rc = SR_ERR_SYS;
        goto cleanup;
    }

    while ((nread = read(fd_from, buf, sizeof buf)) > 0) {
        out_ptr = buf;
        do {
            nwritten = write(fd_to, out_ptr, nread);
            if (nwritten >= 0) {
                nread -= nwritten;
                out_ptr += nwritten;
            } else if (errno != EINTR) {
                SRPLG_LOG_ERR(srpds_name, "Writing data failed (%s).", strerror(errno));
                rc = SR_ERR_SYS;
                goto cleanup;
            }
        } while (nread > 0);
    }
    if (nread == -1) {
        SRPLG_LOG_ERR(srpds_name, "Reading data failed (%s).", strerror(errno));
        rc = SR_ERR_SYS;
        goto cleanup;
    }

cleanup:
    if (fd_from > -1) {
        close(fd_from);
    }
    if (fd_to > -1) {
        close(fd_to);
    }
    return rc;
}

static void
srpds_aug_recover(const struct lys_module *mod, sr_datastore_t ds)
{
    uint32_t i, file_count;
    const char **files = NULL;
    char *bck_path = NULL;
    struct lyd_node *mod_data = NULL;

    /* init */
    if (augds_init(mod, NULL)) {
        return;
    }

    /* check whether the file(s) is valid */
    if (!srpds_aug_load(mod, ds, NULL, 0, &mod_data)) {
        /* data are valid, nothing to do */
        goto cleanup;
    }

    /* get all parsed files */
    if (augds_get_config_files(auginfo.aug, mod, 1, &files, &file_count)) {
        goto cleanup;
    }

    for (i = 0; i < file_count; ++i) {
        /* generate the backup path */
        if (asprintf(&bck_path, "%s%s", files[i], AUG_FILE_BACKUP_SUFFIX) == -1) {
            AUG_LOG_ERRMEM;
            goto cleanup;
        }

        if (augds_file_exists(bck_path)) {
            SRPLG_LOG_WRN("Recovering backup file for \"%s\".", files[i]);

            /* restore the backup data, avoid changing permissions of the target file */
            if (augds_cp_path(files[i], bck_path)) {
                goto cleanup;
            }

            /* remove the backup file */
            if (unlink(bck_path) == -1) {
                SRPLG_LOG_ERR(srpds_name, "Unlinking \"%s\" failed (%s).", bck_path, strerror(errno));
                goto cleanup;
            }
        } else {
            /* there is not much to do but remove the corrupted file */
            SRPLG_LOG_WRN("No backup for \"%s\" to recover.", files[i]);
        }

        free(bck_path);
        bck_path = NULL;
    }

cleanup:
    free(files);
    free(bck_path);
    lyd_free_all(mod_data);
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
    size_t len;

    /* handle special ext path node characters */
    if (!strncmp(ext_node, "$$", 2)) {
        /* matches everything */
        if (node_type) {
            *node_type = AUGDS_EXT_NODE_LABEL;
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
    const char *value, *value2, *ext_node, *label_node;
    char *label, *pos_str = NULL;
    enum augds_ext_node_type node_type;
    struct lyd_node *new_node, *parent2;

    for (i = 0; i < augnode_count; ++i) {
        if (augnodes[i].data_path) {
            ext_node = augds_get_path_node(augnodes[i].data_path, 0);

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

                value = NULL;
                value2 = NULL;
                switch (node_type) {
                case AUGDS_EXT_NODE_VALUE:
                    /* get value */
                    if (aug_get(aug, label, &value) != 1) {
                        AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
                    }
                    break;
                case AUGDS_EXT_NODE_POSITION:
                    /* get position */
                    pos_str = augds_get_path_node_position(label);
                    value = pos_str;
                    break;
                case AUGDS_EXT_NODE_LABEL:
                    /* make sure it matches no following paths */
                    for (k = i + 1; k < augnode_count; ++k) {
                        if (augds_ext_label_node_equal(augds_get_path_node(augnodes[k].data_path, 0), label_node, NULL)) {
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
                if (augnodes[i].value_path) {
                    /* we will also use the value */
                    if (aug_get(aug, label, &value2) != 1) {
                        AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
                    }
                }

                if (value) {
                    /* create and append the term node */
                    if ((rc = augds_aug2yang_augnode_create_node(augnodes[i].schema, value, parent, first, &new_node))) {
                        goto cleanup;
                    }

                    if (value2) {
                        /* also create and append the second node */
                        parent2 = (augnodes[i].schema->nodetype == LYS_LIST) ? new_node : parent;
                        if ((rc = augds_aug2yang_augnode_create_node(augnodes[i].schema2, value2, parent2, first, NULL))) {
                            goto cleanup;
                        }
                    }

                    /* recursively handle all children of this data node */
                    if ((rc = augds_aug2yang_augnode_r(aug, augnodes[i].child, augnodes[i].child_count, label, new_node,
                            first))) {
                        goto cleanup;
                    }
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
            /* create and append the top-level list node with value being the file path */
            assert(augnodes[i].schema->nodetype == LYS_LIST);
            assert(!strcmp(lysc_node_child(augnodes[i].schema)->name, "config-file"));
            assert(!strncmp(parent_label, "/files", 6));
            if ((rc = augds_aug2yang_augnode_create_node(augnodes[i].schema, parent_label + 6, parent, first, &new_node))) {
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

    /* get all matching augeas labels at this depth, skip comments */
    if (asprintf(&path, "%s/*[label() != '#comment']", parent_label) == -1) {
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
    int rc = SR_ERR_OK;
    uint32_t i, file_count;
    struct augmod *augmod;
    const char **files = NULL;

    (void)mod;
    (void)ds;
    (void)xpaths;
    (void)xpath_count;

    *mod_data = NULL;

    /* init */
    if ((rc = augds_init(mod, &augmod))) {
        goto cleanup;
    }

    /* reload data if they changed */
    aug_load(auginfo.aug);
    if ((rc = augds_check_erraug(auginfo.aug))) {
        goto cleanup;
    }

    /* get all parsed files */
    if ((rc = augds_get_config_files(auginfo.aug, mod, 0, &files, &file_count))) {
        goto cleanup;
    }
    if (!file_count) {
        AUG_LOG_ERRINT_GOTO(rc, cleanup);
    }

    for (i = 0; i < file_count; ++i) {
        /* transform augeas context data to YANG data */
        if ((rc = augds_aug2yang_augnode_r(auginfo.aug, augmod->toplevel, augmod->toplevel_count, files[i],
                NULL, mod_data))) {
            goto cleanup;
        }
    }

cleanup:
    free(files);
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
    int rc = SR_ERR_OK;
    const char **files = NULL;
    uint32_t i, file_count;

    (void)ds;
    assert(mod && (owner || group || perm));

    /* init */
    if ((rc = augds_init(mod, NULL))) {
        goto cleanup;
    }

    /* get all parsed files */
    if ((rc = augds_get_config_files(auginfo.aug, mod, 1, &files, &file_count))) {
        goto cleanup;
    }
    if (!file_count) {
        AUG_LOG_ERRINT_GOTO(rc, cleanup);
    }

    /* set permissions of each file */
    for (i = 0; i < file_count; ++i) {
        if ((rc = augds_chmodown(files[i], owner, group, perm))) {
            goto cleanup;
        }
    }

cleanup:
    free(files);
    return rc;
}

static int
srpds_aug_access_get(const struct lys_module *mod, sr_datastore_t ds, char **owner, char **group, mode_t *perm)
{
    int rc = SR_ERR_OK;
    struct stat st;
    const char **files = NULL;
    uint32_t file_count;

    (void)ds;

    if (owner) {
        *owner = NULL;
    }
    if (group) {
        *group = NULL;
    }

    /* init */
    if ((rc = augds_init(mod, NULL))) {
        goto cleanup;
    }

    /* get all parsed files */
    if ((rc = augds_get_config_files(auginfo.aug, mod, 1, &files, &file_count))) {
        goto cleanup;
    }
    if (!file_count) {
        AUG_LOG_ERRINT_GOTO(rc, cleanup);
    }

    /* use the first file, nothing much better to do */
    if (stat(files[0], &st) == -1) {
        if (errno == EACCES) {
            SRPLG_LOG_ERR(srpds_name, "Learning \"%s\" permissions failed.", files[0]);
            rc = SR_ERR_UNAUTHORIZED;
        } else {
            SRPLG_LOG_ERR(srpds_name, "Stat of \"%s\" failed (%s).", files[0], strerror(errno));
            rc = SR_ERR_SYS;
        }
        goto cleanup;
    }

    /* get owner */
    if (owner && (rc = augds_get_pwd(&st.st_uid, owner))) {
        goto cleanup;
    }

    /* get group */
    if (group && (rc = augds_get_grp(&st.st_gid, group))) {
        goto cleanup;
    }

    /* get perms */
    if (perm) {
        *perm = st.st_mode & 0007777;
    }

cleanup:
    free(files);
    if (rc && owner) {
        free(*owner);
        *owner = NULL;
    }
    if (rc && group) {
        free(*group);
        *group = NULL;
    }
    return rc;
}

static int
srpds_aug_access_check(const struct lys_module *mod, sr_datastore_t ds, int *read, int *write)
{
    int rc = SR_ERR_OK;
    const char **files = NULL;
    uint32_t file_count;

    (void)ds;

    /* init */
    if ((rc = augds_init(mod, NULL))) {
        goto cleanup;
    }

    /* get all parsed files */
    if ((rc = augds_get_config_files(auginfo.aug, mod, 1, &files, &file_count))) {
        goto cleanup;
    }
    if (!file_count) {
        AUG_LOG_ERRINT_GOTO(rc, cleanup);
    }

    /* check read */
    if (read) {
        if (eaccess(files[0], R_OK) == -1) {
            if (errno == EACCES) {
                *read = 0;
            } else {
                SRPLG_LOG_ERR(srpds_name, "Eaccess of \"%s\" failed (%s).", files[0], strerror(errno));
                rc = SR_ERR_SYS;
                goto cleanup;
            }
        } else {
            *read = 1;
        }
    }

    /* check write */
    if (write) {
        if (eaccess(files[0], W_OK) == -1) {
            if (errno == EACCES) {
                *write = 0;
            } else {
                SRPLG_LOG_ERR(srpds_name, "Eaccess of \"%s\" failed (%s).", files[0], strerror(errno));
                rc = SR_ERR_SYS;
                goto cleanup;
            }
        } else {
            *write = 1;
        }
    }

cleanup:
    free(files);
    return rc;
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
