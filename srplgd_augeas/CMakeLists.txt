# systemctl
find_program(SYSTEMCTL_EXECUTABLE "systemctl" REQUIRED)

function(find_service FOUND SERVICE)
    # check service exists
    execute_process(COMMAND ${SYSTEMCTL_EXECUTABLE} status ${SERVICE} RESULT_VARIABLE RET OUTPUT_QUIET ERROR_QUIET)
    if(${RET})
        message(STATUS "Service ${SERVICE} - not found")
        set(${FOUND} FALSE PARENT_SCOPE)
    else()
        message(STATUS "Service ${SERVICE} - found")
        set(${FOUND} TRUE PARENT_SCOPE)
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
    srplgd_augeas.c
    srplgda_common.c)

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

configure_file("srplgda_config.h.in" "${PROJECT_BINARY_DIR}/srplgda_config.h" ESCAPE_QUOTES @ONLY)

set(format_sources ${SRPLGD_AUGEAS_SRC} PARENT_SCOPE)
