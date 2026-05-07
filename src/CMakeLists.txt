add_executable(lex
    main.cpp
    diag.cpp
    source.cpp
    regex.cpp
    nfa.cpp
    dfa.cpp
    codegen.cpp
)

target_include_directories(lex PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(lex PRIVATE
    lexcpp_warnings
    lexcpp_sanitize
    lexcpp_coverage)

# Embed the C runtime template as a constant std::string_view.
set(RUNTIME_TEMPLATE "${CMAKE_SOURCE_DIR}/runtime/runtime.c.in")
set(RUNTIME_HEADER   "${CMAKE_CURRENT_BINARY_DIR}/runtime_template.inc")

add_custom_command(
    OUTPUT ${RUNTIME_HEADER}
    COMMAND ${CMAKE_COMMAND}
        -DINPUT=${RUNTIME_TEMPLATE}
        -DOUTPUT=${RUNTIME_HEADER}
        -P ${CMAKE_SOURCE_DIR}/cmake/EmbedFile.cmake
    DEPENDS ${RUNTIME_TEMPLATE} ${CMAKE_SOURCE_DIR}/cmake/EmbedFile.cmake
    VERBATIM)

target_sources(lex PRIVATE ${RUNTIME_HEADER})
