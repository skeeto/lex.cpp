# liblex.a -- default yywrap + main for scanners that don't use
# %option noyywrap. CMake target is `lex_lib` to avoid clashing with
# the executable target `lex`; OUTPUT_NAME drops to `lex` so the
# resulting archive is liblex.a.
add_library(lex_lib STATIC yywrap.c main.c)
set_target_properties(lex_lib PROPERTIES
    OUTPUT_NAME    lex
    POSITION_INDEPENDENT_CODE ON)

target_link_libraries(lex_lib PRIVATE lexcpp_warnings)

install(TARGETS lex_lib
    EXPORT lexcppTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

# pkg-config integration so consumers can do  pkg-config --libs lex.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/lex.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/lex.pc
    @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/lex.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
