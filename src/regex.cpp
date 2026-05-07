#include "regex.h"

namespace lexcpp {

NodePtr parse_regex(std::string_view /*src*/,
                    const MacroResolver& /*macros*/,
                    bool /*ci*/,
                    Diagnostics& diag,
                    const SourceLoc& loc) {
    diag.error(loc, "regex parser is not implemented yet");
    return nullptr;
}

} // namespace lexcpp
