if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(WARNING "LEXCPP_FUZZ requires clang; skipping fuzz targets")
    return()
endif()

# Common fuzz flags. ASan + UBSan + libFuzzer.
add_library(lexcpp_fuzz_iface INTERFACE)
target_compile_options(lexcpp_fuzz_iface INTERFACE
    -fsanitize=fuzzer-no-link,address,undefined
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer)
target_link_options(lexcpp_fuzz_iface INTERFACE
    -fsanitize=fuzzer,address,undefined)

# Fuzz harnesses link diag.cpp which goes through the platform layer
# for stderr; pull in platform_posix.cpp (clang+linux only) and tell
# it not to emit a main.
set(FUZZ_PLATFORM ${CMAKE_SOURCE_DIR}/src/platform_posix.cpp)

# fuzz_regex: random bytes -> regex parser -> NFA construction.
add_executable(fuzz_regex
    fuzz_regex.cc
    ${CMAKE_SOURCE_DIR}/src/diag.cpp
    ${CMAKE_SOURCE_DIR}/src/regex.cpp
    ${CMAKE_SOURCE_DIR}/src/nfa.cpp
    ${CMAKE_SOURCE_DIR}/src/eclass.cpp
    ${CMAKE_SOURCE_DIR}/src/dfa.cpp
    ${FUZZ_PLATFORM})
target_compile_definitions(fuzz_regex PRIVATE LEXCPP_PLATFORM_NO_MAIN)
target_include_directories(fuzz_regex PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fuzz_regex PRIVATE lexcpp_fuzz_iface lexcpp_warnings)

# fuzz_lfile: random bytes -> full .l parser -> codegen.
add_executable(fuzz_lfile
    fuzz_lfile.cc
    ${CMAKE_SOURCE_DIR}/src/diag.cpp
    ${CMAKE_SOURCE_DIR}/src/source.cpp
    ${CMAKE_SOURCE_DIR}/src/regex.cpp
    ${CMAKE_SOURCE_DIR}/src/nfa.cpp
    ${CMAKE_SOURCE_DIR}/src/eclass.cpp
    ${CMAKE_SOURCE_DIR}/src/dfa.cpp
    ${CMAKE_SOURCE_DIR}/src/codegen.cpp
    ${CMAKE_SOURCE_DIR}/src/tables.cpp
    ${FUZZ_PLATFORM})
target_compile_definitions(fuzz_lfile PRIVATE LEXCPP_PLATFORM_NO_MAIN)
target_include_directories(fuzz_lfile PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/src)
target_link_libraries(fuzz_lfile PRIVATE lexcpp_fuzz_iface lexcpp_warnings)
add_dependencies(fuzz_lfile lex)  # needs runtime_template.inc

# fuzz_runtime: feed random bytes into a scanner generated from a fixed
# .l file. The .l is embedded into the build.
set(RUNTIME_LFILE  ${CMAKE_CURRENT_SOURCE_DIR}/runtime_grammar.l)
set(RUNTIME_GEN    ${CMAKE_CURRENT_BINARY_DIR}/fuzz_runtime_scanner.c)
add_custom_command(
    OUTPUT ${RUNTIME_GEN}
    COMMAND $<TARGET_FILE:lex> -o ${RUNTIME_GEN} ${RUNTIME_LFILE}
    DEPENDS ${RUNTIME_LFILE} lex
    VERBATIM)

add_executable(fuzz_runtime
    fuzz_runtime.cc
    ${RUNTIME_GEN})
target_include_directories(fuzz_runtime PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fuzz_runtime PRIVATE lexcpp_fuzz_iface lexcpp_warnings)
# Generated scanner uses old-style C; relax warnings.
set_source_files_properties(${RUNTIME_GEN} PROPERTIES
    COMPILE_OPTIONS "-w;-Wno-error=implicit-function-declaration")
