# Cross-platform sources -- everything above the platform layer.
set(LEXCPP_CORE_SOURCES
    main.cpp
    diag.cpp
    source.cpp
    regex.cpp
    nfa.cpp
    eclass.cpp
    dfa.cpp
    codegen.cpp
    tables.cpp)

# Platform-specific. Both files internally guard on _WIN32 so adding
# both is harmless; we still pick the right one to keep the toolchain
# from chewing on inactive includes (e.g. <windows.h> on Linux).
if(WIN32)
    set(LEXCPP_PLATFORM_SOURCES platform_windows.cpp)
else()
    set(LEXCPP_PLATFORM_SOURCES platform_posix.cpp)
endif()

# Surface the chosen platform .cpp to other CMakeLists (unit tests, fuzz).
set(LEXCPP_PLATFORM_SOURCES "${LEXCPP_PLATFORM_SOURCES}" PARENT_SCOPE)
set(LEXCPP_CORE_SOURCES     "${LEXCPP_CORE_SOURCES}"     PARENT_SCOPE)

add_executable(lex
    ${LEXCPP_CORE_SOURCES}
    ${LEXCPP_PLATFORM_SOURCES}
)

if(WIN32)
    # CommandLineToArgvW lives in shell32.
    target_link_libraries(lex PRIVATE shell32)
endif()

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

install(TARGETS lex
    EXPORT lexcppTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
