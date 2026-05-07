# Unit tests don't need flex.
add_subdirectory(unit)

find_package(FLEX)

if(NOT FLEX_FOUND)
    message(WARNING "flex not found; differential tests will be skipped. "
                    "Install flex to enable the test suite.")
    return()
endif()

# When OFF (default), tests only verify the flex side. When ON, every
# test also generates a scanner with our `lex` and compares output to
# flex byte-for-byte.
option(LEXCPP_LEX_READY "Compare lex output against flex (full differential)" OFF)

# Force lex.cpp to a specific table-compression mode for the sweep:
# empty (default), "-f", "-Cfe", or "-Cem". Passed as an extra flag
# to the lex generation step only -- flex's side is unaffected.
set(LEXCPP_TEST_COMPRESS "" CACHE STRING
    "Extra compression flag passed to lex during testing (-f/-Cfe/-Cem)")

include(${CMAKE_CURRENT_SOURCE_DIR}/differential.cmake)

# Custom test: yytables_fload roundtrip end-to-end.
# Generates a scanner with --tables-file, compiles with a driver that
# calls yytables_fload before yylex, runs against fixed input, and
# compares against a snapshot.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/run_tables_load.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/run_tables_load.cmake
    @ONLY)
add_test(
    NAME tables_load_roundtrip
    COMMAND ${CMAKE_COMMAND}
        -DLEX_EXE=$<TARGET_FILE:lex>
        -DLEX_LIB=$<TARGET_FILE:lex_lib>
        -DCC_EXE=${CMAKE_C_COMPILER}
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/work/tables_load
        -DSCAN_L=${CMAKE_CURRENT_SOURCE_DIR}/tables_load/scanner.l
        -DDRIVER_C=${CMAKE_CURRENT_SOURCE_DIR}/tables_load/driver.c
        -DEXPECTED_OUT=${CMAKE_CURRENT_SOURCE_DIR}/tables_load/expected.txt
        -DINPUT_TXT=${CMAKE_CURRENT_SOURCE_DIR}/tables_load/input.txt
        -P ${CMAKE_CURRENT_BINARY_DIR}/run_tables_load.cmake)

# Auto-discover test cases.
file(GLOB CASE_DIRS LIST_DIRECTORIES true RELATIVE
     ${CMAKE_CURRENT_SOURCE_DIR}/cases
     ${CMAKE_CURRENT_SOURCE_DIR}/cases/*)
foreach(case_name ${CASE_DIRS})
    set(case_path ${CMAKE_CURRENT_SOURCE_DIR}/cases/${case_name})
    if(IS_DIRECTORY ${case_path} AND EXISTS ${case_path}/scanner.l)
        add_lex_diff_test(${case_name} ${case_path})
    endif()
endforeach()
