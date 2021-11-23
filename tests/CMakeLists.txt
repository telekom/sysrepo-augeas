if(NOT AUGYANG_VERSION)
    message(FATAL_ERROR "Please use the root CMakeLists file instead.")
endif()

# format
if (${SOURCE_FORMAT_ENABLED})
    add_test(NAME format WORKING_DIRECTORY ${CMAKE_BINARY_DIR} COMMAND make format-check)
endif()

# YANG module check
add_test(NAME yang COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test.sh)
