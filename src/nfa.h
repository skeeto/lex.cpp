#pragma once

#include "regex.h"

#include <cstdint>
#include <string>
#include <vector>

namespace lexcpp {

struct NFAEdge {
    std::int32_t target = -1;
    ByteSet      on{};        // empty -> epsilon edge
};

struct NFAState {
    std::vector<NFAEdge> out;
    std::int32_t accept_rule = -1;
};

// Per-condition start states: separate "normal" and "BOL" entry points.
struct CondStart {
    std::int32_t normal = -1;
    std::int32_t bol    = -1;
};

struct NFA {
    std::vector<NFAState> states;
    std::vector<CondStart> cond_starts;   // index 0 == INITIAL
    std::vector<std::string> cond_names;  // index 0 == "INITIAL"
    std::vector<std::uint8_t> cond_excl;  // 0 == inclusive, 1 == exclusive
    std::vector<std::uint8_t> rule_bol;   // per rule id
    std::vector<std::uint8_t> rule_eol;   // per rule id
    std::vector<std::vector<std::int32_t>> eof_rules; // per cond id, rule ids
};

// Add a rule to the NFA. `conds` lists condition ids the rule belongs to;
// pass an empty vector with `any_state == false` to mean "default rule"
// (include from INITIAL plus all inclusive conditions). `any_state == true`
// matches all conditions.
struct RuleSites {
    std::vector<std::int32_t> conds;
    bool any_state = false;
};

void add_rule_to_nfa(NFA& nfa, const Node* root, std::int32_t rule_id,
                     const RuleSites& sites);

void add_eof_rule(NFA& nfa, std::int32_t rule_id, const RuleSites& sites);

// Initialise NFA with the given condition table; index 0 must be INITIAL.
void init_nfa(NFA& nfa,
              const std::vector<std::string>& cond_names,
              const std::vector<std::uint8_t>& cond_excl);

} // namespace lexcpp
