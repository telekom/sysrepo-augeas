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

#include <stdint.h>
#include <stdlib.h>
#include <sys/wait.h>

#include <libyang/libyang.h>
#include <sysrepo.h>

#include "srplgda_common.h"

#define PLG_NAME "srplgd_augeas"

#ifdef ACTIVEMQ_EXECUTABLE

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

    /* TODO activemq service */
    return aug_execl(PLG_NAME, ACTIVEMQ_EXECUTABLE, "restart", NULL);
}

#endif

#ifdef AVAHI_DAEMON_EXECUTABLE

static int
aug_avahi_change_cb(sr_session_ctx_t *session, uint32_t sub_id, const char *module_name, const char *xpath,
        sr_event_t event, uint32_t request_id, void *private_data)
{
    int r;

    (void)session;
    (void)sub_id;
    (void)module_name;
    (void)xpath;
    (void)event;
    (void)request_id;
    (void)private_data;

    /* TODO avahi-daemon service */
    if ((r = aug_execl(PLG_NAME, AVAHI_DAEMON_EXECUTABLE, "--kill", NULL))) {
        return r;
    }
    return aug_execl(PLG_NAME, AVAHI_DAEMON_EXECUTABLE, "--syslog", "--daemonize", NULL);
}

#endif

#ifdef CACHEFILESD_EXECUTABLE

static int
aug_cachefilesd_change_cb(sr_session_ctx_t *session, uint32_t sub_id, const char *module_name, const char *xpath,
        sr_event_t event, uint32_t request_id, void *private_data)
{
    int r;
    pid_t pid;

    (void)session;
    (void)sub_id;
    (void)module_name;
    (void)xpath;
    (void)event;
    (void)request_id;
    (void)private_data;

    /* TODO cachefilesd service */
    if ((r = aug_pidfile(PLG_NAME, "/var/run/cachefilesd.pid", &pid))) {
        return r;
    }
    if (!pid) {
        /* daemon not running */
        return SR_ERR_OK;
    }

    return aug_send_sighup(PLG_NAME, pid);
}

#endif

#ifdef CARBON_SERVICES

static int
aug_carbon_change_cb(sr_session_ctx_t *session, uint32_t sub_id, const char *module_name, const char *xpath,
        sr_event_t event, uint32_t request_id, void *private_data)
{
    int r;

    (void)session;
    (void)sub_id;
    (void)module_name;
    (void)xpath;
    (void)event;
    (void)request_id;
    (void)private_data;

    /* service files on github https://github.com/graphite-project/carbon/tree/master/distro/redhat/init.d */
    if ((r = aug_execl(PLG_NAME, SYSTEMCTL_EXECUTABLE, "try-restart", "carbon-cache"))) {
        return r;
    }
    if ((r = aug_execl(PLG_NAME, SYSTEMCTL_EXECUTABLE, "try-restart", "carbon-relay"))) {
        return r;
    }
    if ((r = aug_execl(PLG_NAME, SYSTEMCTL_EXECUTABLE, "try-restart", "carbon-aggregator"))) {
        return r;
    }
    return SR_ERR_OK;
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
        if (!strcmp(ly_mod->name, "activemq-conf")) {
#ifdef ACTIVEMQ_EXECUTABLE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_actimemq_change_cb, NULL, 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "activemq-xml")) {
#ifdef ACTIVEMQ_EXECUTABLE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_actimemq_change_cb, NULL, 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "avahi")) {
#ifdef AVAHI_DAEMON_EXECUTABLE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_avahi_change_cb, NULL, 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "cachefilesd")) {
#ifdef CACHEFILESD_EXECUTABLE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_cachefilesd_change_cb, NULL, 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "carbon")) {
#ifdef CARBON_SERVICES
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_carbon_change_cb, NULL, 0, 0, &subscr);
#endif
        }
        if (rc) {
            SRPLG_LOG_ERR(PLG_NAME, "Failed to subscribe to module \"%s\" (%s).", ly_mod->name, sr_strerror(rc));
            goto cleanup;
        }

        /* access - config for pam_access.so, is reread on every login */
        /* afs-cellalias - cellalias(5), no process to use the config file? */
        /* aliases - local(8), should reread the aliases on each mail delivery */
        /* anaconda - https://anaconda-installer.readthedocs.io/en/latest/configuration-files.html, install config file */
        /* anacron - anacron(8), should reread jobs desription on each execution */
        /* approx - approx(8), no daemon, config file read on every exec by inetd */
        /* apt-update-manager - no deamon, config file read on every exec? */
        /* aptcacherngsecurity - no dameon, config file read on every exec? */
        /* aptconf - no dameon, config file read on every exec? */
        /* aptpreferences - no dameon, config file read on every exec? */
        /* aptsources - no dameon, config file read on every exec? */
        /* authinfo2 - https://github.com/s3ql/s3ql, no deamon */
        /* authorized-keys - reread on every use */
        /* authselectpam - pam config, reread on every use */
        /* automaster - autofs(8), no daemon, script config file */
        /* automounter - autofs(5), no daemon */
        /* backuppchosts - https://backuppc.github.io/backuppc/BackupPC.html, config file is reread automatically */
        /* bbhosts - hobbitlaunch(8), a config file is being monitored for changes but not sure if it is this one? */
        /* bootconf - no daemon */
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
