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

int
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

int
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

int
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

int
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

int
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

int
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

const char *
augds_get_label_node(const char *label, char **dyn)
{
    const char *start;
    char *ptr;
    int len;

    if (dyn) {
        *dyn = NULL;
    }

    /* get last label segment */
    start = strrchr(label, '/');
    start = start ? start + 1 : label;

    len = strlen(start);
    if (strchr(start, '\\') || (start[len - 1] == ']')) {
        assert(dyn);

        /* skip position predicate */
        if (start[len - 1] == ']') {
            while (start[len] != '[') {
                --len;
            }
        }

        /* we need a dynamic string */
        *dyn = strndup(start, len);
        if (!*dyn) {
            AUG_LOG_ERRMEM;
            return NULL;
        }
        start = *dyn;

        while ((ptr = strchr(start, '\\'))) {
            /* decode special Augeas chars by skipping '\' */
            memmove(ptr, ptr + 1, strlen(ptr + 1));
            --len;
        }
        (*dyn)[len] = '\0';
    }

    return start;
}

/**
 * @brief Append a single piece of error information to the message.
 *
 * @param[in] name Error information label.
 * @param[in] value Error information value.
 * @param[in,out] msg Message to append to.
 * @param[in,out] msg_len Length of @p msg, is updated.
 * @return SR error code.
 */
static int
augds_erraug_append(const char *name, const char *value, char **msg, int *msg_len)
{
    int new_len;
    void *mem;

    if (!name) {
        /* alloc memory */
        new_len = *msg_len + 1;
        mem = realloc(*msg, new_len + 1);
        if (!mem) {
            AUG_LOG_ERRMEM_RET;
        }
        *msg = mem;

        /* append */
        sprintf(*msg + *msg_len, "\n");
        *msg_len = new_len;

        return SR_ERR_OK;
    }

    /* alloc memory */
    new_len = *msg_len + 2 + strlen(name) + 2 + strlen(value);
    mem = realloc(*msg, new_len + 1);
    if (!mem) {
        AUG_LOG_ERRMEM_RET;
    }
    *msg = mem;

    /* append */
    sprintf(*msg + *msg_len, "\n\t%s: %s", name, value);
    *msg_len = new_len;

    return SR_ERR_OK;
}

/**
 * @brief Process single Augeas error.
 *
 * @param[in] aug Augeas handle.
 * @param[in] aug_err_path Augeas path to the error.
 * @param[in,out] msg Message to append to.
 * @param[in,out] msg_len Length of @p msg, is updated.
 * @return SR error code.
 */
static int
augds_erraug_error(augeas *aug, const char *aug_err_path, char **msg, int *msg_len)
{
    int rc = SR_ERR_OK, len, i, label_count = 0;
    const char *value;
    char *path = NULL, *file = NULL, **label_matches = NULL;

    /* get error */
    if (aug_get(aug, aug_err_path, &value) != 1) {
        AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
    }
    if ((rc = augds_erraug_append("error", value, msg, msg_len))) {
        AUG_LOG_ERRMEM_GOTO(rc, cleanup);
    }

    /* get file from the error path itself */
    assert(!strncmp(aug_err_path, "/augeas/files", 13));
    assert(!strcmp(aug_err_path + strlen(aug_err_path) - 6, "/error"));
    len = strlen(aug_err_path);
    file = strndup(aug_err_path + 13, (len - 13) - 6);
    if (!file) {
        AUG_LOG_ERRMEM_GOTO(rc, cleanup);
    }
    if ((rc = augds_erraug_append("file", file, msg, msg_len))) {
        AUG_LOG_ERRMEM_GOTO(rc, cleanup);
    }

    /* get error details */
    if (asprintf(&path, "%s/*", aug_err_path) == -1) {
        AUG_LOG_ERRMEM_GOTO(rc, cleanup);
    }
    label_count = aug_match(aug, path, &label_matches);
    if (label_count == -1) {
        AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
    }

    for (i = 0; i < label_count; ++i) {
        /* get value */
        if (aug_get(aug, label_matches[i], &value) != 1) {
            AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
        }

        /* append */
        if ((rc = augds_erraug_append(augds_get_label_node(label_matches[i], NULL), value, msg, msg_len))) {
            AUG_LOG_ERRMEM_GOTO(rc, cleanup);
        }
    }

cleanup:
    for (i = 0; i < label_count; ++i) {
        free(label_matches[i]);
    }
    free(label_matches);
    free(path);
    free(file);
    return rc;
}

int
augds_check_erraug(augeas *aug)
{
    int rc = SR_ERR_OK, i, label_count = 0, len;
    const char *aug_err_mmsg, *aug_err_details;
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
        SRPLG_LOG_ERR(srpds_name, "Augeas error (%s%s%s%s%s).", aug_error_message(aug),
                aug_err_mmsg ? "; " : "", aug_err_mmsg ? aug_err_mmsg : "", aug_err_details ? "; " : "",
                aug_err_details ? aug_err_details : "");
        return SR_ERR_OPERATION_FAILED;
    }

    /* check for data error */
    label_count = aug_match(aug, "/augeas/files//error", &label_matches);
    if (label_count == -1) {
        AUG_LOG_ERRAUG_GOTO(aug, rc, cleanup);
    } else if (!label_count) {
        /* no error */
        goto cleanup;
    }

    /* data error */
    if ((len = asprintf(&msg, "Augeas data error:")) == -1) {
        AUG_LOG_ERRMEM_GOTO(rc, cleanup);
    }

    /* process all the errors */
    for (i = 0; i < label_count; ++i) {
        if ((rc = augds_erraug_error(aug, label_matches[i], &msg, &len))) {
            goto cleanup;
        }

        /* finish error with a newline */
        if ((rc = augds_erraug_append(NULL, NULL, &msg, &len))) {
            AUG_LOG_ERRMEM_GOTO(rc, cleanup);
        }
    }

    /* log */
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

void
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

int
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

void
augds_node_get_type(const struct lysc_node *node, enum augds_ext_node_type *node_type, const char **data_path,
        const char **value_path)
{
    LY_ARRAY_COUNT_TYPE u;
    const struct lysc_ext *ext;
    const char *dpath = NULL, *vpath = NULL;

    if (data_path) {
        *data_path = NULL;
    }
    if (value_path) {
        *value_path = NULL;
    }

    LY_ARRAY_FOR(node->exts, u) {
        ext = node->exts[u].def;
        if (!strcmp(ext->module->name, "augeas-extension")) {
            if (!strcmp(ext->name, "data-path")) {
                dpath = node->exts[u].argument;
            } else if (!strcmp(ext->name, "value-yang-path")) {
                vpath = node->exts[u].argument;
            }
        }
    }

    if (dpath) {
        /* handle special ext data path characters */
        if (!strncmp(dpath, "$$", 2)) {
            *node_type = AUGDS_EXT_NODE_LABEL;
        } else {
            *node_type = AUGDS_EXT_NODE_VALUE;
        }
    } else if ((node->nodetype == LYS_LIST) && !strcmp(lysc_node_child(node)->name, "_r-id")) {
        /* recursive list */
        *node_type = AUGDS_EXT_NODE_REC_LIST;
    } else if ((node->nodetype & LYD_NODE_TERM) && (((struct lysc_node_leaf *)node)->type->basetype == LY_TYPE_LEAFREF)) {
        /* leafref to the recursive list */
        *node_type = AUGDS_EXT_NODE_REC_LREF;
    } else {
        /* else nothing to set in augeas data */
        *node_type = AUGDS_EXT_NODE_NONE;
    }

    if (data_path) {
        *data_path = dpath;
    }
    if (value_path) {
        *value_path = vpath;
    }
}

const char *
augds_get_term_value(const struct lyd_node *node)
{
    const char *val;

    if (!node || !(node->schema->nodetype & LYD_NODE_TERM)) {
        return NULL;
    }

    if (((struct lyd_node_term *)node)->value.realtype->basetype == LY_TYPE_EMPTY) {
        val = NULL;
    } else {
        val = lyd_get_value(node);
    }

    return val;
}
