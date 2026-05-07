# add_lex_diff_test(NAME DIR)
#
# Defines a CTest test that:
#   1. Generates a scanner with flex from ${DIR}/scanner.l
#   2. (If LEXCPP_LEX_READY) generates a scanner with lex.cpp
#   3. Compiles both with the same C compiler
#   4. Runs both, feeding ${DIR}/input.txt on stdin
#      (empty file used if input.txt is absent)
#   5. Compares stdout byte-for-byte (and stderr+status if requested)
#
# Optional files in DIR:
#   input.txt              stdin for the scanner (default: empty)
#   gen_args.txt           extra args for flex / lex (one per line)
#   expected_status.txt    expected scanner exit code (default 0)
function(add_lex_diff_test name dir)
    add_test(
        NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DCASE_NAME=${name}
            -DCASE_DIR=${dir}
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/work/${name}
            -DFLEX_EXE=${FLEX_EXECUTABLE}
            -DLEX_EXE=$<TARGET_FILE:lex>
            -DCC_EXE=${CMAKE_C_COMPILER}
            -DLEX_READY=${LEXCPP_LEX_READY}
            -P ${CMAKE_SOURCE_DIR}/tests/run_diff.cmake
    )
    set_tests_properties(${name} PROPERTIES TIMEOUT 60)
endfunction()
