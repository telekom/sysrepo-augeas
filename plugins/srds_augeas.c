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
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <augeas.h>
#include <libyang/libyang.h>
#include <sysrepo.h>
#include <sysrepo/plugins_datastore.h>

static struct auginfo auginfo;

static int srpds_aug_load(const struct lys_module *mod, sr_datastore_t ds, const char **xpaths, uint32_t xpath_count,
        struct lyd_node **mod_data);

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
    augds_destroy(&auginfo);

    return SR_ERR_OK;
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
    if ((rc = augds_init(&auginfo, mod, NULL))) {
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
        if ((rc = augds_store_diff_r(auginfo.aug, root, NULL, augds_diff_get_op(root, 0), cur_data))) {
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

static void
srpds_aug_recover(const struct lys_module *mod, sr_datastore_t ds)
{
    uint32_t i, file_count;
    const char **files = NULL;
    char *bck_path = NULL;
    struct lyd_node *mod_data = NULL;

    /* init */
    if (augds_init(&auginfo, mod, NULL)) {
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
    if ((rc = augds_init(&auginfo, mod, &augmod))) {
        goto cleanup;
    }

    /* reload data if they changed */
    aug_load(auginfo.aug);
    if ((rc = augds_check_erraug(auginfo.aug))) {
        goto cleanup;
    }

    /* get all parsed files, there may be none */
    if ((rc = augds_get_config_files(auginfo.aug, mod, 0, &files, &file_count))) {
        goto cleanup;
    }

    for (i = 0; i < file_count; ++i) {
        /* transform augeas context data to YANG data */
        if ((rc = augds_aug2yang_augnode_r(auginfo.aug, augmod->toplevel, augmod->toplevel_count, files[i],
                NULL, mod_data))) {
            goto cleanup;
        }
    }

    /* assume valid */
    assert(!lyd_validate_module(mod_data, augmod->mod, 0, NULL));

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
    if ((rc = augds_init(&auginfo, mod, NULL))) {
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
    if ((rc = augds_init(&auginfo, mod, NULL))) {
        goto cleanup;
    }

    /* get all parsed files */
    if ((rc = augds_get_config_files(auginfo.aug, mod, 1, &files, &file_count))) {
        goto cleanup;
    }
    if (!file_count) {
        /* unknown */
        if (owner) {
            *owner = strdup("<unknown>");
        }
        if (group) {
            *group = strdup("<unknown>");
        }
        if (perm) {
            *perm = 0;
        }
        goto cleanup;
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
    if ((rc = augds_init(&auginfo, mod, NULL))) {
        goto cleanup;
    }

    /* get all parsed files */
    if ((rc = augds_get_config_files(auginfo.aug, mod, 1, &files, &file_count))) {
        goto cleanup;
    }
    if (!file_count) {
        /* unknown, pass */
        if (read) {
            *read = 1;
        }
        if (write) {
            *write = 1;
        }
        goto cleanup;
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
    .running_load_cached_cb = NULL,
    .running_update_cached_cb = NULL,
    .running_flush_cached_cb = NULL,
    .copy_cb = srpds_aug_copy,
    .update_differ_cb = srpds_aug_update_differ,
    .candidate_modified_cb = srpds_aug_candidate_modified,
    .candidate_reset_cb = srpds_aug_candidate_reset,
    .access_set_cb = srpds_aug_access_set,
    .access_get_cb = srpds_aug_access_get,
    .access_check_cb = srpds_aug_access_check,
};
