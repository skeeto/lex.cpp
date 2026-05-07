# A single executable with a tiny test framework. No external deps.
add_executable(lexcpp_unit
    test_internal.cpp
    ${CMAKE_SOURCE_DIR}/src/diag.cpp
    ${CMAKE_SOURCE_DIR}/src/source.cpp
    ${CMAKE_SOURCE_DIR}/src/regex.cpp
    ${CMAKE_SOURCE_DIR}/src/nfa.cpp
    ${CMAKE_SOURCE_DIR}/src/eclass.cpp
    ${CMAKE_SOURCE_DIR}/src/dfa.cpp
    ${CMAKE_SOURCE_DIR}/src/codegen.cpp
    ${CMAKE_SOURCE_DIR}/src/tables.cpp)

target_include_directories(lexcpp_unit PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/src)
target_link_libraries(lexcpp_unit PRIVATE
    lexcpp_warnings
    lexcpp_sanitize
    lexcpp_coverage)
add_dependencies(lexcpp_unit lex)  # ensure runtime_template.inc is built

add_test(NAME unit COMMAND lexcpp_unit)
