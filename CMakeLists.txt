cmake_minimum_required(VERSION 2.8.12)

# force out-of-source build
if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
    message(FATAL_ERROR "In-source build is not allowed. Please make a standalone build directory and run CMake from there. You may need to remove CMakeCache.txt.")
endif()

project(augyang C)

# include custom Modules
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/CMakeModules/")

include(ExternalProject)
include(SourceFormat)

# osx specific
set(CMAKE_MACOSX_RPATH TRUE)

# set default build type if not specified by user
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# normalize build type string
string(TOUPPER "${CMAKE_BUILD_TYPE}" BUILD_TYPE_UPPER)
if ("${BUILD_TYPE_UPPER}" STREQUAL "RELEASE")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build Type" FORCE)
    set(CMAKE_C_FLAGS "-DNDEBUG -O2 ${CMAKE_C_FLAGS}")
elseif("${BUILD_TYPE_UPPER}" STREQUAL "DEBUG")
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build Type" FORCE)
    set(CMAKE_C_FLAGS "-g3 -O0 ${CMAKE_C_FLAGS}")
elseif("${BUILD_TYPE_UPPER}" STREQUAL "RELWITHDEBINFO")
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build Type" FORCE)
elseif("${BUILD_TYPE_UPPER}" STREQUAL "RELWITHDEBUG")
    set(CMAKE_BUILD_TYPE "RelWithDebug" CACHE STRING "Build Type" FORCE)
endif()

# check the supported platform
if(NOT UNIX)
    message(FATAL_ERROR "Only *nix like systems are supported.")
endif()

# correct RPATH usage on OS X
set(CMAKE_MACOSX_RPATH TRUE)

# keep all binaries in the build directory
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# set version(s) of the project
set(AUGYANG_VERSION 1.0.0)
set(SRDS_AUGEAS_VERSION 1.0.0)

# version of libyang that this project depends on
set(LIBYANG_DEP_VERSION 2.0.104)
set(LIBYANG_DEP_SOVERSION 2.13.0)
set(LIBYANG_DEP_SOVERSION_MAJOR 2)

# global C flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -std=c11")

#
# sources
#
set(SRDS_AUGEAS_SRC
    ${PROJECT_SOURCE_DIR}/plugins/srds_augeas.c
    ${PROJECT_SOURCE_DIR}/plugins/srdsa_init.c
    ${PROJECT_SOURCE_DIR}/plugins/srdsa_load.c
    ${PROJECT_SOURCE_DIR}/plugins/srdsa_store.c
    ${PROJECT_SOURCE_DIR}/plugins/srdsa_common.c)

set(AY_STARTUP_SRC
    ${PROJECT_SOURCE_DIR}/plugins/ay_startup.c)

set(AUGYANG_SRC
    src/main.c
    src/augyang.c)

# source files to be covered by the 'format' target
set(format_sources
    ${SRDS_AUGEAS_SRC}
    ${AUGYANG_SRC})

#
# options
#

if(("${BUILD_TYPE_UPPER}" STREQUAL "DEBUG") OR ("${BUILD_TYPE_UPPER}" STREQUAL "RELWITHDEBINFO"))
    option(ENABLE_TESTS "Build tests" ON)
    option(ENABLE_VALGRIND_TESTS "Build tests with valgrind" ON)
else()
    option(ENABLE_TESTS "Build tests" OFF)
    option(ENABLE_VALGRIND_TESTS "Build tests with valgrind" OFF)
endif()
set(SUPPORTED_LENSES "anaconda backuppchosts ceph cmdline cpanel cron darkice devfsrules device_map dhclient dnsmasq ethers gtkbookmarks grubenv hostname hosts inittab inputrc iproute2 iptables iscsid login_defs monit ntp ntpd pam passwd pg_hba postfix_access postfix_sasl_smtpd qpid rmt rtadvd securetty simplelines simplevars smbusers ssh sshd star systemd up2date vmware_config xendconfsxp xymon"
        CACHE STRING "Space-separated list of Augeas lenses to be supported in sysrepo, by default all of them")
option(INSTALL_MODULES "Install supported Augeas lens YANG modules into sysrepo" ON)
set(YANG_MODULE_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/yang/modules/augyang" CACHE STRING "Directory where to copy the generated YANG modules to")

#
# checks
#
if(ENABLE_VALGRIND_TESTS)
    if(NOT ENABLE_TESTS)
        message(WARNING "Tests are disabled, disabling memory leak tests")
        set(ENABLE_VALGRIND_TESTS OFF)
    else()
        find_program(VALGRIND_FOUND valgrind)
        if(NOT VALGRIND_FOUND)
            message(WARNING "valgrind executable not found, disabling memory leaks tests")
            set(ENABLE_VALGRIND_TESTS OFF)
        endif()
    endif()
endif()

if(ENABLE_TESTS)
    find_package(CMocka 1.0.1)
    if(NOT CMOCKA_FOUND)
        message(STATUS "Disabling tests because of missing CMocka")
        set(ENABLE_TESTS OFF)
    endif()
endif()

# local augeas lib paths
set(AUGEAS_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/augeas)
set(AUGEAS_DIR ${AUGEAS_PREFIX}/src/augeas_ext)
set(AUGEAS_SRC_DIR ${AUGEAS_PREFIX}/src/augeas_ext-build/src)

# local GNU lib paths
set(GNULIB_DIR ${AUGEAS_DIR}/.gnulib)

if ("${BUILD_TYPE_UPPER}" STREQUAL "DEBUG")
    # enable before adding tests to let them detect that format checking is available - one of the tests is format checking
    source_format_enable()
endif()

#
# targets
#

# augeas sysrepo DS plugin
add_library(srds_augeas MODULE ${SRDS_AUGEAS_SRC})
set_target_properties(srds_augeas PROPERTIES PREFIX "")

# augyang startup config print utility
add_executable(ay_startup ${AY_STARTUP_SRC})
include_directories(${PROJECT_BINARY_DIR})

# augyang executable
add_executable(augyang ${AUGYANG_SRC})
target_compile_options(augyang PRIVATE "-std=gnu11")
include_directories(${PROJECT_SOURCE_DIR}/src)

#
# dependencies
#

# augeas
find_package(Augeas REQUIRED)
target_link_libraries(srds_augeas ${AUGEAS_LIBRARIES})
target_link_libraries(ay_startup ${AUGEAS_LIBRARIES})
include_directories(${AUGEAS_INCLUDE_DIRS})

# pcre2
find_package(PCRE2 10.21 REQUIRED)
target_link_libraries(srds_augeas ${PCRE2_LIBRARIES})
target_link_libraries(ay_startup ${PCRE2_LIBRARIES})
include_directories(${PCRE2_INCLUDE_DIRS})

# libyang
find_package(LibYANG ${LIBYANG_DEP_SOVERSION} REQUIRED)
target_link_libraries(srds_augeas ${LIBYANG_LIBRARIES})
target_link_libraries(ay_startup ${LIBYANG_LIBRARIES})
target_link_libraries(augyang ${LIBYANG_LIBRARIES})
include_directories(${LIBYANG_INCLUDE_DIRS})

# sysrepo
find_package(Sysrepo 7 REQUIRED)
target_link_libraries(srds_augeas ${SYSREPO_LIBRARIES})
target_link_libraries(ay_startup ${SYSREPO_LIBRARIES})
include_directories(${SYSREPO_INCLUDE_DIRS})

# augeas external project
set(AUGEAS_DOWNLOAD_COMMAND git clone https://github.com/hercules-team/augeas "${AUGEAS_DIR}" 2> /dev/null || true)
set(GNULIB_DOWNLOAD_COMMAND git clone https://git.savannah.gnu.org/git/gnulib.git "${GNULIB_DIR}" 2> /dev/null || true)
ExternalProject_Add(augeas_ext
        PREFIX ${AUGEAS_PREFIX}
        DOWNLOAD_COMMAND ${AUGEAS_DOWNLOAD_COMMAND} && ${GNULIB_DOWNLOAD_COMMAND}
        CONFIGURE_COMMAND ${AUGEAS_DIR}/autogen.sh --gnulib-srcdir=${GNULIB_DIR} --disable-gnulib-tests --enable-debug
        BUILD_COMMAND make
        INSTALL_COMMAND ""
)
add_dependencies(augyang augeas_ext)
target_link_libraries(augyang ${AUGEAS_SRC_DIR}/.libs/libaugeas.a ${AUGEAS_SRC_DIR}/.libs/libfa.a)
target_include_directories(augyang PRIVATE ${AUGEAS_DIR}/src ${AUGEAS_SRC_DIR})

# yacc
find_program(BISON_FOUND yacc)
if(NOT BISON_FOUND)
    message(FATAL_ERROR "Bison (yacc) not found!")
endif()

# flex
find_program(FLEX_FOUND flex)
if(NOT FLEX_FOUND)
    message(FATAL_ERROR "Flex not found!")
endif()

# selinux (must be last, linked to augeas as well)
find_package(SELinux REQUIRED)
target_link_libraries(augyang ${SELINUX_LIBRARIES})
include_directories(${SELINUX_INCLUDE_DIRS})

# generate files
configure_file("${PROJECT_SOURCE_DIR}/plugins/plg_config.h.in" "${PROJECT_BINARY_DIR}/plg_config.h" ESCAPE_QUOTES @ONLY)
configure_file("${PROJECT_SOURCE_DIR}/src/ayg_config.h.in" "${PROJECT_BINARY_DIR}/ayg_config.h" ESCAPE_QUOTES @ONLY)

# install latest lenses with our patches
add_custom_target(install_lens
        COMMAND ${PROJECT_SOURCE_DIR}/lens_patch/update_lens.sh "${PROJECT_SOURCE_DIR}/lens_patch" "${AUGEAS_LENS_DIR}"
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR})

# generate correct lists of lenses and YANG modules
set(LENS_LIST ${SUPPORTED_LENSES})
separate_arguments(LENS_LIST)
string(REPLACE "_" "-" SUPPORTED_YANG_LIST ${SUPPORTED_LENSES})
set(YANG_LIST ${SUPPORTED_YANG_LIST})
separate_arguments(YANG_LIST)

# generate YANG modules from Augeas lenses
add_custom_command(TARGET augyang
        POST_BUILD
        COMMAND $<TARGET_FILE:augyang> ${LENS_LIST}
        COMMENT "Generate YANG modules: ${SUPPORTED_LENSES}"
        VERBATIM)

#
# installation
#

install(DIRECTORY "${PROJECT_SOURCE_DIR}/modules/" DESTINATION ${YANG_MODULE_DIR})
foreach(YANG IN LISTS YANG_LIST)
    list(APPEND YANG_FILES_LIST ${CMAKE_CURRENT_BINARY_DIR}/${YANG}.yang)
endforeach()
install(FILES ${YANG_FILES_LIST} DESTINATION ${YANG_MODULE_DIR})

if(INSTALL_MODULES)
    install(CODE "
        message(STATUS \"Install Augeas YANG modules: ${SUPPORTED_YANG_LIST}\")
        set(ENV{YANG_DIR} \"${YANG_MODULE_DIR}\")
        set(ENV{BINARY_DIR} \"${CMAKE_CURRENT_BINARY_DIR}\")
        execute_process(COMMAND \"${PROJECT_SOURCE_DIR}/scripts/install_modules.sh\" ${YANG_LIST} RESULT_VARIABLE EXEC_RET)
        if(NOT EXEC_RET EQUAL \"0\")
            message(FATAL_ERROR \" scripts/install_modules.sh failed: \${EXEC_RET}\")
        endif()
    ")
endif()

# tests
if(ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# source code format target for Makefile
# - add it after tests which may also update list of sources to format
source_format(${format_sources})

# clean cmake cache
add_custom_target(cclean
        COMMAND make clean
        COMMAND find . -iname '*cmake*' -not -name CMakeLists.txt -not -path './CMakeModules*' -exec rm -rf {} +
        COMMAND rm -rf Makefile Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
