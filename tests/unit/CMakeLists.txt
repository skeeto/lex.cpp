# A single executable with a tiny test framework. No external deps.
# Includes the same platform .cpp the lex binary uses but with
# LEXCPP_PLATFORM_NO_MAIN so the test harness keeps its own main.
if(WIN32)
    set(_unit_platform ${CMAKE_SOURCE_DIR}/src/platform_windows.cpp)
else()
    set(_unit_platform ${CMAKE_SOURCE_DIR}/src/platform_posix.cpp)
endif()

add_executable(lexcpp_unit
    test_internal.cpp
    ${CMAKE_SOURCE_DIR}/src/diag.cpp
    ${CMAKE_SOURCE_DIR}/src/source.cpp
    ${CMAKE_SOURCE_DIR}/src/regex.cpp
    ${CMAKE_SOURCE_DIR}/src/nfa.cpp
    ${CMAKE_SOURCE_DIR}/src/eclass.cpp
    ${CMAKE_SOURCE_DIR}/src/dfa.cpp
    ${CMAKE_SOURCE_DIR}/src/codegen.cpp
    ${CMAKE_SOURCE_DIR}/src/tables.cpp
    ${_unit_platform})

target_compile_definitions(lexcpp_unit PRIVATE LEXCPP_PLATFORM_NO_MAIN)

target_include_directories(lexcpp_unit PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/src)
target_link_libraries(lexcpp_unit PRIVATE
    lexcpp_warnings
    lexcpp_sanitize
    lexcpp_coverage)
add_dependencies(lexcpp_unit lex)  # ensure runtime_template.inc is built

add_test(NAME unit COMMAND lexcpp_unit)
