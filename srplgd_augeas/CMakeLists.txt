# systemctl
find_program(SYSTEMCTL_EXECUTABLE "systemctl" REQUIRED)

function(find_service FOUND SERVICE)
    # check service exists
    execute_process(COMMAND ${SYSTEMCTL_EXECUTABLE} status ${SERVICE} RESULT_VARIABLE RET OUTPUT_QUIET ERROR_QUIET)
    if(${RET})
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

find_program(EXPORTFS_EXECUTABLE "exportfs")
find_program_msg(${EXPORTFS_EXECUTABLE} "exportfs")

find_service(FAIL2BAN_SERVICE "fail2ban")

configure_file("srplgda_config.h.in" "${PROJECT_BINARY_DIR}/srplgda_config.h" ESCAPE_QUOTES @ONLY)

set(format_sources ${format_sources} ${SRPLGD_AUGEAS_SRC} PARENT_SCOPE)