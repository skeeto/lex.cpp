#include "nfa.hpp"

#include <cassert>
#include <utility>

namespace lexcpp {
namespace {

std::int32_t new_state(NFA& nfa) {
    nfa.states.emplace_back();
    return static_cast<std::int32_t>(nfa.states.size() - 1);
}

void add_eps(NFA& nfa, std::int32_t from, std::int32_t to) {
    nfa.states[static_cast<std::size_t>(from)].out.push_back({to, {}});
}

void add_class(NFA& nfa, std::int32_t from, std::int32_t to, const ByteSet& on) {
    NFAEdge e;
    e.target = to;
    e.on = on;
    nfa.states[static_cast<std::size_t>(from)].out.push_back(std::move(e));
}

struct Frag { std::int32_t s; std::int32_t e; };

// Strip leading/trailing anchor wrappers from the regex root. Returns
// the unwrapped tree along with the anchor flags.
const Node* unwrap_anchors(const Node* root, bool& bol, bool& eol) {
    bol = false; eol = false;
    if (!root) return root;
    if (root->kind == NodeKind::Concat && root->a &&
        root->a->kind == NodeKind::AnchorBOL) {
        bol = true;
        root = root->b.get();
    }
    if (!root) return root;
    if (root->kind == NodeKind::Concat && root->b &&
        root->b->kind == NodeKind::AnchorEOL) {
        eol = true;
        // We need to drop the trailing anchor; descend.
        // For simplicity, return a temporary tree -- but we cannot
        // mutate the original. So we look one level: assume that at
        // most one level of trailing concat-with-eol exists (true, given
        // how parse_top builds the tree).
        root = root->a.get();
    }
    return root;
}

Frag compile(NFA& nfa, const Node* n);

Frag compile_class(NFA& nfa, const ByteSet& on) {
    auto s = new_state(nfa);
    auto e = new_state(nfa);
    add_class(nfa, s, e, on);
    return {s, e};
}

Frag compile_concat(NFA& nfa, const Node* n) {
    Frag a = compile(nfa, n->a.get());
    Frag b = compile(nfa, n->b.get());
    add_eps(nfa, a.e, b.s);
    return {a.s, b.e};
}

Frag compile_alt(NFA& nfa, const Node* n) {
    Frag a = compile(nfa, n->a.get());
    Frag b = compile(nfa, n->b.get());
    auto s = new_state(nfa);
    auto e = new_state(nfa);
    add_eps(nfa, s, a.s);
    add_eps(nfa, s, b.s);
    add_eps(nfa, a.e, e);
    add_eps(nfa, b.e, e);
    return {s, e};
}

Frag compile_star(NFA& nfa, const Node* n) {
    Frag a = compile(nfa, n->a.get());
    auto s = new_state(nfa);
    auto e = new_state(nfa);
    add_eps(nfa, s, a.s);
    add_eps(nfa, s, e);
    add_eps(nfa, a.e, a.s);
    add_eps(nfa, a.e, e);
    return {s, e};
}

Frag compile_plus(NFA& nfa, const Node* n) {
    Frag a = compile(nfa, n->a.get());
    auto s = new_state(nfa);
    auto e = new_state(nfa);
    add_eps(nfa, s, a.s);
    add_eps(nfa, a.e, a.s);
    add_eps(nfa, a.e, e);
    return {s, e};
}

Frag compile_question(NFA& nfa, const Node* n) {
    Frag a = compile(nfa, n->a.get());
    auto s = new_state(nfa);
    auto e = new_state(nfa);
    add_eps(nfa, s, a.s);
    add_eps(nfa, s, e);
    add_eps(nfa, a.e, e);
    return {s, e};
}

Frag compile_empty(NFA& nfa) {
    auto s = new_state(nfa);
    auto e = new_state(nfa);
    add_eps(nfa, s, e);
    return {s, e};
}

Frag compile_repeat(NFA& nfa, const Node* n) {
    // Unroll lo copies, then either Star (if hi == kInf) or up to (hi-lo)
    // optional copies.
    Frag head{-1, -1};
    auto append = [&](Frag f) {
        if (head.s == -1) head = f;
        else { add_eps(nfa, head.e, f.s); head.e = f.e; }
    };
    for (std::uint32_t i = 0; i < n->lo; ++i) {
        append(compile(nfa, n->a.get()));
    }
    if (n->hi == Node::kInf) {
        // Append a star of the body.
        Node star;
        star.kind = NodeKind::Star;
        // We need a non-owning view of the body. Build the star manually.
        Frag a = compile(nfa, n->a.get());
        auto s = new_state(nfa);
        auto e = new_state(nfa);
        add_eps(nfa, s, a.s);
        add_eps(nfa, s, e);
        add_eps(nfa, a.e, a.s);
        add_eps(nfa, a.e, e);
        Frag star_f{s, e};
        append(star_f);
    } else {
        std::uint32_t opt = (n->hi >= n->lo) ? n->hi - n->lo : 0;
        for (std::uint32_t i = 0; i < opt; ++i) {
            // Optional copy of the body.
            Frag a = compile(nfa, n->a.get());
            auto s = new_state(nfa);
            auto e = new_state(nfa);
            add_eps(nfa, s, a.s);
            add_eps(nfa, s, e);
            add_eps(nfa, a.e, e);
            append({s, e});
        }
    }
    if (head.s == -1) head = compile_empty(nfa);
    return head;
}

Frag compile(NFA& nfa, const Node* n) {
    if (!n) return compile_empty(nfa);
    switch (n->kind) {
        case NodeKind::Empty:    return compile_empty(nfa);
        case NodeKind::Class:    return compile_class(nfa, n->cls);
        case NodeKind::Concat:   return compile_concat(nfa, n);
        case NodeKind::Alt:      return compile_alt(nfa, n);
        case NodeKind::Star:     return compile_star(nfa, n);
        case NodeKind::Plus:     return compile_plus(nfa, n);
        case NodeKind::Question: return compile_question(nfa, n);
        case NodeKind::Repeat:   return compile_repeat(nfa, n);
        case NodeKind::AnchorBOL:
        case NodeKind::AnchorEOL:
            // Anchors should have been stripped at the rule boundary; if
            // they appear elsewhere, treat as no-ops (we don't support
            // mid-pattern anchors for our minimal scope).
            return compile_empty(nfa);
    }
    return compile_empty(nfa);
}

void connect_to_conds(NFA& nfa, std::int32_t entry, bool only_bol,
                      const RuleSites& sites) {
    auto link = [&](std::int32_t cond_id) {
        auto& cs = nfa.cond_starts[static_cast<std::size_t>(cond_id)];
        // Always reachable from BOL entry.
        add_eps(nfa, cs.bol, entry);
        if (!only_bol) add_eps(nfa, cs.normal, entry);
    };
    if (sites.any_state) {
        for (std::size_t i = 0; i < nfa.cond_starts.size(); ++i)
            link(static_cast<std::int32_t>(i));
        return;
    }
    if (sites.conds.empty()) {
        // Default: INITIAL + all inclusive conditions.
        for (std::size_t i = 0; i < nfa.cond_starts.size(); ++i) {
            if (i == 0 || nfa.cond_excl[i] == 0)
                link(static_cast<std::int32_t>(i));
        }
        return;
    }
    for (auto cid : sites.conds) link(cid);
}

} // namespace

void init_nfa(NFA& nfa,
              const std::vector<std::string>& cond_names,
              const std::vector<std::uint8_t>& cond_excl) {
    nfa.states.clear();
    nfa.cond_names = cond_names;
    nfa.cond_excl  = cond_excl;
    nfa.cond_starts.clear();
    nfa.cond_starts.resize(cond_names.size());
    nfa.eof_rules.clear();
    nfa.eof_rules.resize(cond_names.size());
    for (std::size_t i = 0; i < cond_names.size(); ++i) {
        auto n = new_state(nfa);
        auto b = new_state(nfa);
        nfa.cond_starts[i] = {n, b};
    }
}

void add_rule_to_nfa(NFA& nfa, const Node* root, std::int32_t rule_id,
                     const RuleSites& sites) {
    bool bol = false, eol = false;
    const Node* body = unwrap_anchors(root, bol, eol);
    Frag f = compile(nfa, body);
    nfa.states[static_cast<std::size_t>(f.e)].accept_rule = rule_id;
    while (static_cast<std::int32_t>(nfa.rule_bol.size()) <= rule_id) {
        nfa.rule_bol.push_back(0);
        nfa.rule_eol.push_back(0);
    }
    nfa.rule_bol[static_cast<std::size_t>(rule_id)] = bol ? 1 : 0;
    nfa.rule_eol[static_cast<std::size_t>(rule_id)] = eol ? 1 : 0;
    connect_to_conds(nfa, f.s, bol, sites);
}

void add_eof_rule(NFA& nfa, std::int32_t rule_id, const RuleSites& sites) {
    auto link = [&](std::int32_t cond_id) {
        nfa.eof_rules[static_cast<std::size_t>(cond_id)].push_back(rule_id);
    };
    if (sites.any_state) {
        for (std::size_t i = 0; i < nfa.cond_starts.size(); ++i)
            link(static_cast<std::int32_t>(i));
    } else if (sites.conds.empty()) {
        for (std::size_t i = 0; i < nfa.cond_starts.size(); ++i) {
            if (i == 0 || nfa.cond_excl[i] == 0)
                link(static_cast<std::int32_t>(i));
        }
    } else {
        for (auto c : sites.conds) link(c);
    }
}

} // namespace lexcpp
