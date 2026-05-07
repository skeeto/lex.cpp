#include "regex.h"

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

} // namespace

NodePtr parse_regex(std::string_view src,
                    const MacroResolver& macros,
                    bool case_insensitive,
                    Diagnostics& diag,
                    const SourceLoc& loc) {
    Parser p(src, macros, case_insensitive, diag, loc);
    return p.parse_top();
}

} // namespace lexcpp
