# Driver for one differential test case. Run via `cmake -P` from CTest.
# Required:
#   CASE_NAME   - identifier
#   CASE_DIR    - source directory (contains scanner.l, [input.txt])
#   WORK_DIR    - scratch directory (will be created/cleaned)
#   FLEX_EXE    - path to flex
#   LEX_EXE     - path to our lex
#   CC_EXE      - C compiler
#   LEX_READY   - "ON"/"OFF": when ON, generate with lex too and diff

if(NOT CASE_DIR OR NOT WORK_DIR OR NOT FLEX_EXE OR NOT CC_EXE)
    message(FATAL_ERROR "missing required argument")
endif()

file(REMOVE_RECURSE ${WORK_DIR})
file(MAKE_DIRECTORY ${WORK_DIR})

set(SCANNER_L ${CASE_DIR}/scanner.l)
if(NOT EXISTS ${SCANNER_L})
    message(FATAL_ERROR "${CASE_NAME}: ${SCANNER_L} does not exist")
endif()

set(INPUT_FILE ${CASE_DIR}/input.txt)
if(NOT EXISTS ${INPUT_FILE})
    set(INPUT_FILE ${WORK_DIR}/empty_input.txt)
    file(WRITE ${INPUT_FILE} "")
endif()

set(EXPECTED_STATUS 0)
if(EXISTS ${CASE_DIR}/expected_status.txt)
    file(READ ${CASE_DIR}/expected_status.txt EXPECTED_STATUS)
    string(STRIP "${EXPECTED_STATUS}" EXPECTED_STATUS)
endif()

set(GEN_ARGS "")
if(EXISTS ${CASE_DIR}/gen_args.txt)
    file(STRINGS ${CASE_DIR}/gen_args.txt GEN_ARGS)
endif()

# ---------------------------------------------------------------- helpers

function(_run_or_die label cmd)
    execute_process(
        COMMAND ${cmd} ${ARGN}
        WORKING_DIRECTORY ${WORK_DIR}
        RESULT_VARIABLE rv
        OUTPUT_VARIABLE out
        ERROR_VARIABLE  err)
    if(NOT rv EQUAL 0)
        message(FATAL_ERROR
            "${CASE_NAME}: ${label} failed (rc=${rv})\n"
            "stdout:\n${out}\n"
            "stderr:\n${err}")
    endif()
endfunction()

function(_generate_with engine engine_path c_out)
    set(_args ${GEN_ARGS} -o ${c_out} ${SCANNER_L})
    execute_process(
        COMMAND ${engine_path} ${_args}
        WORKING_DIRECTORY ${WORK_DIR}
        RESULT_VARIABLE rv
        OUTPUT_VARIABLE out
        ERROR_VARIABLE  err)
    if(NOT rv EQUAL 0)
        message(FATAL_ERROR
            "${CASE_NAME}: ${engine} generation failed (rc=${rv})\n"
            "argv: ${engine_path} ${_args}\n"
            "stderr:\n${err}")
    endif()
endfunction()

function(_compile c_in bin_out)
    # -w to silence flex's old-style warnings; we only care about output.
    # Modern clang errors on implicit function declarations and old-style
    # K&R; flex 2.6.4's output uses both. Soften to warnings.
    # Link liblex (our default yywrap+main) -- the static archive only
    # contributes symbols not already defined by the .l file.
    set(_lex_lib "")
    if(LEX_LIB)
        set(_lex_lib ${LEX_LIB})
    endif()
    execute_process(
        COMMAND ${CC_EXE} -std=c99 -O0 -g -w
                -Wno-error=implicit-function-declaration
                -Wno-error=int-conversion
                -Wno-error=incompatible-pointer-types
                ${c_in} ${_lex_lib} -o ${bin_out}
        WORKING_DIRECTORY ${WORK_DIR}
        RESULT_VARIABLE rv
        OUTPUT_VARIABLE out
        ERROR_VARIABLE  err)
    if(NOT rv EQUAL 0)
        message(FATAL_ERROR
            "${CASE_NAME}: compile failed (rc=${rv})\n"
            "src: ${c_in}\nstderr:\n${err}")
    endif()
endfunction()

function(_run bin stdout_file status_var)
    execute_process(
        COMMAND ${bin}
        INPUT_FILE  ${INPUT_FILE}
        OUTPUT_FILE ${stdout_file}
        ERROR_QUIET
        WORKING_DIRECTORY ${WORK_DIR}
        RESULT_VARIABLE rv)
    set(${status_var} ${rv} PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------- flex side

_generate_with(flex ${FLEX_EXE} ${WORK_DIR}/flex_scanner.c)
_compile(${WORK_DIR}/flex_scanner.c ${WORK_DIR}/flex_scanner)
_run(${WORK_DIR}/flex_scanner ${WORK_DIR}/flex.out flex_status)

if(NOT flex_status EQUAL EXPECTED_STATUS)
    file(READ ${WORK_DIR}/flex.out flex_out)
    message(FATAL_ERROR
        "${CASE_NAME}: flex scanner exited ${flex_status}, expected ${EXPECTED_STATUS}\n"
        "stdout was:\n${flex_out}")
endif()

# ---------------------------------------------------------------- lex side
if(LEX_READY)
    set(_lex_args ${GEN_ARGS} -o ${WORK_DIR}/lex_scanner.c ${SCANNER_L})
    if(LEX_EXTRA)
        list(PREPEND _lex_args ${LEX_EXTRA})
    endif()
    execute_process(
        COMMAND ${LEX_EXE} ${_lex_args}
        WORKING_DIRECTORY ${WORK_DIR}
        RESULT_VARIABLE rv
        OUTPUT_VARIABLE out
        ERROR_VARIABLE  err)
    if(NOT rv EQUAL 0)
        message(FATAL_ERROR
            "${CASE_NAME}: lex generation failed (rc=${rv})\n"
            "argv: ${LEX_EXE} ${_lex_args}\nstderr:\n${err}")
    endif()
    _compile(${WORK_DIR}/lex_scanner.c ${WORK_DIR}/lex_scanner)
    _run(${WORK_DIR}/lex_scanner ${WORK_DIR}/lex.out lex_status)

    if(NOT lex_status EQUAL EXPECTED_STATUS)
        file(READ ${WORK_DIR}/lex.out lex_out)
        message(FATAL_ERROR
            "${CASE_NAME}: lex scanner exited ${lex_status}, expected ${EXPECTED_STATUS}\n"
            "stdout was:\n${lex_out}")
    endif()

    execute_process(
        COMMAND ${CMAKE_COMMAND} -E compare_files
                ${WORK_DIR}/flex.out ${WORK_DIR}/lex.out
        RESULT_VARIABLE diff_rv)
    if(NOT diff_rv EQUAL 0)
        file(READ ${WORK_DIR}/flex.out flex_out)
        file(READ ${WORK_DIR}/lex.out  lex_out)
        message(FATAL_ERROR
            "${CASE_NAME}: lex output differs from flex.\n"
            "--- flex stdout (${WORK_DIR}/flex.out) ---\n${flex_out}"
            "--- lex stdout  (${WORK_DIR}/lex.out)  ---\n${lex_out}")
    endif()
endif()
