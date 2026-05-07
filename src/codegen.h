#pragma once

#include "dfa.h"
#include "source.h"

#include <string>

namespace lexcpp {

struct CodegenInput {
    const LexFile* file = nullptr;
    const DFA*     dfa  = nullptr;
};

[[nodiscard]] std::string emit_c(const CodegenInput& in);

} // namespace lexcpp
