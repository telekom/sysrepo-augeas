if(NOT ${CMAKE_ARGC} EQUAL 9)
    message(FATAL_ERROR "[aytest] ERROR: wrong number of parameters.")
endif()

set(PROJECT_DIR ${CMAKE_ARGV3})
set(YANG_EXP_DIR ${CMAKE_ARGV4})
set(YANG_GEN_DIR ${CMAKE_ARGV5})
set(AUGYANG_BIN ${CMAKE_ARGV6})
set(YANGLINT_BIN ${CMAKE_ARGV7})
set(MOD ${CMAKE_ARGV8})

if(NOT PROJECT_DIR)
    message(FATAL_ERROR "[aytest] ERROR: PROJECT_DIR variable is empty.")
endif()

if(NOT YANG_EXP_DIR)
    message(FATAL_ERROR "[aytest] ERROR: YANG_EXP_DIR variable is empty.")
endif()

if(NOT YANG_GEN_DIR)
    message(FATAL_ERROR "[aytest] ERROR: YANG_GEN_DIR variable is empty.")
endif()

if(NOT AUGYANG_BIN)
    message(FATAL_ERROR "[aytest] ERROR: AUGYANG_BIN variable is empty.")
endif()

if(NOT YANGLINT_BIN)
    message(FATAL_ERROR "[aytest] ERROR: yanglint not found.")
endif()

if(NOT MOD)
    message(FATAL_ERROR "[aytest] ERROR: MOD variable is empty.")
endif()

set(GENFILE "${YANG_GEN_DIR}/${MOD}.yang")
set(EXPFILE "${YANG_EXP_DIR}/${MOD}.yang")

# generate yang file
execute_process(COMMAND ${AUGYANG_BIN} -v 0x8000 -O ${YANG_GEN_DIR} ${MOD} RESULT_VARIABLE ret)
if(NOT ret EQUAL 0)
    message(FATAL_ERROR "[aytest] '${MOD}' module generation failed.")
endif()

# compare generated yang file with expected one
execute_process(COMMAND diff ${GENFILE} ${EXPFILE} RESULT_VARIABLE ret)
if(ret EQUAL 1)
    message(FATAL_ERROR "[aytest] Comparison for '${MOD}' module failed.")
endif()

# check if generated file is valid yang module
execute_process(COMMAND ${YANGLINT_BIN} ${PROJECT_DIR}/modules/augeas-extension.yang ${GENFILE} RESULT_VARIABLE ret)
if(NOT ret EQUAL 0)
    message(FATAL_ERROR "[aytest] Yanglint validation for '${MOD}' module failed.")
endif()
