# Reads INPUT as binary and writes OUTPUT as a C++ raw string literal
# inside an inline constexpr std::string_view named kRuntimeTemplate.
file(READ "${INPUT}" CONTENTS)

# Pick a delimiter that does not appear in the file.
set(DELIM "LEXCPP")
set(I 0)
while(CONTENTS MATCHES "\\)${DELIM}\"")
    math(EXPR I "${I}+1")
    set(DELIM "LEXCPP${I}")
endwhile()

set(OUT "// AUTO-GENERATED from runtime/runtime.c.in; do not edit by hand.\n")
string(APPEND OUT "#pragma once\n#include <string_view>\n")
string(APPEND OUT "namespace lexcpp {\n")
string(APPEND OUT "inline constexpr std::string_view kRuntimeTemplate = R\"${DELIM}(")
string(APPEND OUT "${CONTENTS}")
string(APPEND OUT ")${DELIM}\";\n")
string(APPEND OUT "} // namespace lexcpp\n")

file(WRITE "${OUTPUT}" "${OUT}")
