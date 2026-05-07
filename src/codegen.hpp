#pragma once

#include "dfa.hpp"
#include "nfa.hpp"
#include "source.hpp"

#include <string>

namespace lexcpp {

struct CodegenInput {
    const LexFile* file = nullptr;
    const NFA*     nfa  = nullptr;
    const DFA*     dfa  = nullptr;
};

[[nodiscard]] std::string emit_c(const CodegenInput& in);

} // namespace lexcpp
