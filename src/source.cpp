#include "source.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>

namespace lexcpp {
namespace {

bool is_ident_start(unsigned char c) {
    return std::isalpha(c) || c == '_';
}
bool is_ident_cont(unsigned char c) {
    return std::isalnum(c) || c == '_' || c == '-';
}

struct Parser {
    std::string_view src;
    std::size_t pos = 0;
    std::uint32_t line = 1;
    std::uint32_t col = 1;
    std::string file;
    Diagnostics& diag;
    LexFile out{};

    Parser(std::string_view s, std::string f, Diagnostics& d)
        : src(s), file(std::move(f)), diag(d) {
        out.origin = {file, 1, 1};
    }

    SourceLoc loc() const { return {file, line, col}; }

    bool at_end() const { return pos >= src.size(); }
    char peek(std::size_t k = 0) const {
        return pos + k < src.size() ? src[pos + k] : '\0';
    }
    char get() {
        if (pos >= src.size()) return '\0';
        char c = src[pos++];
        if (c == '\n') { ++line; col = 1; }
        else           { ++col; }
        return c;
    }

    // Read until end of line (not consuming the newline). Used for
    // captures that should retain trailing whitespace stripped.
    std::string read_line_keep_nl() {
        std::string s;
        while (!at_end()) {
            char c = src[pos];
            s.push_back(c);
            get();
            if (c == '\n') break;
        }
        return s;
    }

    void skip_blank_line() {
        while (!at_end() && src[pos] != '\n') get();
        if (!at_end()) get();
    }

    // Strip trailing CR (Windows line endings).
    static void rstrip(std::string& s) {
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' ||
                              s.back() == ' '  || s.back() == '\t'))
            s.pop_back();
    }

    void parse() {
        parse_section1();
        if (!at_end()) parse_section2();
        if (!at_end()) parse_section3();
    }

    // ---------------------------------------------------- section 1
    void parse_section1() {
        while (!at_end()) {
            // Are we at start of a line?
            char c0 = peek();
            if (c0 == '\n') { get(); continue; }

            // %% on a line by itself ends section 1.
            if (c0 == '%' && peek(1) == '%') {
                std::size_t save = pos;
                get(); get();
                // Skip optional trailing whitespace and newline.
                while (!at_end() && (peek() == ' ' || peek() == '\t')) get();
                if (peek() == '\n') { get(); return; }
                if (at_end()) return;
                // Not a section divider after all -- rewind.
                pos = save; line = 1; // we don't track exact rewind; safe enough since col only
                // Treat as ordinary line.
                std::string l = read_line_keep_nl();
                out.section1_verbatim += l;
                continue;
            }

            // %top { ... }  -- code emitted before standard includes.
            if (c0 == '%' && src.compare(pos, 4, "%top") == 0) {
                std::size_t save = pos;
                std::uint32_t save_line = line;
                std::uint32_t save_col = col;
                for (int k = 0; k < 4; ++k) get();
                while (!at_end() && (peek() == ' ' || peek() == '\t')) get();
                if (peek() == '{') {
                    if (out.section_top.empty()) out.section_top_loc = loc();
                    get();      // consume '{'
                    int depth = 1;
                    while (!at_end() && depth > 0) {
                        char c = peek();
                        if (c == '{') { ++depth; out.section_top.push_back(get()); }
                        else if (c == '}') {
                            --depth;
                            if (depth == 0) { get(); break; }
                            out.section_top.push_back(get());
                        } else {
                            out.section_top.push_back(get());
                        }
                    }
                    while (!at_end() && peek() != '\n') get();
                    if (peek() == '\n') get();
                    continue;
                }
                pos = save; line = save_line; col = save_col;
            }

            // %{ ... %} verbatim block
            if (c0 == '%' && peek(1) == '{') {
                if (out.section1_verbatim.empty())
                    out.section1_loc = loc();
                get(); get();
                while (!at_end() && peek() != '\n') get();
                if (peek() == '\n') get();
                while (!at_end()) {
                    if (peek() == '%' && peek(1) == '}') {
                        get(); get();
                        while (!at_end() && peek() != '\n') get();
                        if (peek() == '\n') get();
                        break;
                    }
                    out.section1_verbatim += read_line_keep_nl();
                }
                continue;
            }

            // %option ...
            if (c0 == '%' && (src.compare(pos, 7, "%option") == 0 ||
                              src.compare(pos, 8, "%options") == 0)) {
                // consume %option keyword
                while (!at_end() && peek() != ' ' && peek() != '\t' &&
                       peek() != '\n') get();
                std::string rest;
                while (!at_end() && peek() != '\n') rest.push_back(get());
                if (peek() == '\n') get();
                parse_options(rest);
                continue;
            }

            // %s NAME ...   or   %x NAME ...
            if (c0 == '%' && (peek(1) == 's' || peek(1) == 'x') &&
                (peek(2) == ' ' || peek(2) == '\t')) {
                bool excl = peek(1) == 'x';
                get(); get();
                std::string rest;
                while (!at_end() && peek() != '\n') rest.push_back(get());
                if (peek() == '\n') get();
                add_start_conds(rest, excl);
                continue;
            }

            // /* ... */ comment (possibly multi-line).
            if (c0 == '/' && peek(1) == '*') {
                get(); get();
                while (!at_end()) {
                    if (peek() == '*' && peek(1) == '/') {
                        get(); get();
                        break;
                    }
                    get();
                }
                continue;
            }

            // Line starting with whitespace: verbatim C in section 1.
            if (c0 == ' ' || c0 == '\t') {
                out.section1_verbatim += read_line_keep_nl();
                continue;
            }

            // Otherwise: NAME pattern   (definition).
            if (is_ident_start(static_cast<unsigned char>(c0))) {
                std::string name;
                while (!at_end() && is_ident_cont(static_cast<unsigned char>(peek())))
                    name.push_back(get());
                while (!at_end() && (peek() == ' ' || peek() == '\t')) get();
                std::string pat;
                while (!at_end() && peek() != '\n') pat.push_back(get());
                if (peek() == '\n') get();
                rstrip(pat);
                if (!pat.empty())
                    out.defs.emplace_back(std::move(name), std::move(pat));
                continue;
            }

            // Unknown line: skip with a warning.
            diag.warn(loc(), "ignoring unrecognised section-1 line");
            skip_blank_line();
        }
    }

    void parse_options(std::string_view text) {
        std::size_t i = 0;
        while (i < text.size()) {
            while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
            if (i >= text.size()) break;
            std::size_t start = i;
            // Tokens are space-separated except for "name=value" with
            // possibly quoted value.
            while (i < text.size() && text[i] != ' ' && text[i] != '\t') {
                if (text[i] == '"') {
                    ++i;
                    while (i < text.size() && text[i] != '"') {
                        if (text[i] == '\\' && i + 1 < text.size()) ++i;
                        ++i;
                    }
                    if (i < text.size()) ++i;
                } else {
                    ++i;
                }
            }
            std::string tok(text.substr(start, i - start));
            apply_option(tok);
        }
    }

    static bool is_in(std::string_view k, std::initializer_list<std::string_view> set) {
        for (auto s : set) if (k == s) return true;
        return false;
    }

    void apply_option(std::string tok) {
        auto split = tok.find('=');
        std::string key = (split == std::string::npos) ? tok : tok.substr(0, split);
        std::string val = (split == std::string::npos) ? std::string{}
                                                       : tok.substr(split + 1);
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
            val = val.substr(1, val.size() - 2);

        if      (key == "noyywrap")              out.options.noyywrap = true;
        else if (key == "yywrap")                out.options.noyywrap = false;
        else if (key == "yylineno")              out.options.yylineno = true;
        else if (key == "noyylineno")            out.options.yylineno = false;
        else if (is_in(key, {"case-insensitive", "caseless", "i"}))
            out.options.case_insensitive = true;
        else if (is_in(key, {"case-sensitive", "nocaseless"}))
            out.options.case_insensitive = false;
        else if (is_in(key, {"nodefault", "s"}))  out.options.nodefault = true;
        else if (key == "default")                out.options.nodefault = false;
        else if (is_in(key, {"debug", "d"}))      out.options.debug = true;
        else if (key == "nodebug")                out.options.debug = false;
        else if (key == "prefix")                 out.options.prefix = val;
        else if (key == "reentrant")              out.options.reentrant = true;
        else if (key == "noreentrant")            out.options.reentrant = false;
        else if (key == "bison-bridge")           out.options.bison_bridge = true;
        else if (key == "bison-locations")        out.options.bison_locations = true;
        else if (key == "extra-type")             out.options.extra_type = val;
        else if (key == "array")                  out.options.array = true;
        else if (key == "pointer")                out.options.array = false;
        else if (is_in(key, {"c++", "lex-compat", "posix-compat", "header"})) {
            diag.error(loc(), "unsupported %option: " + key);
        }
        else if (is_known_ignored_option(key)) {
            // accepted silently
        }
        else {
            diag.warn(loc(), "ignoring unknown %option: " + key);
        }
    }

    static bool is_known_ignored_option(std::string_view k) {
        static const std::string_view kIgnored[] = {
            "8bit", "no8bit", "interactive", "nointeractive",
            "batch", "nobatch", "warn", "nowarn", "ecs", "noecs",
            "meta-ecs", "nometa-ecs", "stack", "nostack",
            "always-interactive", "fast", "full", "main", "nomain",
            "yymore", "noyymore", "input", "noinput", "unput", "nounput",
            "yyalloc", "noyyalloc", "yyfree", "noyyfree",
            "yyrealloc", "noyyrealloc", "yy_scan_buffer", "yy_scan_bytes",
            "yy_scan_string", "outfile", "header-file", "yyclass",
            "tables-file", "tables-verify", "verbose", "noverbose",
            "perf-report", "noperf-report", "backup", "nobackup",
            "align", "noalign", "read", "noread",
        };
        for (auto s : kIgnored) if (k == s) return true;
        return false;
    }

    void add_start_conds(std::string_view text, bool exclusive) {
        std::size_t i = 0;
        while (i < text.size()) {
            while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
            if (i >= text.size()) break;
            std::size_t s = i;
            while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_'))
                ++i;
            if (i == s) break;
            out.conds.push_back({std::string(text.substr(s, i - s)), exclusive});
        }
    }

    // ---------------------------------------------------- section 2
    // Skip ASCII whitespace at start of line, but stop at newline (which
    // means "empty line"). Returns true if any was skipped.
    bool skip_inline_ws() {
        bool any = false;
        while (!at_end() && (peek() == ' ' || peek() == '\t')) { get(); any = true; }
        return any;
    }

    void parse_section2() {
        while (!at_end()) {
            char c = peek();
            if (c == '\n') { get(); continue; }
            if (c == '%' && peek(1) == '%') {
                get(); get();
                while (!at_end() && peek() != '\n') get();
                if (peek() == '\n') get();
                return;
            }
            // %{ ... %} verbatim. Per flex semantics this lands at the
            // top of the yylex() function body (so users can declare
            // local variables); separate field from section 1's
            // verbatim which is at file scope.
            if (c == '%' && peek(1) == '{') {
                if (out.section2_prologue.empty()) out.section2_loc = loc();
                get(); get();
                while (!at_end() && peek() != '\n') get();
                if (peek() == '\n') get();
                while (!at_end()) {
                    if (peek() == '%' && peek(1) == '}') {
                        get(); get();
                        while (!at_end() && peek() != '\n') get();
                        if (peek() == '\n') get();
                        break;
                    }
                    out.section2_prologue += read_line_keep_nl();
                }
                continue;
            }
            // /* ... */ comment line at column 0.
            if (c == '/' && peek(1) == '*') {
                get(); get();
                while (!at_end()) {
                    if (peek() == '*' && peek(1) == '/') {
                        get(); get();
                        break;
                    }
                    get();
                }
                continue;
            }
            // Indented C: a line starting with whitespace inside the
            // rules section is verbatim code (typically goes into yylex
            // body in flex). For our minimal scope, we drop it with a
            // warning unless it's empty.
            if (c == ' ' || c == '\t') {
                std::string l = read_line_keep_nl();
                // ignore
                continue;
            }
            parse_rule();
        }
    }

    void parse_rule() {
        Rule r;
        r.loc = loc();

        // Optional <SC,SC,...> prefix or <*>.
        if (peek() == '<' && peek(1) != '<') {
            get(); // '<'
            if (peek() == '*' && peek(1) == '>') {
                r.any_state = true;
                get(); get();
            } else {
                std::string cur;
                while (!at_end() && peek() != '>' && peek() != '\n') {
                    char ch = get();
                    if (ch == ',') {
                        if (!cur.empty()) r.conds.push_back(std::move(cur));
                        cur.clear();
                    } else if (!std::isspace(static_cast<unsigned char>(ch))) {
                        cur.push_back(ch);
                    }
                }
                if (peek() == '>') get();
                if (!cur.empty()) r.conds.push_back(std::move(cur));
            }
        }

        // <<EOF>> after the optional SC prefix is a special pattern.
        if (peek() == '<' && peek(1) == '<' &&
            src.compare(pos, 7, "<<EOF>>") == 0) {
            for (int k = 0; k < 7; ++k) get();
            r.eof = true;
        } else {
            r.pattern = read_pattern();
            if (r.pattern.empty()) {
                diag.error(r.loc, "empty pattern");
                // Skip rest of line to avoid loops.
                while (!at_end() && peek() != '\n') get();
                if (peek() == '\n') get();
                return;
            }
        }

        // Skip whitespace separating pattern and action.
        while (!at_end() && (peek() == ' ' || peek() == '\t')) get();

        r.action = read_action();
        out.rules.push_back(std::move(r));
    }

    std::string read_pattern() {
        std::string p;
        int paren = 0;
        bool in_class = false;
        bool in_quoted = false;
        while (!at_end()) {
            char c = peek();
            if (!in_class && !in_quoted && paren == 0 &&
                (c == ' ' || c == '\t' || c == '\n')) {
                break;
            }
            if (c == '\\' && pos + 1 < src.size()) {
                p.push_back(get());
                p.push_back(get());
                continue;
            }
            if (in_quoted) {
                if (c == '"') in_quoted = false;
                p.push_back(get());
                continue;
            }
            if (in_class) {
                if (c == ']') in_class = false;
                p.push_back(get());
                continue;
            }
            if (c == '"') { in_quoted = true; p.push_back(get()); continue; }
            if (c == '[') { in_class = true;  p.push_back(get()); continue; }
            if (c == '(') { ++paren;          p.push_back(get()); continue; }
            if (c == ')') { if (paren) --paren; p.push_back(get()); continue; }
            p.push_back(get());
        }
        return p;
    }

    std::string read_action() {
        // If line is empty (just newline), the action is empty.
        if (peek() == '\n') { get(); return {}; }

        // Action of "|" alone on a line means: share next rule's action.
        if (peek() == '|') {
            std::size_t save = pos;
            get();
            while (!at_end() && (peek() == ' ' || peek() == '\t')) get();
            if (peek() == '\n' || at_end()) {
                if (peek() == '\n') get();
                return "|";
            }
            pos = save;
        }

        // Brace block?
        if (peek() == '{') {
            return read_brace_block();
        }
        // Otherwise: rest of the line, as-is.
        std::string a;
        while (!at_end() && peek() != '\n') a.push_back(get());
        if (peek() == '\n') get();
        return a;
    }

    std::string read_brace_block() {
        std::string a;
        a.push_back(get()); // initial '{'
        int depth = 1;
        bool in_str = false, in_chr = false;
        bool in_line_cmt = false, in_block_cmt = false;
        while (!at_end() && depth > 0) {
            char c = peek();
            if (in_line_cmt) {
                a.push_back(get());
                if (c == '\n') in_line_cmt = false;
                continue;
            }
            if (in_block_cmt) {
                if (c == '*' && peek(1) == '/') {
                    a.push_back(get()); a.push_back(get());
                    in_block_cmt = false;
                } else {
                    a.push_back(get());
                }
                continue;
            }
            if (in_str) {
                if (c == '\\' && pos + 1 < src.size()) {
                    a.push_back(get()); a.push_back(get());
                    continue;
                }
                if (c == '"') in_str = false;
                a.push_back(get());
                continue;
            }
            if (in_chr) {
                if (c == '\\' && pos + 1 < src.size()) {
                    a.push_back(get()); a.push_back(get());
                    continue;
                }
                if (c == '\'') in_chr = false;
                a.push_back(get());
                continue;
            }
            if (c == '/' && peek(1) == '/') {
                a.push_back(get()); a.push_back(get());
                in_line_cmt = true;
                continue;
            }
            if (c == '/' && peek(1) == '*') {
                a.push_back(get()); a.push_back(get());
                in_block_cmt = true;
                continue;
            }
            if (c == '"')  { in_str = true; a.push_back(get()); continue; }
            if (c == '\'') { in_chr = true; a.push_back(get()); continue; }
            if (c == '{')  { ++depth; a.push_back(get()); continue; }
            if (c == '}')  { --depth; a.push_back(get()); continue; }
            a.push_back(get());
        }
        // Consume trailing newline if any.
        if (peek() == '\n') get();
        return a;
    }

    // ---------------------------------------------------- section 3
    void parse_section3() {
        out.section3_loc = loc();
        while (!at_end()) out.section3.push_back(get());
    }
};

} // namespace

std::optional<LexFile> parse_lex_file(std::string_view path,
                                      std::string_view contents,
                                      Diagnostics& diag) {
    Parser p(contents, std::string(path), diag);
    p.parse();
    if (!diag.ok()) return std::nullopt;
    return std::move(p.out);
}

} // namespace lexcpp
