#pragma once

#include "regex.h"

#include <cstdint>
#include <vector>

namespace lexcpp {

struct NFAEdge {
    std::int32_t target = -1;     // -1 == epsilon-only
    ByteSet      on{};            // ignored for epsilon edges
    bool         epsilon = false;
};

struct NFAState {
    std::vector<NFAEdge> out;
    std::int32_t accept_rule = -1;     // -1 == not accepting
    bool anchor_bol = false;
    bool anchor_eol = false;
};

struct NFA {
    std::vector<NFAState> states;
    std::vector<std::int32_t> rule_starts; // per-rule start state
};

// Append a rule to nfa, returning its start state. rule_id is stored
// on the accepting state.
std::int32_t add_rule(NFA& nfa, const Node& root, std::int32_t rule_id);

} // namespace lexcpp
