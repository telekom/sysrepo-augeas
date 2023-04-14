set(SRPLGD_AUGEAS_SRC
    srplgd_augeas.c)

# augeas sysrepo-plugind plugin
add_library(srplgd_augeas MODULE ${SRPLGD_AUGEAS_SRC})
set_target_properties(srplgd_augeas PROPERTIES PREFIX "")

# dependencies - sysrepo
target_link_libraries(srplgd_augeas ${SYSREPO_LIBRARIES})

# programs
find_program(ACTIVEMQ_EXECUTABLE "activemq")

configure_file("srplgda_config.h.in" "${PROJECT_BINARY_DIR}/srplgda_config.h" ESCAPE_QUOTES @ONLY)

set(format_sources ${SRPLGD_AUGEAS_SRC} PARENT_SCOPE)
