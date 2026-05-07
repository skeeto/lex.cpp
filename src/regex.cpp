#include "regex.hpp"

#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <unordered_set>

namespace lexcpp {
namespace {

NodePtr make(NodeKind k) {
    auto n = std::make_unique<Node>();
    n->kind = k;
    return n;
}

void add_byte_ci(ByteSet& s, unsigned b, bool ci) {
    s.add(b);
    if (ci && b < 128) {
        if (b >= 'a' && b <= 'z') s.add(b - 'a' + 'A');
        if (b >= 'A' && b <= 'Z') s.add(b - 'A' + 'a');
    }
}

void add_range_ci(ByteSet& s, unsigned lo, unsigned hi, bool ci) {
    if (lo > hi) std::swap(lo, hi);
    for (unsigned b = lo; b <= hi; ++b) add_byte_ci(s, b, ci);
}

NodePtr lit_node(unsigned b, bool ci) {
    auto n = make(NodeKind::Class);
    add_byte_ci(n->cls, b, ci);
    return n;
}

class Parser {
public:
    Parser(std::string_view src, const MacroResolver& macros, bool ci,
           Diagnostics& diag, SourceLoc loc)
        : src_(src), macros_(macros), ci_(ci), diag_(diag), loc_(std::move(loc)) {}

    NodePtr parse_top() {
        // Top-level: optional ^ at start, optional $ at end.
        bool bol = false, eol = false;
        if (peek() == '^') { bol = true; advance(); }
        NodePtr body = parse_alt();
        if (peek() == '$') { eol = true; advance(); }
        if (!at_end()) {
            diag_.error(loc_, "unexpected character in regex: '" +
                              std::string(1, peek()) + "'");
            return nullptr;
        }
        if (eol) {
            auto e = make(NodeKind::AnchorEOL);
            body = concat(std::move(body), std::move(e));
        }
        if (bol) {
            auto a = make(NodeKind::AnchorBOL);
            body = concat(std::move(a), std::move(body));
        }
        return body;
    }

private:
    std::string_view src_;
    std::size_t pos_ = 0;
    const MacroResolver& macros_;
    bool ci_;
    Diagnostics& diag_;
    SourceLoc loc_;
    std::unordered_set<std::string> expanding_;

    bool at_end() const { return pos_ >= src_.size(); }
    char peek(std::size_t k = 0) const {
        return pos_ + k < src_.size() ? src_[pos_ + k] : '\0';
    }
    char advance() {
        return pos_ < src_.size() ? src_[pos_++] : '\0';
    }

    static NodePtr concat(NodePtr a, NodePtr b) {
        if (!a) return b;
        if (!b) return a;
        auto n = make(NodeKind::Concat);
        n->a = std::move(a);
        n->b = std::move(b);
        return n;
    }

    NodePtr parse_alt() {
        NodePtr l = parse_concat();
        while (peek() == '|') {
            advance();
            NodePtr r = parse_concat();
            auto n = make(NodeKind::Alt);
            n->a = std::move(l);
            n->b = std::move(r);
            l = std::move(n);
        }
        return l;
    }

    bool is_concat_terminator(char c) const {
        return c == '\0' || c == '|' || c == ')' || c == '$';
    }

    NodePtr parse_concat() {
        NodePtr acc;
        while (!at_end() && !is_concat_terminator(peek())) {
            NodePtr p = parse_postfix();
            if (!p) break;
            acc = concat(std::move(acc), std::move(p));
        }
        if (!acc) acc = make(NodeKind::Empty);
        return acc;
    }

    NodePtr parse_postfix() {
        NodePtr p = parse_primary();
        if (!p) return nullptr;
        switch (peek()) {
            case '*': advance(); { auto n = make(NodeKind::Star); n->a = std::move(p); return n; }
            case '+': advance(); { auto n = make(NodeKind::Plus); n->a = std::move(p); return n; }
            case '?': advance(); { auto n = make(NodeKind::Question); n->a = std::move(p); return n; }
            case '{':
                if (std::isdigit(static_cast<unsigned char>(peek(1)))) {
                    auto rep = parse_repeat(std::move(p));
                    return rep;
                }
                return p;
            default: return p;
        }
    }

    NodePtr parse_repeat(NodePtr p) {
        std::size_t save = pos_;
        advance(); // '{'
        std::uint32_t lo = 0;
        bool any = false;
        while (std::isdigit(static_cast<unsigned char>(peek()))) {
            lo = lo * 10 + static_cast<unsigned>(advance() - '0');
            any = true;
        }
        if (!any) { pos_ = save; return p; }
        std::uint32_t hi = lo;
        if (peek() == ',') {
            advance();
            if (std::isdigit(static_cast<unsigned char>(peek()))) {
                hi = 0;
                while (std::isdigit(static_cast<unsigned char>(peek())))
                    hi = hi * 10 + static_cast<unsigned>(advance() - '0');
            } else {
                hi = Node::kInf;
            }
        }
        if (peek() != '}') { pos_ = save; return p; }
        advance();
        auto n = make(NodeKind::Repeat);
        n->a = std::move(p);
        n->lo = lo;
        n->hi = hi;
        return n;
    }

    NodePtr parse_primary() {
        char c = peek();
        if (c == '(') {
            advance();
            NodePtr inner = parse_alt();
            if (peek() != ')') {
                diag_.error(loc_, "unmatched '(' in regex");
                return nullptr;
            }
            advance();
            return inner ? std::move(inner) : make(NodeKind::Empty);
        }
        if (c == '[') return parse_class();
        if (c == '"') return parse_quoted();
        if (c == '{') return parse_macro();
        if (c == '.') { advance(); return any_byte_except_nl(); }
        if (c == '\\') return parse_escape_atom();
        if (c == ')' || c == '|' || c == '$') return nullptr;
        if (c == '\0') return nullptr;
        advance();
        return lit_node(static_cast<unsigned char>(c), ci_);
    }

    NodePtr any_byte_except_nl() {
        auto n = make(NodeKind::Class);
        for (unsigned b = 0; b < 256; ++b) if (b != '\n') n->cls.add(b);
        return n;
    }

    NodePtr parse_quoted() {
        advance(); // opening "
        NodePtr acc;
        while (!at_end() && peek() != '"') {
            unsigned b;
            if (peek() == '\\') b = read_escape_byte();
            else                b = static_cast<unsigned char>(advance());
            acc = concat(std::move(acc), lit_node(b, ci_));
        }
        if (peek() != '"') {
            diag_.error(loc_, "unterminated quoted string in regex");
            return acc ? std::move(acc) : make(NodeKind::Empty);
        }
        advance();
        if (!acc) acc = make(NodeKind::Empty);
        return acc;
    }

    NodePtr parse_macro() {
        std::size_t save = pos_;
        advance(); // '{'
        std::string name;
        while (!at_end() && (std::isalnum(static_cast<unsigned char>(peek())) ||
                             peek() == '_' || peek() == '-')) {
            name.push_back(advance());
        }
        if (name.empty() || peek() != '}') {
            pos_ = save;
            advance(); // treat as literal {
            return lit_node('{', ci_);
        }
        advance(); // '}'
        if (expanding_.count(name)) {
            diag_.error(loc_, "recursive macro expansion: {" + name + "}");
            return make(NodeKind::Empty);
        }
        auto exp = macros_(name);
        if (!exp) {
            diag_.error(loc_, "undefined macro: {" + name + "}");
            return make(NodeKind::Empty);
        }
        expanding_.insert(name);
        Parser sub(*exp, macros_, ci_, diag_, loc_);
        sub.expanding_ = expanding_;
        NodePtr inner = sub.parse_alt();
        expanding_.erase(name);
        if (!inner) inner = make(NodeKind::Empty);
        return inner;
    }

    NodePtr parse_class() {
        advance(); // '['
        auto n = make(NodeKind::Class);
        bool neg = false;
        if (peek() == '^') { neg = true; advance(); }
        // First char may be ']' literally.
        bool first = true;
        while (!at_end() && (first || peek() != ']')) {
            first = false;
            // POSIX character class: [:NAME:] only valid inside [].
            if (peek() == '[' && peek(1) == ':') {
                if (try_posix_class(n->cls)) continue;
            }
            unsigned a = read_class_byte();
            if (peek() == '-' && peek(1) != ']' && peek(1) != '\0') {
                advance();
                unsigned b = read_class_byte();
                add_range_ci(n->cls, a, b, ci_);
            } else {
                add_byte_ci(n->cls, a, ci_);
            }
        }
        if (peek() != ']') {
            diag_.error(loc_, "unterminated character class");
            return n;
        }
        advance();
        if (neg) {
            ByteSet inv;
            for (unsigned b = 0; b < 256; ++b) if (!n->cls.test(b)) inv.add(b);
            n->cls = inv;
        }
        return n;
    }

    // POSIX [:NAME:] support. Returns true if a class was recognised
    // and consumed (bits OR'd into `out`); false if the input doesn't
    // look like one (caller falls back to literal handling).
    bool try_posix_class(ByteSet& out) {
        std::size_t save = pos_;
        advance(); advance(); // '[' ':'
        std::string name;
        while (!at_end() && peek() != ':' && peek() != ']' &&
               name.size() < 16) {
            char c = peek();
            if (!std::isalpha(static_cast<unsigned char>(c))) break;
            name.push_back(advance());
        }
        if (peek() != ':' || peek(1) != ']') { pos_ = save; return false; }
        advance(); advance(); // ':' ']'

        auto add = [&](unsigned lo, unsigned hi) {
            for (unsigned b = lo; b <= hi; ++b) out.add(b);
        };
        if      (name == "alpha")  { add('A','Z'); add('a','z'); }
        else if (name == "upper")  { add('A','Z'); if (ci_) add('a','z'); }
        else if (name == "lower")  { add('a','z'); if (ci_) add('A','Z'); }
        else if (name == "digit")  { add('0','9'); }
        else if (name == "xdigit") { add('0','9'); add('A','F'); add('a','f'); }
        else if (name == "alnum")  { add('A','Z'); add('a','z'); add('0','9'); }
        else if (name == "space")  {
            for (unsigned b : {0x20u, 0x09u, 0x0au, 0x0bu, 0x0cu, 0x0du}) out.add(b);
        }
        else if (name == "blank")  { out.add(' '); out.add('\t'); }
        else if (name == "cntrl")  { add(0x00, 0x1f); out.add(0x7f); }
        else if (name == "print")  { add(0x20, 0x7e); }
        else if (name == "graph")  { add(0x21, 0x7e); }
        else if (name == "punct")  {
            for (unsigned b = 0x21; b <= 0x7e; ++b)
                if (!std::isalnum(static_cast<unsigned char>(b))) out.add(b);
        }
        else {
            diag_.error(loc_, "unknown POSIX character class: [:" + name + ":]");
        }
        return true;
    }

    unsigned read_class_byte() {
        if (peek() == '\\') return read_escape_byte();
        return static_cast<unsigned char>(advance());
    }

    NodePtr parse_escape_atom() {
        unsigned b = read_escape_byte();
        return lit_node(b, ci_);
    }

    unsigned read_escape_byte() {
        // Consumes a byte starting at '\\'.
        advance(); // '\\'
        char c = peek();
        if (c == '\0') return 0;
        switch (c) {
            case 'n': advance(); return '\n';
            case 't': advance(); return '\t';
            case 'r': advance(); return '\r';
            case 'f': advance(); return '\f';
            case 'v': advance(); return '\v';
            case 'a': advance(); return '\a';
            case 'b': advance(); return '\b';
            case '\\': advance(); return '\\';
            case '\'': advance(); return '\'';
            case '"': advance(); return '"';
            case '/': advance(); return '/';
            case '0': case '1': case '2': case '3':
            case '4': case '5': case '6': case '7': {
                unsigned v = 0;
                for (int i = 0; i < 3 && std::isdigit(static_cast<unsigned char>(peek())) &&
                                  peek() >= '0' && peek() <= '7'; ++i) {
                    v = v * 8 + static_cast<unsigned>(advance() - '0');
                }
                return v & 0xff;
            }
            case 'x': {
                advance();
                unsigned v = 0;
                int n = 0;
                while (n < 2 && std::isxdigit(static_cast<unsigned char>(peek()))) {
                    char h = advance();
                    unsigned d = (h <= '9') ? static_cast<unsigned>(h - '0')
                                            : static_cast<unsigned>((h | 0x20) - 'a' + 10);
                    v = v * 16 + d;
                    ++n;
                }
                return v & 0xff;
            }
            default:
                advance();
                return static_cast<unsigned char>(c);
        }
    }
};

int find_top_level_slash(std::string_view src) {
    int depth = 0;
    bool in_class = false, in_quoted = false;
    for (std::size_t i = 0; i < src.size(); ++i) {
        char c = src[i];
        if (in_class)  { if (c == ']') in_class = false; continue; }
        if (in_quoted) {
            if (c == '\\' && i + 1 < src.size()) ++i;
            else if (c == '"') in_quoted = false;
            continue;
        }
        if (c == '\\' && i + 1 < src.size()) { ++i; continue; }
        if (c == '[')  { in_class = true;  continue; }
        if (c == '"')  { in_quoted = true; continue; }
        if (c == '(')  { ++depth;          continue; }
        if (c == ')')  { if (depth) --depth; continue; }
        if (c == '/' && depth == 0) return static_cast<int>(i);
    }
    return -1;
}

bool peek_is_eol_anchor(const Node* n) {
    if (!n) return false;
    if (n->kind == NodeKind::AnchorEOL) return true;
    if (n->kind == NodeKind::Concat && n->b)
        return n->b->kind == NodeKind::AnchorEOL;
    return false;
}

NodePtr strip_eol_anchor(NodePtr n) {
    if (!n) return n;
    if (n->kind == NodeKind::AnchorEOL) return nullptr;
    if (n->kind == NodeKind::Concat && n->b &&
        n->b->kind == NodeKind::AnchorEOL) {
        return std::move(n->a);
    }
    return n;
}

} // namespace

NodePtr parse_regex(std::string_view src,
                    const MacroResolver& macros,
                    bool case_insensitive,
                    Diagnostics& diag,
                    const SourceLoc& loc) {
    Parser p(src, macros, case_insensitive, diag, loc);
    return p.parse_top();
}

int fixed_length(const Node* n) {
    if (!n) return 0;
    switch (n->kind) {
        case NodeKind::Empty:      return 0;
        case NodeKind::Class:      return 1;
        case NodeKind::Concat: {
            int la = fixed_length(n->a.get());
            if (la < 0) return -1;
            int lb = fixed_length(n->b.get());
            if (lb < 0) return -1;
            return la + lb;
        }
        case NodeKind::Alt: {
            int la = fixed_length(n->a.get());
            int lb = fixed_length(n->b.get());
            if (la < 0 || lb < 0 || la != lb) return -1;
            return la;
        }
        case NodeKind::Star: case NodeKind::Plus: case NodeKind::Question:
            return -1;
        case NodeKind::Repeat:
            if (n->lo == n->hi && n->hi != Node::kInf) {
                int la = fixed_length(n->a.get());
                if (la < 0) return -1;
                return la * static_cast<int>(n->lo);
            }
            return -1;
        case NodeKind::AnchorBOL: case NodeKind::AnchorEOL:
        case NodeKind::TrailBoundary:
            return 0;   // zero-width assertions / markers
    }
    return -1;
}

ParsedPattern parse_pattern(std::string_view src,
                            const MacroResolver& macros,
                            bool case_insensitive,
                            Diagnostics& diag,
                            const SourceLoc& loc) {
    ParsedPattern out;
    int slash = find_top_level_slash(src);
    if (slash < 0) {
        out.tree = parse_regex(src, macros, case_insensitive, diag, loc);
        if (out.tree && peek_is_eol_anchor(out.tree.get())) {
            out.eol_anchored = true;
        }
        return out;
    }
    auto r_src = src.substr(0, static_cast<std::size_t>(slash));
    auto s_src = src.substr(static_cast<std::size_t>(slash) + 1);
    auto r = parse_regex(r_src, macros, case_insensitive, diag, loc);
    auto s = parse_regex(s_src, macros, case_insensitive, diag, loc);
    if (!r || !s) return {};

    bool s_had_eol = peek_is_eol_anchor(s.get());
    auto s_body = strip_eol_anchor(std::move(s));

    if (peek_is_eol_anchor(r.get())) {
        diag.error(loc, "`$` is not permitted on the head of `r/s`");
        return {};
    }

    int len = fixed_length(s_body.get());
    if (len >= 0) {
        // Fixed-length trail: combine r and s into one regex and rewind
        // by `len` (plus 1 if `s$` -- counts the implicit \n) on accept.
        out.trail_len = len + (s_had_eol ? 1 : 0);
        auto cat = std::make_unique<Node>();
        cat->kind = NodeKind::Concat;
        cat->a = std::move(r);
        if (s_body) {
            cat->b = std::move(s_body);
        } else {
            cat->b = std::make_unique<Node>();
            cat->b->kind = NodeKind::Empty;
        }
        out.tree = std::move(cat);
        if (s_had_eol) {
            auto nl = std::make_unique<Node>();
            nl->kind = NodeKind::Class;
            nl->cls.add('\n');
            auto wrap = std::make_unique<Node>();
            wrap->kind = NodeKind::Concat;
            wrap->a = std::move(out.tree);
            wrap->b = std::move(nl);
            out.tree = std::move(wrap);
        }
        return out;
    }

    // Variable-length trail. Emit r BOUNDARY s; the boundary marker
    // tells the NFA/DFA where to rewind at accept.
    if (fixed_length(r.get()) < 0) {
        // Both head and tail variable -- where r ends is ambiguous.
        // Same diagnostic flex emits.
        diag.warn(loc, "dangerous trailing context (r and s both variable-length)");
    }
    out.trail_len = -1;
    auto bound = std::make_unique<Node>();
    bound->kind = NodeKind::TrailBoundary;
    auto inner = std::make_unique<Node>();
    inner->kind = NodeKind::Concat;
    inner->a = std::move(bound);
    if (s_body) inner->b = std::move(s_body);
    else { inner->b = std::make_unique<Node>(); inner->b->kind = NodeKind::Empty; }
    if (s_had_eol) {
        auto nl = std::make_unique<Node>();
        nl->kind = NodeKind::Class;
        nl->cls.add('\n');
        auto wrap = std::make_unique<Node>();
        wrap->kind = NodeKind::Concat;
        wrap->a = std::move(inner);
        wrap->b = std::move(nl);
        inner = std::move(wrap);
    }
    auto cat = std::make_unique<Node>();
    cat->kind = NodeKind::Concat;
    cat->a = std::move(r);
    cat->b = std::move(inner);
    out.tree = std::move(cat);
    return out;
}

} // namespace lexcpp
