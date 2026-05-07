#pragma once

#include "nfa.h"

#include <array>
#include <cstdint>
#include <vector>

namespace lexcpp {

struct DFAState {
    std::array<std::int32_t, 256> next{};   // transition per byte; -1 = dead
    std::int32_t accept_rule = -1;          // earliest rule id, -1 = none
};

struct DFA {
    std::vector<DFAState> states;
    std::int32_t start = 0;
    std::int32_t start_bol = 0; // start state when at BOL (may equal start)
};

DFA build_dfa(const NFA& nfa);

} // namespace lexcpp
