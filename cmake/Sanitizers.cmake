add_library(lexcpp_sanitize INTERFACE)
if(LEXCPP_SANITIZE)
    if(MSVC)
        message(WARNING "LEXCPP_SANITIZE not supported on MSVC; ignoring")
    else()
        target_compile_options(lexcpp_sanitize INTERFACE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
            -fno-sanitize-recover=all)
        target_link_options(lexcpp_sanitize INTERFACE
            -fsanitize=address,undefined)
    endif()
endif()
