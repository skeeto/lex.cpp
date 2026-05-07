# Centralised warning flags. Apply by linking against lexcpp_warnings.
add_library(lexcpp_warnings INTERFACE)

if(MSVC)
    target_compile_options(lexcpp_warnings INTERFACE /W4 /permissive-)
    if(LEXCPP_WERROR)
        target_compile_options(lexcpp_warnings INTERFACE /WX)
    endif()
    target_compile_definitions(lexcpp_warnings INTERFACE
        _CRT_SECURE_NO_WARNINGS=1
        _CRT_NONSTDC_NO_DEPRECATE=1)
else()
    target_compile_options(lexcpp_warnings INTERFACE
        -Wall -Wextra -Wpedantic
        -Wshadow -Wconversion -Wsign-conversion
        -Wcast-align
        -Wnull-dereference -Wdouble-promotion
        -Wformat=2)
    # C++-only flags: gated so C TUs (lib/yywrap.c, lib/main.c) don't
    # warn about unrecognised options.
    target_compile_options(lexcpp_warnings INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:-Wold-style-cast>
        $<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>
        $<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>)
    if(LEXCPP_WERROR)
        target_compile_options(lexcpp_warnings INTERFACE -Werror)
    endif()
endif()
