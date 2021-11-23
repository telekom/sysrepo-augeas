cmake_minimum_required(VERSION 3.20)
project(AugyangProject)

include(ExternalProject)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(AUGEAS_DIR ${CMAKE_CURRENT_BINARY_DIR}/augeas-prefix/src/augeas)
set(AUGEAS_LENSES_DIR ${AUGEAS_DIR}/lenses)
set(AUGEAS_BUILD ${CMAKE_CURRENT_BINARY_DIR}/augeas-prefix/src/augeas-build)
set(AUGEAS_OBJECTS ${AUGEAS_BUILD}/src)
set(GNU_OBJECTS ${AUGEAS_BUILD}/gnulib/lib)
set(AUGEAS_CFLAGS "-g -O0")
set(AUGYANG_DEPEND
    ${AUGEAS_OBJECTS}/ast.o
    ${AUGEAS_OBJECTS}/builtin.o
    ${AUGEAS_OBJECTS}/fa.o
    ${AUGEAS_OBJECTS}/hash.o
    ${AUGEAS_OBJECTS}/internal.o
    ${AUGEAS_OBJECTS}/lens.o
    ${AUGEAS_OBJECTS}/memory.o
    ${AUGEAS_OBJECTS}/pathx.o 
    ${AUGEAS_OBJECTS}/ref.o   
    ${AUGEAS_OBJECTS}/syntax.o
    ${AUGEAS_OBJECTS}/augeas.o
    ${AUGEAS_OBJECTS}/errcode.o
    ${AUGEAS_OBJECTS}/get.o
    ${AUGEAS_OBJECTS}/info.o
    ${AUGEAS_OBJECTS}/jmt.o
    ${AUGEAS_OBJECTS}/liblexer_la-lexer.o
    ${AUGEAS_OBJECTS}/parser.o
    ${AUGEAS_OBJECTS}/put.o
    ${AUGEAS_OBJECTS}/regexp.o
    ${AUGEAS_OBJECTS}/transform.o

    ${GNU_OBJECTS}/free.o
    ${GNU_OBJECTS}/getfilecon.o
    ${GNU_OBJECTS}/hard-locale.o
    ${GNU_OBJECTS}/mbrtowc.o
    ${GNU_OBJECTS}/nl_langinfo.o
    ${GNU_OBJECTS}/regex.o
    ${GNU_OBJECTS}/setlocale_null.o

    ${GNU_OBJECTS}/malloc/dynarray_emplace_enlarge.o
    ${GNU_OBJECTS}/malloc/dynarray_finalize.o
    ${GNU_OBJECTS}/malloc/dynarray_resize.o
)


find_path(LIBYANG_INCLUDE_DIR
    NAMES
    libyang/libyang.h
    PATHS
    /usr/include
    /usr/local/include
    /opt/local/include
    /sw/include
    ${CMAKE_INCLUDE_PATH}
    ${CMAKE_INSTALL_PREFIX}/include
)

find_library(LIBYANG_LIBRARY
    NAMES
    yang
    libyang
    PATHS
    /usr/lib
    /usr/lib64
    /usr/local/lib
    /usr/local/lib64
    /opt/local/lib
    /sw/lib
    ${CMAKE_LIBRARY_PATH}
    ${CMAKE_INSTALL_PREFIX}/lib
)
set(LIBYANG_INCLUDE_DIRS ${LIBYANG_INCLUDE_DIR})
set(LIBYANG_LIBRARIES ${LIBYANG_LIBRARY})

ExternalProject_Add(augeas
    UPDATE_DISCONNECTED true # avoid constant rebuild
    CONFIGURE_HANDLED_BY_BUILD ON  # avoid constant reconfigure
    GIT_REPOSITORY https://github.com/hercules-team/augeas
    CONFIGURE_COMMAND ${AUGEAS_DIR}/autogen.sh && ${AUGEAS_DIR}/configure --enable-debug CFLAGS=${AUGEAS_CFLAGS}
    BUILD_COMMAND make
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS ${AUGYANG_DEPEND}
)

add_executable(augyang
    augyang_main.c
    augyang.c
    ${AUGYANG_DEPEND}
)

target_link_libraries(augyang ${LIBYANG_LIBRARIES} selinux)
target_include_directories(augyang PRIVATE ${LIBYANG_INCLUDE_DIRS} ${AUGEAS_DIR}/src ${AUGEAS_BUILD}/src)
add_dependencies(augyang augeas)
target_compile_definitions(augyang PRIVATE AUGEAS_LENSES_DIR="${AUGEAS_LENSES_DIR}")
target_compile_options(augyang PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -O0 -ggdb>)
