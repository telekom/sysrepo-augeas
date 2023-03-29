# - Try to find LibYANG
# Once done this will define
#
#  LIBXML2_FOUND - system has LibYANG
#  LIBXML2_INCLUDE_DIRS - the LibYANG include directory
#  LIBXML2_LIBRARIES - Link these to use LibYANG
#  LIBXML2_VERSION - SO version of the found libyang library
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

if(LIBXML2_LIBRARIES AND LIBXML2_INCLUDE_DIRS)
    # in cache already
    set(LIBXML2_FOUND TRUE)
else()
    find_path(LIBXML2_INCLUDE_DIR
        NAMES
        libxml/tree.h
        PATHS
        /usr/include
        /usr/include/libxml2
        /usr/local/include
        /opt/local/include
        /sw/include
        ${CMAKE_INCLUDE_PATH}
        ${CMAKE_INSTALL_PREFIX}/include
    )

    find_library(LIBXML2_LIBRARY
        NAMES
        xml2
	libxml2
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

    if(LIBXML2_INCLUDE_DIR)
        find_path(LXML_VERSION_PATH "libxml/xmlversion.h" HINTS ${LIBXML2_INCLUDE_DIR})
        if(LXML_VERSION_PATH)
            file(READ "${LXML_VERSION_PATH}/libxml/xmlversion.h" LXML_VERSION_FILE)
        endif()
	string(REGEX MATCH "#define LIBXML_VERSION \"[0-9]+\\.[0-9]+\\.[0-9]+\"" LXML_VERSION_MACRO "${LXML_VERSION_FILE}")
        string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" LIBXML_VERSION "${LXML_VERSION_MACRO}")
    endif()

    set(LIBXML2_INCLUDE_DIRS ${LIBXML2_INCLUDE_DIR})
    set(LIBXML2_LIBRARIES ${LIBXML2_LIBRARY})
    mark_as_advanced(LIBXML2_INCLUDE_DIRS LIBXML2_LIBRARIES)

    # handle the QUIETLY and REQUIRED arguments and set LIBXML2_FOUND to TRUE
    # if all listed variables are TRUE
    find_package_handle_standard_args(LibXML2 FOUND_VAR LIBXML2_FOUND
        REQUIRED_VARS LIBXML2_LIBRARY LIBXML2_INCLUDE_DIR
        VERSION_VAR LIBXML2_VERSION)
endif()
