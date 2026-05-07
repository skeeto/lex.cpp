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
        -Wold-style-cast -Wcast-align
        -Wnon-virtual-dtor -Woverloaded-virtual
        -Wnull-dereference -Wdouble-promotion
        -Wformat=2)
    if(LEXCPP_WERROR)
        target_compile_options(lexcpp_warnings INTERFACE -Werror)
    endif()
endif()
