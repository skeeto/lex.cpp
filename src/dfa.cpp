#include "dfa.h"

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
};

AcceptPair accept_for(const NFA& nfa, const StateSet& set) {
    AcceptPair p;
    for (auto s : set) {
        auto r = nfa.states[static_cast<std::size_t>(s)].accept_rule;
        if (r < 0) continue;
        bool is_eol = nfa.rule_eol[static_cast<std::size_t>(r)] != 0;
        std::int32_t& slot = is_eol ? p.eol : p.normal;
        if (slot < 0 || r < slot) slot = r;
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

} // namespace

DFA build_dfa(const NFA& nfa) {
    DFA dfa;
    dfa.cond_starts.resize(nfa.cond_starts.size());

    // Add the dead state (index 0) so transitions can target -1; use
    // -1 only as a sentinel and never as a valid index.
    DFAState dead{};
    dead.next.fill(-1);
    dfa.states.push_back(dead);

    std::unordered_map<SetKey, std::int32_t, SetHash> index;
    std::vector<StateSet> id_to_set;

    auto get_state = [&](StateSet set) -> std::int32_t {
        SetKey k{std::move(set)};
        auto it = index.find(k);
        if (it != index.end()) return it->second;
        std::int32_t id = static_cast<std::int32_t>(dfa.states.size());
        DFAState s;
        s.next.fill(-1);
        auto ap = accept_for(nfa, k.v);
        s.accept_normal = ap.normal;
        s.accept_eol    = ap.eol;
        dfa.states.push_back(s);
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
        // Note: id_to_set is keyed off non-dead states (id - 1).
        auto idx = static_cast<std::size_t>(id - 1);
        if (idx >= id_to_set.size()) continue;
        StateSet cur = id_to_set[idx];
        for (unsigned b = 0; b < 256; ++b) {
            StateSet n = step(nfa, cur, b);
            if (n.empty()) continue;
            auto exists = index.find(SetKey{n});
            std::int32_t target;
            if (exists != index.end()) {
                target = exists->second;
            } else {
                target = get_state(std::move(n));
                work.push(target);
            }
            dfa.states[static_cast<std::size_t>(id)].next[b] = target;
        }
    }

    return dfa;
}

} // namespace lexcpp
