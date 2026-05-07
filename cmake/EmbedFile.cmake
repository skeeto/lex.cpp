# Reads INPUT as binary and writes OUTPUT as a C++ raw-string-literal
# constant exposed via std::string_view kRuntimeTemplate.
#
# The contents are split into chunks <= 12000 bytes each. Adjacent
# string literals concatenate at compile time, which keeps each
# individual literal well under MSVC's 16380-byte cap (C2026).
file(READ "${INPUT}" CONTENTS)

# Pick a delimiter that does not appear in the file.
set(DELIM "LEXCPP")
set(I 0)
while(CONTENTS MATCHES "\\)${DELIM}\"")
    math(EXPR I "${I}+1")
    set(DELIM "LEXCPP${I}")
endwhile()

string(LENGTH "${CONTENTS}" TOTAL)
set(CHUNK 12000)
set(OFFSET 0)
set(BODY "")
while(OFFSET LESS TOTAL)
    math(EXPR REMAIN "${TOTAL} - ${OFFSET}")
    if(REMAIN LESS CHUNK)
        set(TAKE ${REMAIN})
    else()
        set(TAKE ${CHUNK})
    endif()
    string(SUBSTRING "${CONTENTS}" ${OFFSET} ${TAKE} PART)
    string(APPEND BODY "R\"${DELIM}(${PART})${DELIM}\"\n")
    math(EXPR OFFSET "${OFFSET} + ${TAKE}")
endwhile()
if(BODY STREQUAL "")
    set(BODY "\"\"")
endif()

set(OUT "// AUTO-GENERATED from runtime/runtime.c.in; do not edit by hand.\n")
string(APPEND OUT "#pragma once\n#include <string_view>\n")
string(APPEND OUT "namespace lexcpp {\n")
string(APPEND OUT "inline constexpr std::string_view kRuntimeTemplate =\n")
string(APPEND OUT "${BODY}")
string(APPEND OUT ";\n")
string(APPEND OUT "} // namespace lexcpp\n")

file(WRITE "${OUTPUT}" "${OUT}")
