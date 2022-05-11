if(NOT AUGYANG_VERSION)
    message(FATAL_ERROR "Please use the root CMakeLists file instead.")
endif()

# correct RPATH usage on OS X
set(CMAKE_MACOSX_RPATH TRUE)

# test lib
set(TLIB_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/tcommon.c)

# find diff
find_program(DIFF_EXECUTABLE diff)
if(NOT DIFF_EXECUTABLE)
    message(FATAL_ERROR "Failed to find diff(1) executable.")
endif()

# generate config
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/tconfig.h.in" "${CMAKE_CURRENT_BINARY_DIR}/tconfig.h" ESCAPE_QUOTES @ONLY)

# format
if(SOURCE_FORMAT_ENABLED)
    add_test(NAME format WORKING_DIRECTORY ${CMAKE_BINARY_DIR} COMMAND make format-check)
endif()

# augyang tests
set(YANG_EXP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/yang_expected)
set(YANG_GEN_DIR ${CMAKE_BINARY_DIR}/tests/augyang)
file(MAKE_DIRECTORY ${YANG_GEN_DIR})
find_program(YANGLINT_BIN yanglint)

file(GLOB yang_expected_files "${YANG_EXP_DIR}/*.yang")
foreach(expfile ${yang_expected_files})
    get_filename_component(mod ${expfile} NAME_WE)
    add_test(NAME "aytest_${mod}" COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules/AYtest.cmake
        ${PROJECT_SOURCE_DIR} ${YANG_EXP_DIR} ${YANG_GEN_DIR} $<TARGET_FILE:augyang> ${YANGLINT_BIN} ${mod})
endforeach()

# lists of all the DS plugin tests
set(tests test_passwd test_simplevars test_postfix_sasl_smtpd test_dhclient test_ntpd test_ntp test_cron test_dnsmasq
    test_iptables test_pam test_xendconfsxp test_systemd test_sshd test_ssh test_anaconda test_ceph test_cmdline
    test_cpanel test_darkice test_pg_hba test_devfsrules test_device_map test_ethers test_grubenv test_backuppchosts
    test_gtkbookmarks test_hostname test_hosts test_inittab test_inputrc test_iproute2 test_iscsid test_login_defs
    test_monit test_postfix_access)

include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_SOURCE_DIR}/plugins)
include_directories(SYSTEM ${CMOCKA_INCLUDE_DIR})
foreach(test_name IN LISTS tests)
    add_executable(${test_name} ${test_name}.c ${TLIB_SRC})
    set_target_properties(${test_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endforeach()

# set common attributes of all tests
foreach(test_name IN LISTS tests)
    target_link_libraries(${test_name} ${CMOCKA_LIBRARIES} ${AUGEAS_LIBRARIES} ${PCRE2_LIBRARIES} ${SYSREPO_LIBRARIES} ${LIBYANG_LIBRARIES})
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

# make ay_new_expected
# Copy generated yang files to the tests/yang_expected directory.
# Useful for uploading modified test yang files to the git repository.
add_custom_target(ay_new_expected COMMAND cp -R ${YANG_GEN_DIR}/*.yang ${YANG_EXP_DIR})
