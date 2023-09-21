# systemctl
find_program(SYSTEMCTL_EXECUTABLE "systemctl" REQUIRED)

function(find_service FOUND SERVICE)
    # check service exists
    execute_process(COMMAND ${SYSTEMCTL_EXECUTABLE} status ${SERVICE} RESULT_VARIABLE RET OUTPUT_QUIET ERROR_QUIET)
    if(${RET} EQUAL 4)
        # leave ${FOUND} var undefined
        message(STATUS "Service ${SERVICE} - not found")
    else()
        message(STATUS "Service ${SERVICE} - found")
        set(${FOUND} 1 PARENT_SCOPE)
    endif()
endfunction()

function(find_program_msg VAR PROGRAM)
    if(VAR)
        message(STATUS "Program ${PROGRAM} - found (${VAR})")
    else()
        message(STATUS "Program ${PROGRAM} - not found")
    endif()
endfunction()

set(SRPLGD_AUGEAS_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/srplgd_augeas.c
    ${CMAKE_CURRENT_SOURCE_DIR}/srplgda_common.c)

# augeas sysrepo-plugind plugin
add_library(srplgd_augeas MODULE ${SRPLGD_AUGEAS_SRC})
set_target_properties(srplgd_augeas PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")

# dependencies - sysrepo
target_link_libraries(srplgd_augeas ${SYSREPO_LIBRARIES})

# programs and services
find_program(ACTIVEMQ_EXECUTABLE "activemq")
find_program_msg(${ACTIVEMQ_EXECUTABLE} "activemq")
find_program(AVAHI_DAEMON_EXECUTABLE "avahi-daemon")
find_program_msg(${AVAHI_DAEMON_EXECUTABLE} "avahi-daemon")
find_program(CACHEFILESD_EXECUTABLE "cachefilesd")
find_program_msg(${CACHEFILESD_EXECUTABLE} "cachefilesd")

find_service(CARBON_CACHE_SERVICE "carbon-cache")
find_service(CARBON_RELAY_SERVICE "carbon-relay")
find_service(CARBON_AGGREGATOR_SERVICE "carbon-aggregator")
if(CARBON_CACHE_SERVICE AND CARBON_RELAY_SERVICE AND CARBON_AGGREGATOR_SERVICE)
    set(CARBON_SERVICES 1)
endif()

find_service(CGCONFIG_SERVICE "cgconfig")
find_service(CHRONY_SERVICE "chrony")

find_service(CLAMAV_DAEMON_SERVICE "clamav-daemon")
find_service(CLAMAV_FRESHCLAM_SERVICE "clamav-freshclam")
if(CLAMAV_DAEMON_SERVICE AND CLAMAV_FRESHCLAM_SERVICE)
    set(CLAMAV_SERVICES 1)
endif()

find_service(COCKPIT_SERVICE "cockpit")
find_service(COLLECTD_SERVICE "collectd")
find_service(CRON_SERVICE "cron")
find_service(CUPS_SERVICE "cups")
find_service(DARKICE_SERVICE "darkice")
find_service(DEVFS_SERVICE "devfs")

find_program(DHCPD_EXECUTABLE "dhcpd")
find_program_msg(${DHCPD_EXECUTABLE} "dhcpd")

find_service(DOVECOT_SERVICE "dovecot")
find_service(DNSMASQ_SERVICE "dnsmasq")

find_program(EXPORTFS_EXECUTABLE "exportfs")
find_program_msg(${EXPORTFS_EXECUTABLE} "exportfs")

find_service(FAIL2BAN_SERVICE "fail2ban")
find_service(HTTPD_SERVICE "httpd")
find_service(ISCSID_SERVICE "iscsid")
find_service(KDUMP_SERVICE "kdump")
find_service(KEEPALIVED_SERVICE "keepalived")
find_service(SLAPD_SERVICE "slapd")
find_service(LIGHTDM_SERVICE "lightdm")
find_service(LOGROTATE_SERVICE "logrotate")
find_service(MAILSCANNER_SERVICE "MailScanner")
find_service(MCOLLECTIVE_SERVICE "mcollective")
find_service(MEMCACHED_SERVICE "memcached")
find_service(MONGOD_SERVICE "mongod")
find_service(MONIT_SERVICE "monit")
find_service(MULTIPATHD_SERVICE "multipathd")
find_service(MYSQL_SERVICE "mysql")
find_service(NAGIOS_SERVICE "nagios")

find_program(NETPLAN_EXECUTABLE "netplan")
find_program_msg(${NETPLAN_EXECUTABLE} "netplan")

find_service(NGINX_SERVICE "nginx")
find_service(NSLCD_SERVICE "nslcd")
find_service(NTPD_SERVICE "ntpd")
find_service(OPENDKIM_SERVICE "opendkim")
find_service(OPENVPN_SERVICE "openvpn.target")
find_service(PAGEKITE_SERVICE "pagekite")

find_program(PG_CTL_EXECUTABLE "pg_ctl")
find_program_msg(${PG_CTL_EXECUTABLE} "pg_ctl")

find_service(PGBOUNCER_SERVICE "pgbouncer")

find_program(POSTMAP_EXECUTABLE "postmap")
find_program_msg(${POSTMAP_EXECUTABLE} "postmap")

find_program(POSTFIX_EXECUTABLE "postfix")
find_program_msg(${POSTFIX_EXECUTABLE} "postfix")

find_service(SASLAUTHD_SERVICE "saslauthd")
find_service(PUPPET_SERVICE "puppet")
find_service(QPIDD_SERVICE "qpidd")
find_service(RABBITMQ_SERVER_SERVICE "rabbitmq-server")
find_service(RADICALE_SERVICE "radicale")
find_service(REDIS_SERVICE "redis.target")
find_service(RSYNCD_SERVICE "rsyncd")
find_service(RSYSLOG_SERVICE "rsyslog")

find_program(RTADVD_EXECUTABLE "rtadvd")
find_program_msg(${RTADVD_EXECUTABLE} "rtadvd")

find_program(SMBCONTROL_EXECUTABLE "smbcontrol")
find_program_msg(${SMBCONTROL_EXECUTABLE} "smbcontrol")

find_service(ASTERISK_SERVICE "asterisk")
find_service(SPLUNK_SERVICE "splunk")
find_service(SPLUNK_SERVICE "squid")
find_service(SSHD_SERVICE "sshd")
find_service(SSSD_SERVICE "sssd")
find_service(SSSD_SERVICE "strongswan")
find_service(SSSD_SERVICE "stunnel")

find_program(SYSCTL_EXECUTABLE "sysctl")
find_program_msg(${SYSCTL_EXECUTABLE} "sysctl")

find_service(SYSLOG_SERVICE "syslog")
find_service(THTTPD_SERVICE "thttpd")
find_service(TINC_SERVICE "tinc")
find_service(SYSTEMD_TMPFILES_CLEAN_SERVICE "systemd-tmpfiles-clean")
find_service(TUNED_SERVICE "tuned")
find_service(VSFTPD_SERVICE "vsftpd")

find_program(WEBMIN_EXECUTABLE "webmin")
find_program_msg(${WEBMIN_EXECUTABLE} "webmin")

find_service(XINETD_SERVICE "xinetd")
find_service(XYMONLAUNCH_SERVICE "xymonlaunch")

configure_file("srplgda_config.h.in" "${PROJECT_BINARY_DIR}/srplgda_config.h" ESCAPE_QUOTES @ONLY)

set(format_sources ${format_sources} ${SRPLGD_AUGEAS_SRC} PARENT_SCOPE)
