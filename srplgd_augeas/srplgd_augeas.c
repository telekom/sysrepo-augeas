/**
 * @file srplgd_augeas.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief Augeas sysrepo-plugind plugin for applying configuration changes in run-time
 *
 * @copyright
 * Copyright (c) 2023 Deutsche Telekom AG.
 * Copyright (c) 2023 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#include "srplgda_config.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <libyang/libyang.h>
#include <sysrepo.h>

#define PLG_NAME "srplgd_augeas"

/**
 * @brief Wrapper for execl(3) function.
 *
 * @param[in] pathname Path to the executable to exec.
 * @param[in] ... Arguments to use ended with NULL.
 * @return SR_ERR value.
 */
static int
aug_execl(const char *pathname, ...)
{
    int rc = SR_ERR_OK, wstatus;
    va_list ap;
    const char **args = NULL, *arg;
    uint32_t arg_count = 0;
    void *mem;
    pid_t ch_pid;

    /* process variable args into an array */
    va_start(ap, pathname);
    do {
        arg = va_arg(ap, const char *);

        /* add new arg */
        mem = realloc(args, (arg_count + 1) * sizeof *args);
        if (!mem) {
            SRPLG_LOG_ERR(PLG_NAME, "Memory allocation failed (%s:%d).", __FILE__, __LINE__);
            rc = SR_ERR_NO_MEMORY;
            goto cleanup;
        }
        args = mem;

        args[arg_count] = arg;
        ++arg_count;
    } while (arg);
    va_end(ap);

    /* fork and execv */
    if (!(ch_pid = fork())) {
        execv(pathname, (char * const *)args);
        exit(-1);
    } else if (ch_pid == -1) {
        SRPLG_LOG_ERR(PLG_NAME, "fork() failed (%s).", strerror(errno));
        rc = SR_ERR_SYS;
        goto cleanup;
    }

    /* check return value of the forked process */
    if (waitpid(ch_pid, &wstatus, 0) == -1) {
        SRPLG_LOG_ERR(PLG_NAME, "waitpid() failed (%s).", strerror(errno));
        rc = SR_ERR_SYS;
        goto cleanup;
    }
    if (!WIFEXITED(wstatus)) {
        SRPLG_LOG_ERR(PLG_NAME, "Exec of \"%s\" did not terminate normally.", pathname);
        rc = SR_ERR_OPERATION_FAILED;
        goto cleanup;
    }
    if (WEXITSTATUS(wstatus)) {
        SRPLG_LOG_ERR(PLG_NAME, "Exec of \"%s\" returned %d.", pathname, WEXITSTATUS(wstatus));
        rc = SR_ERR_OPERATION_FAILED;
        goto cleanup;
    }

cleanup:
    free(args);
    return rc;
}

#ifdef PATH_ACTIVEMQ

static int
aug_actimemq_change_cb(sr_session_ctx_t *session, uint32_t sub_id, const char *module_name, const char *xpath,
        sr_event_t event, uint32_t request_id, void *private_data)
{
    (void)session;
    (void)sub_id;
    (void)module_name;
    (void)xpath;
    (void)event;
    (void)request_id;
    (void)private_data;

    return aug_execl(PATH_ACTIVEMQ, "restart", NULL);
}

#endif

int
sr_plugin_init_cb(sr_session_ctx_t *session, void **private_data)
{
    sr_subscription_ctx_t *subscr = NULL;
    const struct ly_ctx *ly_ctx;
    const struct lys_module *ly_mod;
    uint32_t i;
    int rc = SR_ERR_OK;

    sr_session_switch_ds(session, SR_DS_RUNNING);

    /* subscribe to the found supported modules */
    ly_ctx = sr_session_acquire_context(session);
    i = ly_ctx_internal_modules_count(ly_ctx);
    while ((ly_mod = ly_ctx_get_module_iter(ly_ctx, &i))) {
#ifdef PATH_ACTIVEMQ
        if (!strcmp(ly_mod->name, "activemq-conf")) {
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_actimemq_change_cb, NULL, 0, 0, &subscr);
        } else if (!strcmp(ly_mod->name, "activemq-xml")) {
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_actimemq_change_cb, NULL, 0, 0, &subscr);
        }
#endif
        if (rc) {
            goto cleanup;
        }

        /* access - config for pam_access.so, is reread on every login */
    }

cleanup:
    sr_session_release_context(session);
    if (rc) {
        sr_unsubscribe(subscr);
    } else {
        *private_data = subscr;
    }
    return rc;
}

void
sr_plugin_cleanup_cb(sr_session_ctx_t *session, void *private_data)
{
    sr_subscription_ctx_t *subscr = private_data;

    (void)session;

    /* unsubscribe */
    sr_unsubscribe(subscr);
}
