#include "source.h"

namespace lexcpp {

std::optional<LexFile> parse_lex_file(std::string_view path,
                                      std::string_view /*contents*/,
                                      Diagnostics& diag) {
    diag.error({std::string(path), 1, 1},
               "lex source parser is not implemented yet");
    return std::nullopt;
}

} // namespace lexcpp
