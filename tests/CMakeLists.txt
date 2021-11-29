if(NOT AUGYANG_VERSION)
    message(FATAL_ERROR "Please use the root CMakeLists file instead.")
endif()

# correct RPATH usage on OS X
set(CMAKE_MACOSX_RPATH TRUE)

# generate config
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/tconfig.h.in" "${CMAKE_CURRENT_BINARY_DIR}/tconfig.h" ESCAPE_QUOTES @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(SYSTEM ${CMOCKA_INCLUDE_DIR})

# format
if(SOURCE_FORMAT_ENABLED)
    add_test(NAME format WORKING_DIRECTORY ${CMAKE_BINARY_DIR} COMMAND make format-check)
endif()

if(ENABLE_AUGYANG)
    # YANG module check
    add_test(NAME yang COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test.sh)
endif()

# lists of all the DS plugin tests
set(tests test_passwd)

foreach(test_name IN LISTS tests)
    add_executable(${test_name} ${test_name}.c ${SRDS_AUGEAS_SRC})
    set_target_properties(${test_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    # config file to use as input for augeas for the test
    string(SUBSTRING "${test_name}" 5 -1 TEST_FILE)
    target_compile_definitions(${test_name} PUBLIC AUG_TEST_INPUT_FILE=\"${CMAKE_CURRENT_SOURCE_DIR}/config_files/${TEST_FILE}\")
endforeach()

# set common attributes of all tests
foreach(test_name IN LISTS tests)
    target_link_libraries(${test_name} ${CMOCKA_LIBRARIES} ${AUGEAS_LIBRARIES} ${SYSREPO_LIBRARIES} ${LIBYANG_LIBRARIES})
    add_test(NAME ${test_name} COMMAND $<TARGET_FILE:${test_name}>)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT
        "MALLOC_CHECK_=3"
        "CMOCKA_TEST_ABORT=1"
    )
endforeach()

# valgrind tests
if(ENABLE_VALGRIND_TESTS)
    foreach(test_name IN LISTS tests)
        add_test(NAME ${test_name}_valgrind COMMAND valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 $<TARGET_FILE:${test_name}>)
    endforeach()
endif()
