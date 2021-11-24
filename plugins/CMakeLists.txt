cmake_minimum_required(VERSION 2.8.12)

# force out-of-source build
if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
    message(FATAL_ERROR "In-source build is not allowed. Please make a standalone build directory and run CMake from there. You may need to remove CMakeCache.txt.")
endif()

project(srds_augeas C)

# include custom Modules
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/../CMakeModules/")

# global C flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -std=c11")

#
# targets
#
add_library(srds_augeas MODULE srds_augeas.c)
set_target_properties(srds_augeas PROPERTIES PREFIX "")

#
# dependencies
#

# augeas
find_package(Augeas REQUIRED)
target_link_libraries(srds_augeas ${AUGEAS_LIBRARIES})
include_directories(${AUGEAS_INCLUDE_DIRS})

# sysrepo
find_package(Sysrepo 7 REQUIRED)
#target_link_libraries(srds_augeas ${SYSREPO_LIBRARIES})
include_directories(${SYSREPO_INCLUDE_DIRS})
