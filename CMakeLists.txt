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

# Correct RPATH usage on OS X
set(CMAKE_MACOSX_RPATH TRUE)

# keep all binaries in the build directory
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# set version of the project
set(AUGYANG_VERSION 1.0.0)

# version of libyang that this project depends on
set(LIBYANG_DEP_VERSION 2.0.104)
set(LIBYANG_DEP_SOVERSION 2.13.0)
set(LIBYANG_DEP_SOVERSION_MAJOR 2)

# global C flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -std=gnu11")

include_directories(${PROJECT_BINARY_DIR}/src ${PROJECT_SOURCE_DIR}/src)

#
# sources
#
set(augyang_src
    src/main.c
    src/augyang.c)

# source files to be covered by the 'format' target
set(format_sources
    ${augyang_src})

#
# options
#

if(("${BUILD_TYPE_UPPER}" STREQUAL "DEBUG") OR ("${BUILD_TYPE_UPPER}" STREQUAL "RELWITHDEBINFO"))
    option(ENABLE_TESTS "Build tests" ON)
else()
    option(ENABLE_TESTS "Build tests" OFF)
endif()

# augeas paths
set(AUGEAS_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/augeas)
set(AUGEAS_DIR ${AUGEAS_PREFIX}/src/augeas)
set(AUGEAS_LENSES_DIR ${AUGEAS_DIR}/lenses)
set(AUGEAS_BUILD_DIR ${AUGEAS_PREFIX}/src/augeas-build)
set(AUGEAS_SRC_DIR ${AUGEAS_BUILD_DIR}/src)

# GNU lib paths
set(GNULIB_DIR ${AUGEAS_DIR}/.gnulib)
set(GNULIB_SRC_DIR ${AUGEAS_BUILD_DIR}/gnulib/lib)

#
# checks
#
if ("${BUILD_TYPE_UPPER}" STREQUAL "DEBUG")
    # enable before adding tests to let them detect that format checking is available - one of the tests is format checking
    source_format_enable()
endif()

#
# targets
#
set(AUGEAS_OBJS
    ${AUGEAS_SRC_DIR}/ast.o
    ${AUGEAS_SRC_DIR}/builtin.o
    ${AUGEAS_SRC_DIR}/fa.o
    ${AUGEAS_SRC_DIR}/hash.o
    ${AUGEAS_SRC_DIR}/internal.o
    ${AUGEAS_SRC_DIR}/lens.o
    ${AUGEAS_SRC_DIR}/memory.o
    ${AUGEAS_SRC_DIR}/pathx.o
    ${AUGEAS_SRC_DIR}/ref.o
    ${AUGEAS_SRC_DIR}/syntax.o
    ${AUGEAS_SRC_DIR}/augeas.o
    ${AUGEAS_SRC_DIR}/errcode.o
    ${AUGEAS_SRC_DIR}/get.o
    ${AUGEAS_SRC_DIR}/info.o
    ${AUGEAS_SRC_DIR}/jmt.o
    ${AUGEAS_SRC_DIR}/liblexer_la-lexer.o
    ${AUGEAS_SRC_DIR}/parser.o
    ${AUGEAS_SRC_DIR}/put.o
    ${AUGEAS_SRC_DIR}/regexp.o
    ${AUGEAS_SRC_DIR}/transform.o
)

set(GNULIB_OBJS
    ${GNULIB_SRC_DIR}/free.o
    ${GNULIB_SRC_DIR}/getfilecon.o
    ${GNULIB_SRC_DIR}/hard-locale.o
    ${GNULIB_SRC_DIR}/mbrtowc.o
    ${GNULIB_SRC_DIR}/nl_langinfo.o
    ${GNULIB_SRC_DIR}/regex.o
    ${GNULIB_SRC_DIR}/setlocale_null.o

    ${GNULIB_SRC_DIR}/malloc/dynarray_emplace_enlarge.o
    ${GNULIB_SRC_DIR}/malloc/dynarray_finalize.o
    ${GNULIB_SRC_DIR}/malloc/dynarray_resize.o)

add_executable(augyang ${augyang_src} ${AUGEAS_OBJS} ${GNULIB_OBJS})

#
# dependencies
#

# augeas external project
ExternalProject_Add(augeas
    PREFIX ${AUGEAS_PREFIX}
    DOWNLOAD_COMMAND git clone https://github.com/hercules-team/augeas ${AUGEAS_DIR} &&
        git clone https://git.savannah.gnu.org/git/gnulib.git ${GNULIB_DIR}
    CONFIGURE_COMMAND ${AUGEAS_DIR}/autogen.sh --gnulib-srcdir=${GNULIB_DIR} --disable-gnulib-tests --enable-debug
    BUILD_COMMAND make
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS ${AUGEAS_OBJS} ${GNULIB_OBJS}
)

add_dependencies(augyang augeas)
target_include_directories(augyang PRIVATE ${AUGEAS_DIR}/src ${AUGEAS_BUILD_DIR}/src)
target_compile_definitions(augyang PRIVATE AUGEAS_LENSES_DIR="${AUGEAS_LENSES_DIR}")

# libyang, check version
find_package(LibYANG ${LIBYANG_DEP_SOVERSION} REQUIRED)
target_link_libraries(augyang ${LIBYANG_LIBRARIES})
include_directories(${LIBYANG_INCLUDE_DIRS})

# selinux
target_link_libraries(augyang selinux)

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
