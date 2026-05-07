add_library(lexcpp_coverage INTERFACE)
if(LEXCPP_COVERAGE)
    if(MSVC)
        message(WARNING "LEXCPP_COVERAGE not supported on MSVC; ignoring")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(lexcpp_coverage INTERFACE
            -fprofile-instr-generate -fcoverage-mapping)
        target_link_options(lexcpp_coverage INTERFACE
            -fprofile-instr-generate -fcoverage-mapping)
    else()
        target_compile_options(lexcpp_coverage INTERFACE --coverage -O0 -g)
        target_link_options(lexcpp_coverage INTERFACE --coverage)
    endif()

    # `coverage` target: run ctest, then summarise.
    find_program(GCOVR_EXE gcovr)
    find_program(LLVM_COV  llvm-cov)
    find_program(LLVM_PROFDATA llvm-profdata)

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND LLVM_COV AND LLVM_PROFDATA)
        add_custom_target(coverage
            COMMAND ${CMAKE_COMMAND} -E rm -rf coverage
            COMMAND ${CMAKE_COMMAND} -E make_directory coverage
            COMMAND ${CMAKE_COMMAND} -E env LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/coverage/lex-%p.profraw
                    ${CMAKE_CTEST_COMMAND} --output-on-failure
            COMMAND ${LLVM_PROFDATA} merge -sparse
                    ${CMAKE_BINARY_DIR}/coverage/lex-*.profraw
                    -o ${CMAKE_BINARY_DIR}/coverage/lex.profdata
            COMMAND ${LLVM_COV} report $<TARGET_FILE:lex>
                    -instr-profile=${CMAKE_BINARY_DIR}/coverage/lex.profdata
            COMMAND ${LLVM_COV} show $<TARGET_FILE:lex>
                    -instr-profile=${CMAKE_BINARY_DIR}/coverage/lex.profdata
                    -format=html -output-dir=${CMAKE_BINARY_DIR}/coverage/html
                    -show-line-counts-or-regions
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            VERBATIM)
    elseif(GCOVR_EXE)
        add_custom_target(coverage
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            COMMAND ${GCOVR_EXE} --root ${CMAKE_SOURCE_DIR}
                    --filter ${CMAKE_SOURCE_DIR}/src
                    --html-details ${CMAKE_BINARY_DIR}/coverage/index.html
                    --txt
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            VERBATIM)
    endif()
endif()
