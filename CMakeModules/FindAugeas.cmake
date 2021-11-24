# - Try to find augeas
# Once done this will define
#
#  AUGEAS_FOUND - system has Augeas
#  AUGEAS_INCLUDE_DIRS - the Augeas include directory
#  AUGEAS_LIBRARIES - Link these to use augeas
#
#  Author Michal Vasko <mvasko@cesnet.cz>
#  Copyright (c) 2021 CESNET, z.s.p.o.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions
#  are met:
#
#  1. Redistributions of source code must retain the copyright
#     notice, this list of conditions and the following disclaimer.
#  2. Redistributions in binary form must reproduce the copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#  3. The name of the author may not be used to endorse or promote products
#     derived from this software without specific prior written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
#  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
#  OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
#  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
#  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
#  NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
#  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
include(FindPackageHandleStandardArgs)

if(AUGEAS_LIBRARIES AND AUGEAS_INCLUDE_DIRS)
    # in cache already
    set(AUGEAS_FOUND TRUE)
else()
    find_path(AUGEAS_INCLUDE_DIR
        NAMES
        augeas.h
        PATHS
        /usr/include
        /usr/local/include
        /opt/local/include
        /sw/include
        ${CMAKE_INCLUDE_PATH}
        ${CMAKE_INSTALL_PREFIX}/include
    )

    find_library(AUGEAS_LIBRARY
        NAMES
        augeas
        libaugeas
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

    set(AUGEAS_INCLUDE_DIRS ${AUGEAS_INCLUDE_DIR})
    set(AUGEAS_LIBRARIES ${AUGEAS_LIBRARY})
    mark_as_advanced(AUGEAS_INCLUDE_DIRS AUGEAS_LIBRARIES)

    # handle the QUIETLY and REQUIRED arguments and set AUGEAS_FOUND to TRUE
    # if all listed variables are TRUE
    find_package_handle_standard_args(Augeas FOUND_VAR AUGEAS_FOUND
        REQUIRED_VARS AUGEAS_LIBRARY AUGEAS_INCLUDE_DIR)
endif()
