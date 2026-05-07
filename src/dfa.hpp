#pragma once

#include "eclass.hpp"
#include "nfa.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace lexcpp {

struct DFAState {
    // Per-equivalence-class transition; size == DFA::nclasses. -1 = dead.
    std::vector<std::int32_t> next;
    // Earliest accepting rule id whose pattern has no $ anchor; -1 if none.
    std::int32_t accept_normal = -1;
    // Earliest accepting rule id whose pattern ends with $ anchor; -1 if none.
    std::int32_t accept_eol    = -1;
    // All accepting rules in this state's NFA-set (sorted by id ASC).
    std::vector<std::int32_t> accept_list;
};

struct DFAStart {
    std::int32_t normal = -1;
    std::int32_t bol    = -1;
};

struct DFA {
    std::vector<DFAState> states;
    std::vector<DFAStart> cond_starts;        // per condition id
    Eclasses eclasses;                        // byte -> class id
    int nclasses = 256;                       // == eclasses.nclasses

    // Meta-equivalence classes: classes that produce identical
    // transitions in every DFA state are fused. Populated only when
    // build_dfa is asked to compute it. yy_meta[c] = meta-class id.
    std::vector<std::uint8_t> meta;            // empty if not computed
    int nmeta = 0;
};

// Build a DFA. With use_eclasses=false, the alphabet stays at 256 bytes
// (yy_ec is identity). Otherwise compute equivalence classes first.
// If compute_meta is true, also fill in DFA::meta after construction.
DFA build_dfa(const NFA& nfa, bool use_eclasses = false,
              bool compute_meta = false);

} // namespace lexcpp
