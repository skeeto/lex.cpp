#include "dfa.hpp"

#include <algorithm>
#include <map>
#include <queue>
#include <unordered_map>
#include <vector>

namespace lexcpp {
namespace {

using StateSet = std::vector<std::int32_t>;

void eps_closure(const NFA& nfa, StateSet& set) {
    std::vector<std::uint8_t> seen(nfa.states.size(), 0);
    StateSet work = set;
    for (auto s : set) seen[static_cast<std::size_t>(s)] = 1;
    while (!work.empty()) {
        auto s = work.back();
        work.pop_back();
        const auto& st = nfa.states[static_cast<std::size_t>(s)];
        for (const auto& e : st.out) {
            if (e.on.empty() && !seen[static_cast<std::size_t>(e.target)]) {
                seen[static_cast<std::size_t>(e.target)] = 1;
                work.push_back(e.target);
                set.push_back(e.target);
            }
        }
    }
    std::sort(set.begin(), set.end());
    set.erase(std::unique(set.begin(), set.end()), set.end());
}

StateSet step(const NFA& nfa, const StateSet& set, unsigned byte) {
    StateSet out;
    for (auto s : set) {
        const auto& st = nfa.states[static_cast<std::size_t>(s)];
        for (const auto& e : st.out) {
            if (!e.on.empty() && e.on.test(byte)) out.push_back(e.target);
        }
    }
    eps_closure(nfa, out);
    return out;
}

struct AcceptPair {
    std::int32_t normal = -1;
    std::int32_t eol    = -1;
    std::vector<std::int32_t> list;     // sorted ASC, deduped
    std::vector<std::int32_t> boundary; // rules whose boundary marker is in set
};

AcceptPair accept_for(const NFA& nfa, const StateSet& set) {
    AcceptPair p;
    std::vector<std::int32_t> rules;
    for (auto s : set) {
        auto r = nfa.states[static_cast<std::size_t>(s)].accept_rule;
        if (r < 0) continue;
        rules.push_back(r);
    }
    std::sort(rules.begin(), rules.end());
    rules.erase(std::unique(rules.begin(), rules.end()), rules.end());
    for (auto r : rules) {
        bool is_eol = nfa.rule_eol[static_cast<std::size_t>(r)] != 0;
        std::int32_t& slot = is_eol ? p.eol : p.normal;
        if (slot < 0 || r < slot) slot = r;
    }
    p.list = std::move(rules);

    // Boundary tracking: for each rule with a variable trail, check
    // whether any of its boundary marker states is in this DFA state's
    // NFA-set. Use a hash set for O(|set|).
    if (!nfa.rule_boundary_states.empty()) {
        for (std::size_t r = 0; r < nfa.rule_boundary_states.size(); ++r) {
            const auto& markers = nfa.rule_boundary_states[r];
            if (markers.empty()) continue;
            for (auto m : markers) {
                if (std::binary_search(set.begin(), set.end(), m)) {
                    p.boundary.push_back(static_cast<std::int32_t>(r));
                    break;
                }
            }
        }
    }
    return p;
}

struct SetKey {
    StateSet v;
    bool operator==(const SetKey& o) const noexcept { return v == o.v; }
};

struct SetHash {
    std::size_t operator()(const SetKey& k) const noexcept {
        std::size_t h = 0;
        for (auto x : k.v) {
            h = h * 1099511628211ULL;
            h ^= static_cast<std::size_t>(static_cast<std::uint32_t>(x));
        }
        return h;
    }
};

// For each class id, return a representative byte (the first byte that
// maps to that class).
std::vector<unsigned> class_reps(const Eclasses& ec) {
    std::vector<unsigned> r(static_cast<std::size_t>(ec.nclasses), 256u);
    for (unsigned b = 0; b < 256; ++b) {
        unsigned c = ec.ec[b];
        if (r[c] == 256u) r[c] = b;
    }
    return r;
}

} // namespace

namespace {

void compute_meta_classes(DFA& dfa) {
    int nc = dfa.nclasses;
    dfa.meta.assign(static_cast<std::size_t>(nc), 0);

    // Build per-class signature: concatenation of next[c] across states.
    // Two classes share a meta if their column vectors are identical.
    std::vector<std::vector<std::int32_t>> col(static_cast<std::size_t>(nc));
    for (auto& v : col) v.reserve(dfa.states.size());
    for (const auto& st : dfa.states) {
        for (int c = 0; c < nc; ++c) {
            col[static_cast<std::size_t>(c)].push_back(
                st.next[static_cast<std::size_t>(c)]);
        }
    }
    std::map<std::vector<std::int32_t>, std::uint8_t> seen;
    int next_id = 0;
    for (int c = 0; c < nc; ++c) {
        auto it = seen.find(col[static_cast<std::size_t>(c)]);
        if (it != seen.end()) {
            dfa.meta[static_cast<std::size_t>(c)] = it->second;
        } else {
            std::uint8_t id = static_cast<std::uint8_t>(next_id++);
            dfa.meta[static_cast<std::size_t>(c)] = id;
            seen.emplace(col[static_cast<std::size_t>(c)], id);
        }
    }
    dfa.nmeta = next_id;
}

} // namespace

DFA build_dfa(const NFA& nfa, bool use_eclasses, bool compute_meta) {
    DFA dfa;
    dfa.eclasses = use_eclasses ? compute_eclasses(nfa) : identity_eclasses();
    dfa.nclasses = dfa.eclasses.nclasses;
    auto reps = class_reps(dfa.eclasses);

    dfa.cond_starts.resize(nfa.cond_starts.size());

    // Add the dead state (index 0).
    DFAState dead{};
    dead.next.assign(static_cast<std::size_t>(dfa.nclasses), -1);
    dfa.states.push_back(dead);

    std::unordered_map<SetKey, std::int32_t, SetHash> index;
    std::vector<StateSet> id_to_set;

    auto get_state = [&](StateSet set) -> std::int32_t {
        SetKey k{std::move(set)};
        auto it = index.find(k);
        if (it != index.end()) return it->second;
        std::int32_t id = static_cast<std::int32_t>(dfa.states.size());
        DFAState s;
        s.next.assign(static_cast<std::size_t>(dfa.nclasses), -1);
        auto ap = accept_for(nfa, k.v);
        s.accept_normal   = ap.normal;
        s.accept_eol      = ap.eol;
        s.accept_list     = std::move(ap.list);
        s.boundary_rules  = std::move(ap.boundary);
        dfa.states.push_back(std::move(s));
        id_to_set.push_back(k.v);
        index.emplace(std::move(k), id);
        return id;
    };

    // Seed with per-condition starts.
    std::queue<std::int32_t> work;
    for (std::size_t c = 0; c < nfa.cond_starts.size(); ++c) {
        StateSet n_set{nfa.cond_starts[c].normal};
        StateSet b_set{nfa.cond_starts[c].bol};
        eps_closure(nfa, n_set);
        eps_closure(nfa, b_set);
        std::int32_t n_id = get_state(std::move(n_set));
        std::int32_t b_id = get_state(std::move(b_set));
        dfa.cond_starts[c] = {n_id, b_id};
        work.push(n_id);
        work.push(b_id);
    }

    while (!work.empty()) {
        auto id = work.front(); work.pop();
        auto idx = static_cast<std::size_t>(id - 1);
        if (idx >= id_to_set.size()) continue;
        StateSet cur = id_to_set[idx];
        for (int cl = 0; cl < dfa.nclasses; ++cl) {
            unsigned rep = reps[static_cast<std::size_t>(cl)];
            if (rep >= 256) continue;       // unused class
            StateSet n = step(nfa, cur, rep);
            if (n.empty()) continue;
            auto exists = index.find(SetKey{n});
            std::int32_t target;
            if (exists != index.end()) {
                target = exists->second;
            } else {
                target = get_state(std::move(n));
                work.push(target);
            }
            dfa.states[static_cast<std::size_t>(id)].next[static_cast<std::size_t>(cl)] = target;
        }
    }

    if (compute_meta) compute_meta_classes(dfa);
    return dfa;
}

CompressedDFA compress_dfa(const DFA& dfa) {
    CompressedDFA c;
    int nstates  = static_cast<int>(dfa.states.size());
    int nclasses = dfa.nclasses;

    c.yy_base.assign(static_cast<std::size_t>(nstates), 0);
    c.yy_def .assign(static_cast<std::size_t>(nstates), 0);

    // Collect exceptions per state.
    struct StateExc {
        std::int32_t state_id;
        std::vector<std::pair<std::int32_t, std::int32_t>> excs;  // (class, target)
    };
    std::vector<StateExc> exc(static_cast<std::size_t>(nstates));
    for (int s = 0; s < nstates; ++s) {
        exc[static_cast<std::size_t>(s)].state_id = s;
        if (s == 0) continue;   // dead state has no transitions
        const auto& st = dfa.states[static_cast<std::size_t>(s)];
        for (int ec = 0; ec < nclasses; ++ec) {
            std::int32_t t = st.next[static_cast<std::size_t>(ec)];
            if (t > 0)        // skip both dead (-1) and dead state (0)
                exc[static_cast<std::size_t>(s)].excs.push_back({ec, t});
        }
    }

    // Pack largest exception lists first; fewer collisions overall.
    std::vector<int> order;
    order.reserve(static_cast<std::size_t>(nstates));
    for (int s = 1; s < nstates; ++s) {
        if (!exc[static_cast<std::size_t>(s)].excs.empty()) order.push_back(s);
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return exc[static_cast<std::size_t>(a)].excs.size()
             > exc[static_cast<std::size_t>(b)].excs.size();
    });

    c.yy_chk.assign(static_cast<std::size_t>(nclasses), -1);
    c.yy_nxt.assign(static_cast<std::size_t>(nclasses), 0);

    auto fits_at = [&](std::int32_t base, const StateExc& se) -> bool {
        for (const auto& [ec, _t] : se.excs) {
            std::size_t i = static_cast<std::size_t>(base + ec);
            if (i < c.yy_chk.size() && c.yy_chk[i] != -1) return false;
        }
        return true;
    };

    auto place_at = [&](std::int32_t base, const StateExc& se) {
        std::int32_t hi = 0;
        for (const auto& [ec, _t] : se.excs) hi = std::max(hi, base + ec + 1);
        if (static_cast<std::size_t>(hi) > c.yy_chk.size()) {
            c.yy_chk.resize(static_cast<std::size_t>(hi), -1);
            c.yy_nxt.resize(static_cast<std::size_t>(hi), 0);
        }
        for (const auto& [ec, t] : se.excs) {
            std::size_t i = static_cast<std::size_t>(base + ec);
            c.yy_chk[i] = se.state_id;
            c.yy_nxt[i] = t;
        }
    };

    for (int s : order) {
        const auto& se = exc[static_cast<std::size_t>(s)];
        // Search lowest viable base. Allow negative base so position 0
        // can be reached; bound by a margin to avoid overshooting.
        std::int32_t best = 0;
        for (std::int32_t base = 0;
             base < static_cast<std::int32_t>(c.yy_chk.size()) + 1; ++base) {
            if (fits_at(base, se)) { best = base; break; }
        }
        c.yy_base[static_cast<std::size_t>(s)] = best;
        place_at(best, se);
    }

    // Pad pool so that any state-class lookup is in-bounds (guards the
    // runtime against reading off the end when a state with empty
    // exception list reuses base 0 against a high class id).
    std::size_t needed = c.yy_chk.size();
    if (needed < static_cast<std::size_t>(nclasses))
        needed = static_cast<std::size_t>(nclasses);
    c.yy_chk.resize(needed, -1);
    c.yy_nxt.resize(needed, 0);
    c.pool_size = static_cast<std::int32_t>(needed);
    return c;
}

} // namespace lexcpp
