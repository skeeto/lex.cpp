#pragma once

#include "nfa.h"

#include <array>
#include <cstdint>
#include <vector>

namespace lexcpp {

struct DFAState {
    // Transition table per byte; -1 means dead.
    std::array<std::int32_t, 256> next{};
    // Earliest accepting rule id whose pattern has no $ anchor; -1 if none.
    std::int32_t accept_normal = -1;
    // Earliest accepting rule id whose pattern ends with $ anchor; -1 if none.
    std::int32_t accept_eol    = -1;
};

struct DFAStart {
    std::int32_t normal = -1;
    std::int32_t bol    = -1;
};

struct DFA {
    std::vector<DFAState> states;
    std::vector<DFAStart> cond_starts;        // per condition id
};

DFA build_dfa(const NFA& nfa);

} // namespace lexcpp
