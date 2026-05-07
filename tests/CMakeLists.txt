find_package(FLEX)

if(NOT FLEX_FOUND)
    message(WARNING "flex not found; differential tests will be skipped. "
                    "Install flex to enable the test suite.")
    return()
endif()

# When OFF (default), tests only verify the flex side. When ON, every
# test also generates a scanner with our `lex` and compares output to
# flex byte-for-byte.
option(LEXCPP_LEX_READY "Compare lex output against flex (full differential)" OFF)

include(${CMAKE_CURRENT_SOURCE_DIR}/differential.cmake)

# Auto-discover test cases.
file(GLOB CASE_DIRS LIST_DIRECTORIES true RELATIVE
     ${CMAKE_CURRENT_SOURCE_DIR}/cases
     ${CMAKE_CURRENT_SOURCE_DIR}/cases/*)
foreach(case_name ${CASE_DIRS})
    set(case_path ${CMAKE_CURRENT_SOURCE_DIR}/cases/${case_name})
    if(IS_DIRECTORY ${case_path} AND EXISTS ${case_path}/scanner.l)
        add_lex_diff_test(${case_name} ${case_path})
    endif()
endforeach()
