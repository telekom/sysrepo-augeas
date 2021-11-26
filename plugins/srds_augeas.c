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

#include <augeas.h>
#include <libyang/libyang.h>

#define srpds_name "augeas DS"  /**< plugin name */

#define AUG_LOG_ERRINT SRPLG_LOG_ERR(srpds_name, "Internal error (%s:%d).", __FILE__, __LINE__)
#define AUG_LOG_ERRMEM SRPLG_LOG_ERR(srpds_name, "Memory allocation failed (%s:%d).", __FILE__, __LINE__)

#define AUG_LOG_ERRINT_RET AUG_LOG_ERRINT;return SR_ERR_INTERNAL
#define AUG_LOG_ERRINT_GOTO(rc, label) AUG_LOG_ERRINT;rc = SR_ERR_INTERNAL; goto label
#define AUG_LOG_ERRMEM_GOTO(rc, label) AUG_LOG_ERRMEM;rc = SR_ERR_NO_MEMORY; goto label

static augeas *aug; /**< augeas handle */

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
 * @brief Append converted augeas data to YANG data. Convert all data handled by a YANG module
 * using the context in the augeas handle.
 *
 * @param[in] aug Augeas handle.
 * @param[in] mod YANG module.
 * @param[in,out] mod_data YANG module data to append to.
 * @return SR error code.
 */
static int
augds_aug2yang(augeas *aug, const struct lys_module *mod, struct lyd_node **mod_data)
{
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
    aug = aug_init(NULL, NULL, AUG_NO_LOAD | AUG_NO_ERR_CLOSE);
    if ((rc = augds_check_erraug(aug))) {
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
    aug_rm(aug, path);

    /* check owner/group/perms */
    /* TODO */

cleanup:
    free(lens);
    free(path);
    if (rc) {
        aug_close(aug);
        aug = NULL;
    }
    return rc;
}

static int
srpds_aug_destroy(const struct lys_module *mod, sr_datastore_t ds)
{
    (void)mod;
    (void)ds;

    /* destroy augeas */
    aug_close(aug);
    aug = NULL;

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
srpds_aug_load(const struct lys_module *mod, sr_datastore_t ds, const char **xpaths, uint32_t xpath_count,
        struct lyd_node **mod_data)
{
    int rc = SR_ERR_OK, i, match_count = 0;
    char **matches = NULL;
    const char *value;

    *mod_data = NULL;

    /* load data */
    aug_load(aug);
    if ((rc = augds_check_erraug(aug))) {
        goto cleanup;
    }

    /* get parsed file path nodes */
    match_count = aug_match(aug, "/augeas/files//path", &matches);
    if (match_count == -1) {
        rc = augds_check_erraug(aug);
        assert(rc);
        goto cleanup;
    }

    /* get all their values and append their YANG data */
    for (i = 0; i < match_count; ++i) {
        if (aug_get(aug, matches[i], &value) != 1) {
            AUG_LOG_ERRINT_GOTO(rc, cleanup);
        }

        /* set context of this file */
        if (aug_set(aug, "/augeas/context", value) == -1) {
            rc = augds_check_erraug(aug);
            assert(rc);
            goto cleanup;
        }

        /* transform augeas context data to YANG data */
        if ((rc = augds_aug2yang(aug, mod, mod_data))) {
            goto cleanup;
        }
    }

    /* TODO debug */
    FILE *f = fopen("out.txt", "w");
    aug_print(aug, f, "/*");
    fclose(f);

cleanup:
    for (i = 0; i < match_count; ++i) {
        free(matches[i]);
    }
    free(matches);
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
