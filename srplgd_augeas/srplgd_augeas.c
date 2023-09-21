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

#define _GNU_SOURCE

#include "srplgda_config.h"

#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include <libyang/libyang.h>
#include <sysrepo.h>

#include "srplgda_common.h"

#define PLG_NAME "srplgd_augeas"

static int
aug_service_change_cb(sr_session_ctx_t *UNUSED(session), uint32_t UNUSED(sub_id), const char *UNUSED(module_name),
        const char *UNUSED(xpath), sr_event_t event, uint32_t UNUSED(request_id), void *private_data)
{
    const char *service_name = private_data;
    int r;

    if (event != SR_EV_DONE) {
        return SR_ERR_OK;
    }

    if ((r = aug_execl(PLG_NAME, SYSTEMCTL_EXECUTABLE, "try-restart", service_name, NULL))) {
        return r;
    }
    return SR_ERR_OK;
}

#ifdef ACTIVEMQ_EXECUTABLE

static int
aug_actimemq_change_cb(sr_session_ctx_t *UNUSED(session), uint32_t UNUSED(sub_id), const char *UNUSED(module_name),
        const char *UNUSED(xpath), sr_event_t event, uint32_t UNUSED(request_id), void *UNUSED(private_data))
{
    if (event != SR_EV_DONE) {
        return SR_ERR_OK;
    }

    /* TODO activemq service */
    return aug_execl(PLG_NAME, ACTIVEMQ_EXECUTABLE, "restart", NULL);
}

#endif

#ifdef AVAHI_DAEMON_EXECUTABLE

static int
aug_avahi_change_cb(sr_session_ctx_t *UNUSED(session), uint32_t UNUSED(sub_id), const char *UNUSED(module_name),
        const char *UNUSED(xpath), sr_event_t event, uint32_t UNUSED(request_id), void *UNUSED(private_data))
{
    int r;

    if (event != SR_EV_DONE) {
        return SR_ERR_OK;
    }

    /* TODO avahi-daemon service */
    if ((r = aug_execl(PLG_NAME, AVAHI_DAEMON_EXECUTABLE, "--kill", NULL))) {
        return r;
    }
    return aug_execl(PLG_NAME, AVAHI_DAEMON_EXECUTABLE, "--syslog", "--daemonize", NULL);
}

#endif

#ifdef CACHEFILESD_EXECUTABLE

static int
aug_cachefilesd_change_cb(sr_session_ctx_t *UNUSED(session), uint32_t UNUSED(sub_id), const char *UNUSED(module_name),
        const char *UNUSED(xpath), sr_event_t event, uint32_t UNUSED(request_id), void *UNUSED(private_data))
{
    int r;
    pid_t pid;

    if (event != SR_EV_DONE) {
        return SR_ERR_OK;
    }

    /* TODO cachefilesd service */
    if ((r = aug_pidfile(PLG_NAME, "/var/run/cachefilesd.pid", &pid))) {
        return r;
    }
    if (!pid) {
        /* daemon not running */
        return SR_ERR_OK;
    }

    return aug_send_sig(PLG_NAME, pid, SIGHUP);
}

#endif

#ifdef CARBON_SERVICES

static int
aug_carbon_change_cb(sr_session_ctx_t *UNUSED(session), uint32_t UNUSED(sub_id), const char *UNUSED(module_name),
        const char *UNUSED(xpath), sr_event_t event, uint32_t UNUSED(request_id), void *UNUSED(private_data))
{
    int r;

    if (event != SR_EV_DONE) {
        return SR_ERR_OK;
    }

    /* service files on github https://github.com/graphite-project/carbon/tree/master/distro/redhat/init.d */
    if ((r = aug_execl(PLG_NAME, SYSTEMCTL_EXECUTABLE, "try-restart", "carbon-cache", NULL))) {
        return r;
    }
    if ((r = aug_execl(PLG_NAME, SYSTEMCTL_EXECUTABLE, "try-restart", "carbon-relay", NULL))) {
        return r;
    }
    if ((r = aug_execl(PLG_NAME, SYSTEMCTL_EXECUTABLE, "try-restart", "carbon-aggregator", NULL))) {
        return r;
    }
    return SR_ERR_OK;
}

#endif

#ifdef CLAMAV_SERVICES

static int
aug_clamav_change_cb(sr_session_ctx_t *UNUSED(session), uint32_t UNUSED(sub_id), const char *UNUSED(module_name),
        const char *UNUSED(xpath), sr_event_t event, uint32_t UNUSED(request_id), void *UNUSED(private_data))
{
    int r;

    if (event != SR_EV_DONE) {
        return SR_ERR_OK;
    }

    if ((r = aug_execl(PLG_NAME, SYSTEMCTL_EXECUTABLE, "try-restart", "clamav-daemon", NULL))) {
        return r;
    }
    if ((r = aug_execl(PLG_NAME, SYSTEMCTL_EXECUTABLE, "try-restart", "clamav-freshclam", NULL))) {
        return r;
    }
    return SR_ERR_OK;
}

#endif

#ifdef DHCPD_EXECUTABLE

static int
aug_dhcpd_change_cb(sr_session_ctx_t *UNUSED(session), uint32_t UNUSED(sub_id), const char *UNUSED(module_name),
        const char *UNUSED(xpath), sr_event_t event, uint32_t UNUSED(request_id), void *UNUSED(private_data))
{
    int r;
    pid_t pid;

    if (event != SR_EV_DONE) {
        return SR_ERR_OK;
    }

    /* TODO on Ubuntu service isc-dhcp-server with PID file /run/dhcp-server/dhcpd.pid */
    if ((r = aug_pidfile(PLG_NAME, "/var/run/dhcpd.pid", &pid))) {
        return r;
    }
    if (!pid) {
        /* daemon not running */
        return SR_ERR_OK;
    }

    /* terminate and restart manually (see dhcpd(8)) */
    if ((r = aug_send_sig(PLG_NAME, pid, SIGTERM))) {
        return r;
    }
    if ((r = aug_execl(PLG_NAME, DHCPD_EXECUTABLE, NULL))) {
        return r;
    }
    return SR_ERR_OK;
}

#endif

#ifdef EXPORTFS_EXECUTABLE

static int
aug_exports_change_cb(sr_session_ctx_t *UNUSED(session), uint32_t UNUSED(sub_id), const char *UNUSED(module_name),
        const char *UNUSED(xpath), sr_event_t event, uint32_t UNUSED(request_id), void *UNUSED(private_data))
{
    if (event != SR_EV_DONE) {
        return SR_ERR_OK;
    }

    return aug_execl(PLG_NAME, EXPORTFS_EXECUTABLE, "-ra", NULL);
}

#endif

static int
aug_ldso_change_cb(sr_session_ctx_t *UNUSED(session), uint32_t UNUSED(sub_id), const char *UNUSED(module_name),
        const char *UNUSED(xpath), sr_event_t event, uint32_t UNUSED(request_id), void *UNUSED(private_data))
{
    if (event != SR_EV_DONE) {
        return SR_ERR_OK;
    }

    return aug_execl(PLG_NAME, "/sbin/ldconfig", NULL);
}

#ifdef NETPLAN_EXECUTABLE

static int
aug_netplan_change_cb(sr_session_ctx_t *UNUSED(session), uint32_t UNUSED(sub_id), const char *UNUSED(module_name),
        const char *UNUSED(xpath), sr_event_t event, uint32_t UNUSED(request_id), void *UNUSED(private_data))
{
    if (event != SR_EV_DONE) {
        return SR_ERR_OK;
    }

    return aug_execl(PLG_NAME, NETPLAN_EXECUTABLE, "apply", NULL);
}

#endif

#ifdef PG_CTL_EXECUTABLE

static int
aug_pg_hba_change_cb(sr_session_ctx_t *UNUSED(session), uint32_t UNUSED(sub_id), const char *UNUSED(module_name),
        const char *UNUSED(xpath), sr_event_t event, uint32_t UNUSED(request_id), void *UNUSED(private_data))
{
    if (event != SR_EV_DONE) {
        return SR_ERR_OK;
    }

    return aug_execl(PLG_NAME, PG_CTL_EXECUTABLE, "reload", NULL);
}

#endif

#ifdef POSTMAP_EXECUTABLE

static int
aug_postmap_change_cb(sr_session_ctx_t *UNUSED(session), uint32_t UNUSED(sub_id), const char *UNUSED(module_name),
        const char *UNUSED(xpath), sr_event_t event, uint32_t UNUSED(request_id), void *private_data)
{
    const char *file_name = private_data;
    char *path;
    int r;

    if (event != SR_EV_DONE) {
        return SR_ERR_OK;
    }

    if (asprintf(&path, "/etc/postfix/%s", file_name) == -1) {
        return SR_ERR_NO_MEMORY;
    }
    r = aug_execl(PLG_NAME, POSTMAP_EXECUTABLE, path, NULL);
    free(path);
    if (r) {
        return r;
    }

    if (asprintf(&path, "/usr/local/etc/postfix/%s", file_name) == -1) {
        return SR_ERR_NO_MEMORY;
    }
    r = aug_execl(PLG_NAME, POSTMAP_EXECUTABLE, path, NULL);
    free(path);
    if (r) {
        return r;
    }

    return SR_ERR_OK;
}

#endif

#ifdef POSTFIX_EXECUTABLE

static int
aug_postfix_change_cb(sr_session_ctx_t *UNUSED(session), uint32_t UNUSED(sub_id), const char *UNUSED(module_name),
        const char *UNUSED(xpath), sr_event_t event, uint32_t UNUSED(request_id), void *UNUSED(private_data))
{
    if (event != SR_EV_DONE) {
        return SR_ERR_OK;
    }

    return aug_execl(PLG_NAME, POSTFIX_EXECUTABLE, "reload", NULL);
}

#endif

#ifdef RTADVD_EXECUTABLE

static int
aug_rtadvd_change_cb(sr_session_ctx_t *UNUSED(session), uint32_t UNUSED(sub_id), const char *UNUSED(module_name),
        const char *UNUSED(xpath), sr_event_t event, uint32_t UNUSED(request_id), void *UNUSED(private_data))
{
    int r;
    pid_t pid;

    if (event != SR_EV_DONE) {
        return SR_ERR_OK;
    }

    if ((r = aug_pidfile(PLG_NAME, "/var/run/rtadvd.pid", &pid))) {
        return r;
    }
    if (!pid) {
        /* daemon not running */
        return SR_ERR_OK;
    }

    /* rtadvd(8) */
    if ((r = aug_send_sig(PLG_NAME, pid, SIGHUP))) {
        return r;
    }

    return SR_ERR_OK;
}

#endif

#ifdef SMBCONTROL_EXECUTABLE

static int
aug_samba_change_cb(sr_session_ctx_t *UNUSED(session), uint32_t UNUSED(sub_id), const char *UNUSED(module_name),
        const char *UNUSED(xpath), sr_event_t event, uint32_t UNUSED(request_id), void *UNUSED(private_data))
{
    if (event != SR_EV_DONE) {
        return SR_ERR_OK;
    }

    /* ignore return value in case the daemons are not running */
    aug_execl(PLG_NAME, SMBCONTROL_EXECUTABLE, "reload-config", "nmbd", NULL);
    aug_execl(PLG_NAME, SMBCONTROL_EXECUTABLE, "reload-config", "smbd", NULL);
    aug_execl(PLG_NAME, SMBCONTROL_EXECUTABLE, "reload-config", "winbindd", NULL);

    return SR_ERR_OK;
}

#endif

#ifdef SYSCTL_EXECUTABLE

static int
aug_sysctl_change_cb(sr_session_ctx_t *UNUSED(session), uint32_t UNUSED(sub_id), const char *UNUSED(module_name),
        const char *UNUSED(xpath), sr_event_t event, uint32_t UNUSED(request_id), void *UNUSED(private_data))
{
    if (event != SR_EV_DONE) {
        return SR_ERR_OK;
    }

    /* load kernel parameters from the config file */
    return aug_execl(PLG_NAME, SYSCTL_EXECUTABLE, "--load", NULL);
}

#endif

#ifdef WEBMIN_EXECUTABLE

static int
aug_webmin_change_cb(sr_session_ctx_t *UNUSED(session), uint32_t UNUSED(sub_id), const char *UNUSED(module_name),
        const char *UNUSED(xpath), sr_event_t event, uint32_t UNUSED(request_id), void *UNUSED(private_data))
{
    if (event != SR_EV_DONE) {
        return SR_ERR_OK;
    }

    return aug_execl(PLG_NAME, "/etc/webmin/restart", NULL);
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

    sr_session_switch_ds(session, SR_DS_STARTUP);

    /* subscribe to the found supported modules */
    ly_ctx = sr_session_acquire_context(session);
    i = ly_ctx_internal_modules_count(ly_ctx);
    while ((ly_mod = ly_ctx_get_module_iter(ly_ctx, &i))) {
        if (!strcmp(ly_mod->name, "activemq-conf") || !strcmp(ly_mod->name, "activemq-xml") ||
                !strcmp(ly_mod->name, "jmxaccess") || !strcmp(ly_mod->name, "jmxpassword")) {
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
        } else if (!strcmp(ly_mod->name, "cgconfig") || !strcmp(ly_mod->name, "cgrules")) {
#ifdef CGCONFIG_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "cgconfig", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "chrony")) {
#ifdef CHRONY_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "chrony", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "clamav")) {
#ifdef CLAMAV_SERVICES
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_clamav_change_cb, NULL, 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "cockpit")) {
#ifdef COCKPIT_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "cockpit", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "collectd")) {
#ifdef COLLECTD_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "collectd", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "cron_user") || !strcmp(ly_mod->name, "cron")) {
#ifdef CRON_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "cron", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "cups")) {
#ifdef CUPS_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "cups", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "cyrus-imapd")) {
#ifdef CYRUS_IMAPD_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "cyrus-imapd", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "darkice")) {
#ifdef DARKICE_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "darkice", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "devfsrules")) {
#ifdef DEVFS_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "devfs", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "dhcpd")) {
#ifdef DHCPD_EXECUTABLE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_dhcpd_change_cb, NULL, 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "dnsmasq")) {
#ifdef DNSMASQ_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "dnsmasq", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "dovecot")) {
#ifdef DOVECOT_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "dovecot", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "exports")) {
#ifdef EXPORTFS_EXECUTABLE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_exports_change_cb, NULL, 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "fail2ban")) {
#ifdef FAIL2BAN_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "fail2ban", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "httpd")) {
#ifdef HTTPD_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "httpd", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "iscsid")) {
#ifdef ISCSID_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "iscsid", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "kdump")) {
#ifdef KDUMP_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "kdump", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "keepalived")) {
#ifdef KEEPALIVED_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "keepalived", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "ldif") || !strcmp(ly_mod->name, "slapd")) {
#ifdef SLAPD_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "slapd", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "ldso")) {
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_ldso_change_cb, NULL, 0, 0, &subscr);
        } else if (!strcmp(ly_mod->name, "lightdm")) {
#ifdef LIGHTDM_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "lightdm", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "logrotate")) {
#ifdef LOGROTATE_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "logrotate", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "mailscanner_rules") || !strcmp(ly_mod->name, "mailscanner")) {
#ifdef MAILSCANNER_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "MailScanner", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "mcollective")) {
#ifdef MCOLLECTIVE_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "mcollective", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "memcached")) {
#ifdef MEMCACHED_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "memcached", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "mongodbserver")) {
#ifdef MONGOD_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "mongod", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "monit")) {
#ifdef MONIT_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "monit", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "multipath")) {
#ifdef MULTIPATHD_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "multipathd", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "mysql")) {
#ifdef MYSQL_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "mysql", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "nagioscfg") || !strcmp(ly_mod->name, "nagiosobjects") ||
                !strcmp(ly_mod->name, "nrpe")) {
#ifdef NAGIOS_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "nagios", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "netplan")) {
#ifdef NETPLAN_EXECUTABLE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_netplan_change_cb, NULL, 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "nginx")) {
#ifdef NGINX_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "nginx", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "nslcd")) {
#ifdef NSLCD_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "nslcd", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "ntp") || !strcmp(ly_mod->name, "ntpd")) {
#ifdef NTPD_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "ntpd", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "opendkim")) {
#ifdef OPENDKIM_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "opendkim", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "openvpn")) {
#ifdef OPENVPN_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "openvpn.target", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "pagekite")) {
#ifdef PAGEKITE_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "pagekite", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "pg_hba") || !strcmp(ly_mod->name, "postgresql")) {
#ifdef PG_CTL_EXECUTABLE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_pg_hba_change_cb, NULL, 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "pgbouncer")) {
#ifdef PGBOUNCER_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "pgbouncer", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "postfix_access") || !strcmp(ly_mod->name, "postfix_passwordmap")) {
#ifdef POSTMAP_EXECUTABLE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_postmap_change_cb, "access", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "postfix_main") || !strcmp(ly_mod->name, "postfix_master")) {
#ifdef POSTFIX_EXECUTABLE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_postfix_change_cb, NULL, 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "postfix_sasl_smtpd")) {
#ifdef SASLAUTHD_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "postfix_sasl_smtpd", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "postfix_transport")) {
#ifdef POSTMAP_EXECUTABLE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_postmap_change_cb, "transport", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "postfix_virtual")) {
#ifdef POSTMAP_EXECUTABLE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_postmap_change_cb, "virtual", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "puppet_auth") || !strcmp(ly_mod->name, "puppet") ||
                !strcmp(ly_mod->name, "puppetfileserver") || !strcmp(ly_mod->name, "trapperkeeper")) {
#ifdef PUPPET_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "puppet", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "qpid")) {
#ifdef QPIDD_EXECUTABLE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "qpidd", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "rabbitmq")) {
#ifdef RABBITMQ_SERVER_EXECUTABLE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "rabbitmq-server", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "radicale")) {
#ifdef RADICALE_EXECUTABLE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "radicale", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "redis")) {
#ifdef REDIS_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "redis.target", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "rsyncd")) {
#ifdef RSYNCD_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "rsyncd", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "rsyslog")) {
#ifdef RSYSLOG_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "rsyslog", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "rtadvd")) {
#ifdef RTADVD_EXECUTABLE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_rtadvd_change_cb, NULL, 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "samba") || !strcmp(ly_mod->name, "smbusers")) {
#ifdef SMBCONTROL_EXECUTABLE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_samba_change_cb, NULL, 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "sip_conf")) {
#ifdef ASTERISK_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "asterisk", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "splunk")) {
#ifdef SPLUNK_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "splunk", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "squid")) {
#ifdef SQUID_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "squid", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "sshd")) {
#ifdef SSHD_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "sshd", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "sssd")) {
#ifdef SSSD_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "sssd", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "strongswan")) {
#ifdef STRONGSWAN_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "strongswan", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "stunnel")) {
#ifdef STUNNEL_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "stunnel", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "sysctl")) {
#ifdef SYSCTL_EXECUTABLE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_sysctl_change_cb, NULL, 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "syslog")) {
#ifdef SYSLOG_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "syslog", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "thttpd")) {
#ifdef THTTPD_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "thttpd", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "tinc")) {
#ifdef TINC_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "tinc", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "tmpfiles")) {
#ifdef SYSTEMD_TMPFILES_CLEAN_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "systemd-tmpfiles-clean",
                    0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "tuned")) {
#ifdef TUNED_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "tuned", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "vsftpd")) {
#ifdef VSFTPD_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "vsftpd", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "webmin")) {
#ifdef WEBMIN_EXECUTABLE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_webmin_change_cb, NULL, 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "xinetd")) {
#ifdef XINETD_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "xinetd", 0, 0, &subscr);
#endif
        } else if (!strcmp(ly_mod->name, "xymon")) {
#ifdef XYMONLAUNCH_SERVICE
            rc = sr_module_change_subscribe(session, ly_mod->name, NULL, aug_service_change_cb, "xymonlaunch", 0, 0, &subscr);
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
        /* ceph - https://ubuntu.com/ceph/docs/client-setup, only client, no daemon? */
        /* channels - no daemon? */
        /* cmdline - kernel command-line parameters */
        /* cobblermodules, cobblersettings - package manager, no daemon */
        /* cpanel - not able to find any relevant info? */
        /* crypttab - systemd-cryptsetup@.service(8) service needs generated service files on boot */
        /* desktop - lots of affected applications */
        /* device_map - grub configuration */
        /* dhclient - should work as a service but not sure what service to restart? */
        /* dns_zone - no specific process to use the files */
        /* dpkg - no daemon */
        /* dput - no daemon */
        /* ethers - ethers(5), no (specific) daemon */
        /* fai_diskconfig - installation configuration */
        /* fonts - no daemon */
        /* fstab - no daemon */
        /* fuse - no daemon */
        /* gdm - has daemon but restarting it causes all users to log out */
        /* getcap - no daemon */
        /* group - would cause log out */
        /* grub - no daemon */
        /* grubenv - no daemon */
        /* gshadow - would cause log out */
        /* gtkbookmarks - applied as needed? */
        /* host_conf - no daemon */
        /* hostname - no daemon */
        /* hosts_access - tcpd(8), used only by other daemons? */
        /* hosts - hosts(5), no daemon */
        /* htpasswd - restart httpd, rsyncd? */
        /* inetd - inetd(8), should restart it? */
        /* inittab - applied on next boot */
        /* inputrc - readline(3), no daemon */
        /* interaces - interfaces(5), specific inetrfaces would need to be disabled and enabled */
        /* iproute2 - ip-route(8), no simple way of applying changes */
        /* iptables - iptables(8), some changes should be possible to apply with iptables-restore */
        /* jaas - not sure if has any daemon */
        /* jettyrealm - Java app */
        /* known_hosts - no daemon */
        /* koji - several daemons, need restart? */
        /* krb5 - service name(s) differs across distributions? */
        /* limits - limits.conf(5), no daemon */
        /* login_defs - applied when creating new users */
        /* logwatch - executed by cron */
        /* lokkit - interactive configuration */
        /* lvm - not a good idea to restart the manager */
        /* masterpasswd - no deamon */
        /* mdadm_conf - requires a restart */
        /* mke2fs - no daemon */
        /* modprobe - default options for modprobe exec, rather leave it to the user */
        /* modules_conf - default options for modprobe exec */
        /* modules - read on boot */
        /* netmasks - specific interface restart required */
        /* networkmanager - better not restart it */
        /* networks - no daemon */
        /* nsswitch - no single daemon */
        /* odbc - no daemon */
        /* openshift_config - many managed projects */
        /* openshift_http - managed by openshift? */
        /* openshift_quickstarts - applied on start */
        /* oz - no daemon */
        /* pam - no daemon */
        /* pamconf - no daemon */
        /* passwd - no daemon */
        /* pbuilder - no daemon */
        /* php - no daemon */
        /* phpvars - squirrelmail, a web service */
        /* protocols - no daemon */
        /* pylonspaste - no single daemon? */
        /* pythonpaste - no single daemon? */
        /* rancid - no daemon */
        /* resolv - no daemon */
        /* rhsm - Java apps? */
        /* rmt - no daemon */
        /* schroot - applied on next schroot access */
        /* securetty - applied on next login */
        /* semanage - library configuration */
        /* services - library configuration */
        /* shadow - no daemon */
        /* shells - no daemon */
        /* shellvars_list - no daemon */
        /* shellvars - many config files */
        /* simplelines - files reread on use */
        /* simplevars - many config files */
        /* solaris_system - no daemon */
        /* soma - could not find info? */
        /* sos - no daemon */
        /* spacevars - many config files */
        /* ssh - no daemon */
        /* star - no daemon */
        /* subversion - no daemon */
        /* sudoers - no daemon */
        /* sysconfig_route - restart NetworkManager service? */
        /* systemd - restart the changed services? */
        /* termcap - no daemon */
        /* up2date - no daemons? */
        /* updatedb - updatedb(8) run preiodically */
        /* vfstab - no daemon */
        /* vmware_config - applied automatically? */
        /* xml - too generic */
        /* xorg - Xorg(1), system needs restart */
        /* xymon_alerting - no daemon? */
        /* yum - no daemon? */
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
