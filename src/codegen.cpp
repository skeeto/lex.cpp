#include "codegen.hpp"

#include "runtime_template.inc"

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace lexcpp {
namespace {

// ---------- #line tracking -------------------------------------------------

std::size_t count_lines(std::string_view s) {
    std::size_t n = 0;
    for (char c : s) if (c == '\n') ++n;
    return n;
}

// Append a #line directive that points the next emitted line at the
// given source file/line. Caller decides when this is appropriate.
void emit_line_directive(std::string& out, std::string_view file,
                         std::uint32_t line, bool enabled) {
    if (!enabled) return;
    out += "#line ";
    out += std::to_string(line);
    out += " \"";
    for (char c : file) {
        if (c == '\\' || c == '"') out.push_back('\\');
        out.push_back(c);
    }
    out += "\"\n";
}

// Restore line-tracking back to the generated file at the next
// upcoming output line.
void restore_line_directive(std::string& out, std::string_view gen_file,
                            bool enabled) {
    if (!enabled) return;
    std::size_t next_line = count_lines(out) + 2;  // +1 for this dir, +1 for next
    out += "#line ";
    out += std::to_string(next_line);
    out += " \"";
    for (char c : gen_file) {
        if (c == '\\' || c == '"') out.push_back('\\');
        out.push_back(c);
    }
    out += "\"\n";
}

// ---------- helpers --------------------------------------------------------

// Whether the next emit_c invocation should drop `const` from table
// declarations so yytables_fload can overwrite them. Threaded as a
// file-static so the helpers don't all need a new parameter.
namespace { bool g_emit_loadable = false; }

void append_int_array(std::string& out, std::string_view name,
                      std::string_view ctype,
                      const std::vector<long long>& values) {
    out += g_emit_loadable ? "static " : "static const ";
    out += ctype;
    out += " ";
    out += name;
    out += "[";
    out += std::to_string(values.size() ? values.size() : 1);
    out += "] = {";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i % 16 == 0) out += "\n    ";
        out += std::to_string(values[i]);
        out += ",";
    }
    if (values.empty()) out += "0";
    out += "\n};\n";
}

void append_2d_array(std::string& out, std::string_view name,
                     std::string_view ctype,
                     std::size_t rows, std::size_t cols,
                     const std::vector<long long>& values) {
    out += g_emit_loadable ? "static " : "static const ";
    out += ctype;
    out += " ";
    out += name;
    out += "[";
    out += std::to_string(rows ? rows : 1);
    out += "][";
    out += std::to_string(cols ? cols : 1);
    out += "] = {";
    for (std::size_t r = 0; r < rows; ++r) {
        out += "\n    {";
        for (std::size_t c = 0; c < cols; ++c) {
            out += std::to_string(values[r * cols + c]);
            out += ",";
        }
        out += "},";
    }
    out += "\n};\n";
}

// Public symbols flex renames via `%option prefix=...`. Keep this list
// in sync with the runtime's externally visible names; users that rely
// on flex-style prefixing (PostgreSQL, Bison, Wireshark, ...) need every
// one of these to be #define-renamed.
const char* kPrefixSyms[] = {
    // globals (non-reentrant)
    "yytext", "yyleng", "yylineno", "yyin", "yyout",
    // entry points / lifecycle
    "yylex", "yywrap", "yyrestart",
    "yylex_init", "yylex_init_extra", "yylex_destroy",
    // buffer management
    "yy_scan_string", "yy_scan_buffer", "yy_scan_bytes",
    "yy_create_buffer", "yy_delete_buffer", "yy_init_buffer",
    "yy_flush_buffer", "yy_load_buffer_state",
    "yy_switch_to_buffer",
    "yypush_buffer_state", "yypop_buffer_state",
    "yyensure_buffer_stack",
    // accessors (reentrant)
    "yyget_debug", "yyset_debug",
    "yyget_extra", "yyset_extra",
    "yyget_in",    "yyset_in",
    "yyget_out",   "yyset_out",
    "yyget_leng",
    "yyget_text",
    "yyget_lineno", "yyset_lineno",
    "yyget_column", "yyset_column",
    "yyget_lval",   "yyset_lval",
    "yyget_lloc",   "yyset_lloc",
    // start-condition stack
    "yy_push_state", "yy_pop_state", "yy_top_state",
    // user-overridable allocator hooks
    "yyalloc", "yyrealloc", "yyfree",
    // debug toggle
    "yy_flex_debug",
};

// Symbols that are real globals only in non-reentrant mode. In
// reentrant mode they are macros into the yyguts_t struct, so prefix
// renaming would create a `#define yytext abc_text` in the user
// section followed by a `#define yytext (yyg->yytext_r)` in the
// runtime, redefining yytext and breaking the rename. Skip them.
[[nodiscard]] bool is_reentrant_only_global(std::string_view sym) {
    return sym == "yytext" || sym == "yyleng" || sym == "yylineno" ||
           sym == "yyin"   || sym == "yyout";
}

void emit_prefix_defines(std::string& out, const LexFile& f) {
    const std::string& pfx = f.options.prefix;
    if (pfx == "yy") return;
    for (const char* sym : kPrefixSyms) {
        if (f.options.reentrant && is_reentrant_only_global(sym)) continue;
        std::string repl = pfx + (sym + 2);  // skip leading "yy"
        out += "#define ";
        out += sym;
        out += " ";
        out += repl;
        out += "\n";
    }
    out += "\n";
}

void emit_cond_defines(std::string& out, const LexFile& f) {
    out += "#define INITIAL 0\n";
    for (std::size_t i = 0; i < f.conds.size(); ++i) {
        out += "#define ";
        out += f.conds[i].name;
        out += " ";
        out += std::to_string(i + 1);
        out += "\n";
    }
    out += "\n";
}

// Build mapping from rule indices in LexFile to `accept rule` ids in NFA.
// For now we use the same indexing -- rule i in LexFile is rule i in NFA,
// limited to non-EOF rules.
struct RuleMap {
    // For each LexFile rule i (non-EOF), nfa_id[i] is its NFA id
    // (-1 for EOF rules).
    std::vector<std::int32_t> nfa_id;
    std::int32_t total_nfa_rules = 0;
};

RuleMap make_rule_map(const LexFile& f) {
    RuleMap m;
    m.nfa_id.reserve(f.rules.size());
    for (const auto& r : f.rules) {
        if (r.eof) m.nfa_id.push_back(-1);
        else       m.nfa_id.push_back(m.total_nfa_rules++);
    }
    return m;
}

// Resolve "share-with-next" `|` actions. Returns, for each LexFile rule,
// the case-label group it belongs to (LexFile rule index whose action is
// concrete and shared with the contiguous run).
std::vector<std::size_t> resolve_pipe_targets(const LexFile& f) {
    std::vector<std::size_t> tgt(f.rules.size(), 0);
    for (std::size_t i = 0; i < f.rules.size(); ++i) {
        std::size_t j = i;
        while (j < f.rules.size() && f.rules[j].action == "|") ++j;
        if (j >= f.rules.size()) j = i;  // last rule -- shouldn't happen
        tgt[i] = j;
    }
    return tgt;
}

void emit_action_dispatch(std::string& out, const LexFile& f,
                          const RuleMap& rm, bool line_directives) {
    auto tgt = resolve_pipe_targets(f);
    out += "switch (yy_rule) {\n";
    for (std::size_t i = 0; i < f.rules.size(); ++i) {
        if (f.rules[i].eof) continue;
        out += "    case ";
        out += std::to_string(rm.nfa_id[i]);
        out += ":";
        if (f.rules[i].action == "|") {
            out += " /* fall through */\n";
            continue;
        }
        out += " {\n";
        emit_line_directive(out, f.rules[i].loc.file, f.rules[i].loc.line,
                            line_directives);
        out += f.rules[i].action;
        out += "\n    } break;\n";
    }
    out += "    default: break;\n";
    out += "}\n";
}

void emit_eof_dispatch(std::string& out, const LexFile& f, const NFA& nfa) {
    // For each condition, find the rule id (LexFile index) of the EOF rule,
    // if any. Per condition, only one EOF rule applies; use the first
    // declared in source.
    std::vector<std::int32_t> per_cond(nfa.cond_starts.size(), -1);
    for (std::size_t c = 0; c < nfa.cond_starts.size(); ++c) {
        if (!nfa.eof_rules[c].empty())
            per_cond[c] = nfa.eof_rules[c].front();
    }

    out += "switch (yy_start) {\n";
    for (std::size_t c = 0; c < per_cond.size(); ++c) {
        out += "    case ";
        out += std::to_string(c);
        out += ":";
        if (per_cond[c] < 0) {
            out += " yyterminate();\n";
        } else {
            const auto& r = f.rules[static_cast<std::size_t>(per_cond[c])];
            out += " {\n";
            out += r.action;
            out += "\n    } break;\n";
        }
    }
    out += "    default: yyterminate();\n";
    out += "}\n";
}

std::string yylex_signature(const LexFile& f) {
    std::string s = "int yylex(";
    bool first = true;
    auto add = [&](std::string_view a) {
        if (!first) s += ", ";
        s += a;
        first = false;
    };
    if (f.options.bison_bridge)    add("YYSTYPE *yylval_param");
    if (f.options.bison_locations) add("YYLTYPE *yylloc_param");
    if (f.options.reentrant)       add("yyscan_t yyscanner");
    if (first) s += "void";
    s += ")";
    return s;
}

std::string yy_lex_body_reject(const LexFile& f, const NFA& nfa,
                               const RuleMap& rm, bool line_directives) {
    std::ostringstream s;
    int nrules = static_cast<int>(rm.total_nfa_rules);
    if (nrules <= 0) nrules = 1;
    s << "YY_DECL\n{\n";
    if (f.options.reentrant) {
        // flex convention: yytext/yyleng/etc. are macros into `yyg`.
        // Declaring it here makes them work in user actions without
        // each .l file having to do it itself.
        s << "    struct yyguts_t *yyg = (struct yyguts_t *)yyscanner;\n";
    }
    if (f.options.bison_bridge)
        s << "    yyg->yylval_r = yylval_param;\n";
    if (f.options.bison_locations)
        s << "    yyg->yylloc_r = yylloc_param;\n";
    s << "    if (!yyin) yyin = stdin;\n";
    s << "    if (!yyout) yyout = stdout;\n";
    s << "    int yy_first = !yy_init_done;\n";
    s << "    yy_init_default_buffer(YY_CALLPM);\n";
    s << "    if (yy_first) {\n";
    s << "        #ifdef YY_USER_INIT\n        YY_USER_INIT\n        #endif\n";
    s << "    }\n";
    if (!f.section2_prologue.empty()) {
        std::string ld;
        emit_line_directive(ld, f.section2_loc.file, f.section2_loc.line,
                            line_directives);
        s << ld;
        s << f.section2_prologue;
        if (f.section2_prologue.back() != '\n') s << "\n";
    }
    s << "    int yy_match_lens[" << nrules << "];\n";
    s << "    int yy_rule = -1;\n";
    s << "    size_t yy_len = 0;\n";
    s << "    size_t yy_mb = 0;\n";
    s << "    for (;;) {\n";
    s << "        yy_text_unseal(YY_CALLPM);\n";
    s << "        /* Buffer exhausted on a file-backed buffer? Auto-restart\n";
    s << "         * from yyin. flex does the same. yyin may have been\n";
    s << "         * reassigned by the user (typical: gengtype's parse_file).\n";
    s << "         * Comparing FILE* pointers is unreliable -- fclose+fopen\n";
    s << "         * often reuses the same slot -- so we retry the read\n";
    s << "         * unconditionally and let yyrestart -> yy_slurp report 0\n";
    s << "         * bytes when the underlying stream is truly at EOF. */\n";
    s << "        if (yy_buf_pos >= yy_buf_end &&\n";
    s << "            YY_CURRENT_BUFFER->yy_input_file && yyin) {\n";
    s << "            yyrestart(yyin YY_CALLPM_C);\n";
    s << "        }\n";
    s << "        if (yy_buf_pos >= yy_buf_end) {\n";
    if (f.options.array) {
        s << "            yytext[0] = 0;\n";
    } else {
        s << "            yytext = yy_buf + yy_buf_pos;\n";
    }
    s << "            yyleng = 0;\n";
    s << "            yy_text_save = 0;\n";
    s << "            yy_text_active = 1;\n";
    s << "            ";
    {
        std::string eof;
        emit_eof_dispatch(eof, f, nfa);
        s << eof;
    }
    s << "            (void)yy_rule; yyterminate();\n";
    s << "        }\n";
    s << "        for (int r = 0; r < " << nrules << "; ++r) yy_match_lens[r] = -1;\n";
    s << "        yy_mb = yy_buf_pos;\n";
    s << "        size_t yy_scan = yy_mb;\n";
    s << "        int yy_state = yy_at_bol ? yy_cond_bol[yy_start] : yy_cond_normal[yy_start];\n";
    // EOL-only rules ($-anchored) only fire when the byte AFTER the
    // match is `\n` (or we're at end-of-input). The accept_pool
    // contains both kinds; filter at collection time.
    s << "        {\n";
    s << "            int yy_off = yy_accept_off[yy_state];\n";
    s << "            int yy_n   = yy_accept_off[yy_state + 1] - yy_off;\n";
    s << "            int yy_at_eol = (yy_scan >= yy_buf_end || yy_buf[yy_scan] == '\\n');\n";
    s << "            for (int i = 0; i < yy_n; ++i) {\n";
    s << "                int r = yy_accept_pool[yy_off + i];\n";
    s << "                if (yy_rule_eol[r] && !yy_at_eol) continue;\n";
    s << "                if (yy_match_lens[r] < 0) yy_match_lens[r] = 0;\n";
    s << "            }\n";
    s << "        }\n";
    s << "        while (yy_scan < yy_buf_end) {\n";
    s << "            unsigned char yy_c = (unsigned char)yy_buf[yy_scan];\n";
    s << "            int yy_next = YY_TRANSITION(yy_state, yy_c);\n";
    s << "            if (yy_next < 0) break;\n";
    s << "            yy_state = yy_next;\n";
    s << "            yy_scan++;\n";
    s << "            int yy_off = yy_accept_off[yy_state];\n";
    s << "            int yy_n   = yy_accept_off[yy_state + 1] - yy_off;\n";
    s << "            int yy_at_eol = (yy_scan >= yy_buf_end || yy_buf[yy_scan] == '\\n');\n";
    s << "            for (int i = 0; i < yy_n; ++i) {\n";
    s << "                int r = yy_accept_pool[yy_off + i];\n";
    s << "                if (yy_rule_eol[r] && !yy_at_eol) continue;\n";
    s << "                int cur = (int)(yy_scan - yy_mb);\n";
    s << "                if (yy_match_lens[r] < cur) yy_match_lens[r] = cur;\n";
    s << "            }\n";
    s << "        }\n";
    s << "    yy_find_best:\n";
    s << "        yy_rule = -1; yy_len = 0;\n";
    s << "        for (int r = 0; r < " << nrules << "; ++r) {\n";
    s << "            if (yy_match_lens[r] >= 0) {\n";
    s << "                size_t L = (size_t)yy_match_lens[r];\n";
    s << "                if (yy_rule < 0 || L > yy_len) { yy_rule = r; yy_len = L; }\n";
    s << "            }\n";
    s << "        }\n";
    s << "        if (yy_rule < 0 || yy_len == 0) {\n";
    if (f.options.nodefault) {
        s << "            yy_jam_report(YY_CALLPM); yyterminate();\n";
    } else {
        s << "            if (yy_buf_pos < yy_buf_end) {\n";
        s << "                unsigned char yy_d = (unsigned char)yy_buf[yy_buf_pos++];\n";
        s << "                (void)fputc((int)yy_d, yyout);\n";
        if (f.options.yylineno) s << "                if (yy_d == '\\n') yylineno++;\n";
        s << "                yy_at_bol = (yy_d == '\\n');\n";
        s << "            }\n";
        s << "            continue;\n";
    }
    s << "        }\n";
    if (f.options.array) {
        s << "        {\n";
        s << "            size_t yy_cp = yy_len < YYLMAX - 1 ? yy_len : YYLMAX - 1;\n";
        s << "            memcpy(yytext, yy_buf + yy_mb, yy_cp);\n";
        s << "            yytext[yy_cp] = 0;\n";
        s << "        }\n";
    } else {
        s << "        yytext = yy_buf + yy_mb;\n";
    }
    s << "        {\n";
    s << "            int yy_trail = yy_rule_trail_len[yy_rule];\n";
    s << "            if (yy_trail > 0 && (size_t)yy_trail <= yy_len) yy_len -= (size_t)yy_trail;\n";
    s << "        }\n";
    s << "        yy_buf_pos = yy_mb + yy_len;\n";
    s << "        if (yy_more_offset > 0) {\n";
    if (f.options.array) {
        s << "            /* yymore is unsupported with %option array */\n";
    } else {
        s << "            yytext = yy_buf + yy_mb - (size_t)yy_more_offset;\n";
    }
    s << "            yy_len += (size_t)yy_more_offset;\n";
    s << "        }\n";
    s << "        yy_more_flag = 0;\n";
    s << "        yyleng = (int)yy_len;\n";
    s << "        yy_text_save = yytext[yy_len];\n";
    s << "        yytext[yy_len] = 0;\n";
    s << "        yy_text_active = 1;\n";
    if (f.options.yylineno) {
        s << "        for (size_t yy_i = 0; yy_i < yy_len; ++yy_i)\n";
        s << "            if (yytext[yy_i] == '\\n') yylineno++;\n";
    }
    s << "        yy_at_bol = (yy_len > 0 && yytext[yy_len - 1] == '\\n');\n";
    if (f.options.debug) {
        // flex only emits the debug trace block when `%option debug`
        // is set. Doing it unconditionally trips on user code that
        // overrides `fprintf` via macro (PostgreSQL's bootscanner does
        // exactly this) because the macro arity won't match.
        s << "        if (yy_flex_debug) fprintf(stderr,\n";
        s << "            \"--accepting rule at line %d (\\\"%s\\\")\\n\",\n";
        s << "            yy_rule_line[yy_rule], yytext);\n";
    }
    s << "        #ifdef YY_USER_ACTION\n";
    s << "        YY_USER_ACTION\n";
    s << "        #endif\n";
    s << "#define REJECT  do {                                                  \\\n";
    s << "    yy_match_lens[yy_rule] = -1;                                       \\\n";
    s << "    yytext[yyleng] = yy_text_save;                                     \\\n";
    s << "    yy_text_active = 0;                                                \\\n";
    s << "    yy_buf_pos = yy_mb;                                              \\\n";
    s << "    goto yy_find_best;                                                 \\\n";
    s << "} while (0)\n";
    s << "        switch (yy_rule) {\n";
    auto tgt = resolve_pipe_targets(f);
    (void)tgt;
    for (std::size_t i = 0; i < f.rules.size(); ++i) {
        if (f.rules[i].eof) continue;
        s << "            case " << rm.nfa_id[i] << ":";
        if (f.rules[i].action == "|") {
            s << " /* fall through */\n";
            continue;
        }
        s << " {\n";
        std::string ld;
        emit_line_directive(ld, f.rules[i].loc.file, f.rules[i].loc.line, line_directives);
        s << ld;
        s << f.rules[i].action;
        s << "\n            } break;\n";
    }
    s << "            default: break;\n";
    s << "        }\n";
    s << "#undef REJECT\n";
    s << "        yy_more_offset = yy_more_flag ? yyleng : 0;\n";
    s << "    }\n";
    s << "}\n";
    return s.str();
}

std::string yy_lex_body(const LexFile& f, const DFA& dfa, const NFA& nfa,
                        const RuleMap& rm, bool line_directives) {
    (void)dfa;
    std::ostringstream s;
    s << "YY_DECL\n{\n";
    if (f.options.reentrant) {
        s << "    struct yyguts_t *yyg = (struct yyguts_t *)yyscanner;\n";
    }
    if (f.options.bison_bridge)
        s << "    yyg->yylval_r = yylval_param;\n";
    if (f.options.bison_locations)
        s << "    yyg->yylloc_r = yylloc_param;\n";
    s << "    if (!yyin) yyin = stdin;\n";
    s << "    if (!yyout) yyout = stdout;\n";
    s << "    int yy_first = !yy_init_done;\n";
    s << "    yy_init_default_buffer(YY_CALLPM);\n";
    s << "    if (yy_first) {\n";
    s << "        #ifdef YY_USER_INIT\n";
    s << "        YY_USER_INIT\n";
    s << "        #endif\n";
    s << "    }\n";
    if (!f.section2_prologue.empty()) {
        std::string ld;
        emit_line_directive(ld, f.section2_loc.file, f.section2_loc.line,
                            line_directives);
        s << ld;
        s << f.section2_prologue;
        if (f.section2_prologue.back() != '\n') s << "\n";
    }
    s << "    for (;;) {\n";
    s << "        yy_text_unseal(YY_CALLPM);\n";
    s << "        /* Buffer exhausted on a file-backed buffer? Auto-restart\n";
    s << "         * from yyin. flex does the same. yyin may have been\n";
    s << "         * reassigned by the user (typical: gengtype's parse_file).\n";
    s << "         * Comparing FILE* pointers is unreliable -- fclose+fopen\n";
    s << "         * often reuses the same slot -- so we retry the read\n";
    s << "         * unconditionally and let yyrestart -> yy_slurp report 0\n";
    s << "         * bytes when the underlying stream is truly at EOF. */\n";
    s << "        if (yy_buf_pos >= yy_buf_end &&\n";
    s << "            YY_CURRENT_BUFFER->yy_input_file && yyin) {\n";
    s << "            yyrestart(yyin YY_CALLPM_C);\n";
    s << "        }\n";
    s << "        if (yy_buf_pos >= yy_buf_end) {\n";
    s << "            int yy_rule = -1;\n";
    if (f.options.array) {
        s << "            yytext[0] = 0;\n";
    } else {
        s << "            yytext = yy_buf + yy_buf_pos;\n";
    }
    s << "            yyleng = 0;\n";
    s << "            yy_text_save = 0;\n";
    s << "            yy_text_active = 1;\n";
    s << "            ";
    {
        std::string eof;
        emit_eof_dispatch(eof, f, nfa);
        s << eof;
    }
    s << "            (void)yy_rule;\n";
    s << "            yyterminate();\n";
    s << "        }\n";

    bool any_var_trail = false;
    for (auto t : nfa.rule_trail) if (t < 0) { any_var_trail = true; break; }
    int n_rules_v = static_cast<int>(rm.total_nfa_rules);
    if (n_rules_v <= 0) n_rules_v = 1;

    s << "        int yy_state = yy_at_bol\n";
    s << "            ? yy_cond_bol[yy_start]\n";
    s << "            : yy_cond_normal[yy_start];\n";
    s << "        size_t yy_mb = yy_buf_pos;\n";
    s << "        size_t yy_scan = yy_mb;\n";
    s << "        int    yy_acc_n = -1;\n";
    s << "        size_t yy_acc_n_len = 0;\n";
    s << "        int    yy_acc_e = -1;\n";
    s << "        size_t yy_acc_e_len = 0;\n";
    if (any_var_trail) {
        s << "        size_t yy_boundary_pos[" << n_rules_v << "];\n";
        s << "        for (int yy_bi = 0; yy_bi < " << n_rules_v
          << "; ++yy_bi) yy_boundary_pos[yy_bi] = (size_t)-1;\n";
        s << "        { int yy_off = yy_boundary_off[yy_state];\n";
        s << "          int yy_n   = yy_boundary_off[yy_state + 1] - yy_off;\n";
        s << "          for (int yy_bi = 0; yy_bi < yy_n; ++yy_bi)\n";
        s << "              yy_boundary_pos[yy_boundary_pool[yy_off + yy_bi]] = yy_mb;\n";
        s << "        }\n";
    }
    s << "        if (yy_accept_normal[yy_state] >= 0) {\n";
    s << "            yy_acc_n = yy_accept_normal[yy_state]; yy_acc_n_len = 0;\n";
    s << "        }\n";
    s << "        if (yy_accept_eol[yy_state] >= 0) {\n";
    s << "            yy_acc_e = yy_accept_eol[yy_state]; yy_acc_e_len = 0;\n";
    s << "        }\n";
    s << "        while (yy_scan < yy_buf_end) {\n";
    s << "            unsigned char yy_c = (unsigned char)yy_buf[yy_scan];\n";
    s << "            int yy_next = YY_TRANSITION(yy_state, yy_c);\n";
    s << "            if (yy_next < 0) break;\n";
    s << "            yy_state = yy_next;\n";
    s << "            yy_scan++;\n";
    if (any_var_trail) {
        s << "            { int yy_off = yy_boundary_off[yy_state];\n";
        s << "              int yy_n   = yy_boundary_off[yy_state + 1] - yy_off;\n";
        s << "              for (int yy_bi = 0; yy_bi < yy_n; ++yy_bi)\n";
        s << "                  yy_boundary_pos[yy_boundary_pool[yy_off + yy_bi]] = yy_scan;\n";
        s << "            }\n";
    }
    s << "            int yy_an = yy_accept_normal[yy_state];\n";
    s << "            int yy_ae = yy_accept_eol[yy_state];\n";
    s << "            if (yy_an >= 0) { yy_acc_n = yy_an; yy_acc_n_len = yy_scan - yy_mb; }\n";
    s << "            if (yy_ae >= 0) { yy_acc_e = yy_ae; yy_acc_e_len = yy_scan - yy_mb; }\n";
    s << "        }\n";

    s << "        int    yy_rule = -1;\n";
    s << "        size_t yy_len = 0;\n";
    s << "        if (yy_acc_e >= 0 && yy_acc_e_len > 0) {\n";
    s << "            size_t yy_after = yy_mb + yy_acc_e_len;\n";
    s << "            int    yy_eol_ok = (yy_after >= yy_buf_end) ||\n";
    s << "                               (yy_buf[yy_after] == '\\n');\n";
    s << "            if (yy_eol_ok) {\n";
    s << "                if (yy_acc_n_len > yy_acc_e_len) {\n";
    s << "                    yy_rule = yy_acc_n; yy_len = yy_acc_n_len;\n";
    s << "                } else if (yy_acc_n_len == yy_acc_e_len && yy_acc_n >= 0 && yy_acc_n < yy_acc_e) {\n";
    s << "                    yy_rule = yy_acc_n; yy_len = yy_acc_n_len;\n";
    s << "                } else {\n";
    s << "                    yy_rule = yy_acc_e; yy_len = yy_acc_e_len;\n";
    s << "                }\n";
    s << "            }\n";
    s << "        }\n";
    s << "        if (yy_rule < 0 && yy_acc_n >= 0 && yy_acc_n_len > 0) {\n";
    s << "            yy_rule = yy_acc_n; yy_len = yy_acc_n_len;\n";
    s << "        }\n";
    s << "        if (yy_rule < 0) {\n";
    if (f.options.nodefault) {
        s << "            yy_jam_report(YY_CALLPM); yyterminate();\n";
    } else {
        s << "            unsigned char yy_d = (unsigned char)yy_buf[yy_buf_pos++];\n";
        s << "            (void)fputc((int)yy_d, yyout);\n";
        if (f.options.yylineno) {
            s << "            if (yy_d == '\\n') yylineno++;\n";
        }
        s << "            yy_at_bol = (yy_d == '\\n');\n";
        s << "            continue;\n";
    }
    s << "        }\n";

    if (f.options.array) {
        s << "        {\n";
        s << "            size_t yy_cp = yy_len < YYLMAX - 1 ? yy_len : YYLMAX - 1;\n";
        s << "            memcpy(yytext, yy_buf + yy_mb, yy_cp);\n";
        s << "            yytext[yy_cp] = 0;\n";
        s << "        }\n";
    } else {
        s << "        yytext = yy_buf + yy_mb;\n";
    }
    s << "        {\n";
    s << "            int yy_trail = yy_rule_trail_len[yy_rule];\n";
    s << "            if (yy_trail > 0 && (size_t)yy_trail <= yy_len) {\n";
    s << "                yy_len -= (size_t)yy_trail;\n";
    s << "            }\n";
    if (any_var_trail) {
        s << "            else if (yy_trail < 0) {\n";
        s << "                size_t yy_bp = yy_boundary_pos[yy_rule];\n";
        s << "                if (yy_bp != (size_t)-1 && yy_bp >= yy_mb && yy_bp <= yy_mb + yy_len) {\n";
        s << "                    yy_len = yy_bp - yy_mb;\n";
        s << "                }\n";
        s << "            }\n";
    }
    s << "        }\n";
    s << "        yy_buf_pos = yy_mb + yy_len;\n";
    s << "        if (yy_more_offset > 0) {\n";
    if (f.options.array) {
        s << "            /* yymore is unsupported with %option array */\n";
    } else {
        s << "            yytext = yy_buf + yy_mb - (size_t)yy_more_offset;\n";
    }
    s << "            yy_len += (size_t)yy_more_offset;\n";
    s << "        }\n";
    s << "        yy_more_flag = 0;\n";
    s << "        yyleng = (int)yy_len;\n";
    s << "        yy_text_save = yytext[yy_len];\n";
    s << "        yytext[yy_len] = 0;\n";
    s << "        yy_text_active = 1;\n";
    if (f.options.yylineno) {
        s << "        for (size_t yy_i = 0; yy_i < yy_len; ++yy_i)\n";
        s << "            if (yytext[yy_i] == '\\n') yylineno++;\n";
    }
    s << "        yy_at_bol = (yy_len > 0 && yytext[yy_len - 1] == '\\n');\n";
    if (f.options.debug) {
        // flex only emits the debug trace block when `%option debug`
        // is set. Doing it unconditionally trips on user code that
        // overrides `fprintf` via macro (PostgreSQL's bootscanner does
        // exactly this) because the macro arity won't match.
        s << "        if (yy_flex_debug) fprintf(stderr,\n";
        s << "            \"--accepting rule at line %d (\\\"%s\\\")\\n\",\n";
        s << "            yy_rule_line[yy_rule], yytext);\n";
    }
    s << "        #ifdef YY_USER_ACTION\n";
    s << "        YY_USER_ACTION\n";
    s << "        #endif\n";

    {
        std::string disp;
        emit_action_dispatch(disp, f, rm, line_directives);
        // indent disp
        std::string indented;
        for (char c : disp) {
            indented.push_back(c);
            if (c == '\n') indented += "        ";
        }
        s << "        " << indented;
    }
    s << "\n        yy_more_offset = yy_more_flag ? yyleng : 0;\n";
    s << "    }\n";
    s << "}\n";
    return s.str();
}

} // namespace

std::string emit_c(const CodegenInput& in) {
    const auto& f = *in.file;
    const auto& d = *in.dfa;
    const auto& nfa = *in.nfa;
    RuleMap rm = make_rule_map(f);
    g_emit_loadable = in.emit_tables_loader;

    std::string out;
    out += "/* Generated by lex.cpp -- do not edit. */\n";

    // %top { ... } -- runs before any standard includes.
    if (!f.section_top.empty()) {
        emit_line_directive(out, f.section_top_loc.file, f.section_top_loc.line,
                            in.emit_line_directives);
        out += f.section_top;
        if (out.back() != '\n') out += "\n";
    }

    out += "#include <stdio.h>\n";
    out += "#include <stdlib.h>\n";
    out += "#include <string.h>\n";
    out += "#include <errno.h>\n";  /* matches flex; user actions may use errno */
    out += "\n";

    // Reentrant toggle (must precede the typedefs below).
    out += "#define YY_REENTRANT ";
    out += (f.options.reentrant ? "1" : "0");
    out += "\n";

    // Forward typedefs so user code in %{ ... %} can name yyscan_t.
    out += "typedef struct yy_buffer_state *YY_BUFFER_STATE;\n";
    if (f.options.reentrant)
        out += "typedef void *yyscan_t;\n";
    out += "\n";

    // Capture libc allocators as function pointers BEFORE the user's
    // section-1 verbatim runs. This insulates our runtime from
    // `#define malloc xmalloc`-style macros that some projects (gdb,
    // libiberty consumers) drop into their .l files: the bare names
    // here resolve to the C library; later code that refers to
    // yy_libc_malloc_p stays stable even after malloc has been
    // hijacked downstream.
    out += "#include <stdlib.h>\n";
    out += "static void *(*yy_libc_malloc_p)(size_t) = malloc;\n";
    out += "static void *(*yy_libc_realloc_p)(void *, size_t) = realloc;\n";
    out += "static void  (*yy_libc_free_p)(void *) = free;\n\n";

    // User %{ %} block(s)
    const std::string& gen_file = in.output_path.empty() ? std::string("lex.yy.c")
                                                         : in.output_path;
    if (!f.section1_verbatim.empty()) {
        out += "/* user prologue */\n";
        emit_line_directive(out, f.section1_loc.file, f.section1_loc.line,
                            in.emit_line_directives);
        out += f.section1_verbatim;
        if (out.back() != '\n') out += "\n";
        restore_line_directive(out, gen_file, in.emit_line_directives);
    }

    // Prefix renames
    emit_prefix_defines(out, f);

    // Condition constants
    emit_cond_defines(out, f);

    // Track-yylineno toggle for the runtime helpers.
    out += "#define YY_TRACK_LINENO ";
    out += (f.options.yylineno ? "1" : "0");
    out += "\n";
    // %option no{yyalloc,yyrealloc,yyfree}: suppress emission of the
    // default allocator hooks; the user provides their own (PostgreSQL
    // routes everything through palloc/repalloc/pfree this way).
    if (f.options.noyyalloc)   out += "#define YY_NO_YYALLOC 1\n";
    if (f.options.noyyrealloc) out += "#define YY_NO_YYREALLOC 1\n";
    if (f.options.noyyfree)    out += "#define YY_NO_YYFREE 1\n";
    // %option array selects the fixed-buffer yytext layout.
    out += "#define YY_ARRAY ";
    out += (f.options.array ? "1" : "0");
    out += "\n";
    // Extra-type for yyextra (default void*).
    out += "#ifndef YY_EXTRA_TYPE\n";
    out += "#define YY_EXTRA_TYPE ";
    out += (f.options.extra_type.empty() ? "void *" : f.options.extra_type);
    out += "\n#endif\n\n";

    // Tables.
    {
        bool emit_ec      = in.compress != CompressMode::Full;
        bool emit_meta    = in.compress == CompressMode::Compress;
        bool emit_compact = in.compress == CompressMode::Compress;

        if (emit_ec) {
            std::vector<long long> ec;
            ec.reserve(256);
            for (unsigned i = 0; i < 256; ++i) ec.push_back(d.eclasses.ec[i]);
            append_int_array(out, "yy_ec", "unsigned char", ec);
        } else {
            // Identity table for the runtime macro that always reads it.
            std::vector<long long> ec;
            ec.reserve(256);
            for (unsigned i = 0; i < 256; ++i) ec.push_back(i);
            append_int_array(out, "yy_ec", "unsigned char", ec);
        }

        std::size_t ncls = static_cast<std::size_t>(d.nclasses);

        if (emit_compact) {
            CompressedDFA c = compress_dfa(d);
            std::vector<long long> base, def, nxt, chk;
            for (auto v : c.yy_base) base.push_back(v);
            for (auto v : c.yy_def)  def.push_back(v);
            for (auto v : c.yy_nxt)  nxt.push_back(v);
            for (auto v : c.yy_chk)  chk.push_back(v);
            append_int_array(out, "yy_base", "int", base);
            append_int_array(out, "yy_def",  "int", def);
            append_int_array(out, "yy_nxt",  "int", nxt);
            append_int_array(out, "yy_chk",  "int", chk);
        } else {
            std::vector<long long> nxt;
            nxt.reserve(d.states.size() * ncls);
            for (const auto& st : d.states) {
                for (std::size_t i = 0; i < ncls; ++i) nxt.push_back(st.next[i]);
            }
            append_2d_array(out, "yy_nxt", "int", d.states.size(), ncls, nxt);
        }

        if (emit_meta) {
            std::vector<long long> mv;
            mv.reserve(d.meta.size());
            for (auto v : d.meta) mv.push_back(v);
            if (mv.empty()) {
                for (std::size_t i = 0; i < ncls; ++i) mv.push_back(static_cast<long long>(i));
            }
            append_int_array(out, "yy_meta", "unsigned char", mv);
        }

        std::vector<long long> an, ae;
        an.reserve(d.states.size());
        ae.reserve(d.states.size());
        for (const auto& st : d.states) {
            an.push_back(st.accept_normal);
            ae.push_back(st.accept_eol);
        }
        append_int_array(out, "yy_accept_normal", "int", an);
        append_int_array(out, "yy_accept_eol",    "int", ae);

        std::vector<long long> cs_n, cs_b;
        cs_n.reserve(d.cond_starts.size());
        cs_b.reserve(d.cond_starts.size());
        for (const auto& cs : d.cond_starts) {
            cs_n.push_back(cs.normal);
            cs_b.push_back(cs.bol);
        }
        append_int_array(out, "yy_cond_normal", "int", cs_n);
        append_int_array(out, "yy_cond_bol",    "int", cs_b);

        // Per-rule trailing-context length (0 = none).
        std::vector<long long> tr;
        tr.reserve(nfa.rule_trail.size());
        for (auto t : nfa.rule_trail) tr.push_back(t);
        if (tr.empty()) tr.push_back(0);
        append_int_array(out, "yy_rule_trail_len", "int", tr);

        // Per-rule EOL flag (1 = `$`-anchored, 0 = ordinary). Used by
        // the REJECT body to filter accept candidates: an EOL-only
        // rule must only fire when the next byte is `\n` or EOF.
        // The non-REJECT body uses the per-state yy_accept_eol table
        // and doesn't need this.
        if (f.options.uses_reject) {
            std::vector<long long> reol;
            reol.reserve(nfa.rule_eol.size());
            for (auto e : nfa.rule_eol) reol.push_back(e);
            if (reol.empty()) reol.push_back(0);
            append_int_array(out, "yy_rule_eol", "int", reol);
        }

        // Per-rule source line number, for the %option debug trace.
        std::vector<long long> rl;
        for (const auto& r : f.rules)
            if (!r.eof) rl.push_back(r.loc.line);
        if (rl.empty()) rl.push_back(0);
        append_int_array(out, "yy_rule_line", "int", rl);

        // Variable-length trailing context: per-state list of rule
        // ids whose boundary state is in this DFA-state's NFA-set.
        bool any_var_trail = false;
        for (auto t : nfa.rule_trail) if (t < 0) { any_var_trail = true; break; }
        if (any_var_trail) {
            std::vector<long long> off, pool;
            off.reserve(d.states.size() + 1);
            for (const auto& st : d.states) {
                off.push_back(static_cast<long long>(pool.size()));
                for (auto r : st.boundary_rules) pool.push_back(r);
            }
            off.push_back(static_cast<long long>(pool.size()));
            if (pool.empty()) pool.push_back(0);
            append_int_array(out, "yy_boundary_off",  "int", off);
            append_int_array(out, "yy_boundary_pool", "int", pool);
        }

        // REJECT support: full per-state accept lists.
        if (f.options.uses_reject) {
            std::vector<long long> off;
            std::vector<long long> pool;
            off.reserve(d.states.size() + 1);
            for (const auto& st : d.states) {
                off.push_back(static_cast<long long>(pool.size()));
                for (auto r : st.accept_list) pool.push_back(r);
            }
            off.push_back(static_cast<long long>(pool.size()));
            if (pool.empty()) pool.push_back(0);
            append_int_array(out, "yy_accept_off",  "int", off);
            append_int_array(out, "yy_accept_pool", "int", pool);
        }
    }
    out += "\n";

    // %option debug toggle. yy_flex_debug is exposed to user code so
    // they can flip it at runtime; the initial value follows the option.
    out += "int yy_flex_debug = ";
    out += (f.options.debug ? "1" : "0");
    out += ";\n\n";

    // %option noyywrap: provide a default yywrap so user code that
    // calls it links cleanly. Emit either a real function (the
    // historical lex.cpp behaviour) or, when the user already
    // function-like-macroed yywrap (Bison's scan-{code,gram,skel}.l
    // does `#define <prefix>wrap() 1` in section 1), set
    // YY_SKIP_YYWRAP and don't emit our function. The runtime sees
    // YY_SKIP_YYWRAP and also skips its `extern int yywrap(void);`
    // declaration -- that declaration would otherwise be expanded
    // through the user's function-like macro and trip "wrong number
    // of arguments". This block has to land before the runtime
    // template so YY_SKIP_YYWRAP is visible to it.
    if (f.options.noyywrap) {
        out += "#ifdef yywrap\n";
        out += "/* yywrap is already a macro (typically from a\n";
        out += " * `#define <prefix>wrap()` in section 1 after a\n";
        out += " * prefix= rename); skip our default function. */\n";
        out += "#define YY_SKIP_YYWRAP\n";
        out += "#else\n";
        if (f.options.reentrant)
            out += "int yywrap(yyscan_t s) { (void)s; return 1; }\n";
        else
            out += "int yywrap(void) { return 1; }\n";
        out += "#endif\n\n";
    }

    // Embed runtime helpers.
    out += "/* runtime helpers (from runtime/runtime.c.in) */\n";
    if (!in.runtime_override.empty()) {
        out.append(in.runtime_override);
    } else {
        out += std::string(kRuntimeTemplate);
    }
    if (out.back() != '\n') out += "\n";
    out += "\n";

    // Inner-loop transition macro. Compressed mode goes through
    // yy_base/yy_chk; dense modes index yy_nxt[s][ec] directly.
    if (in.compress == CompressMode::Compress) {
        out += "#define YY_TRANSITION(s, b) "
               "(yy_chk[yy_base[(s)] + yy_ec[(unsigned char)(b)]] == (s) ? "
               "yy_nxt[yy_base[(s)] + yy_ec[(unsigned char)(b)]] : -1)\n";
    } else {
        out += "#define YY_TRANSITION(s, b) "
               "(yy_nxt[(s)][yy_ec[(unsigned char)(b)]])\n";
    }
    out += "\n";

    // Bison-bridge: yylval is the *pointer* (matches flex). Users write
    // yylval->field. Outside yylex, callers use yyget_lval(scanner).
    if (f.options.bison_bridge)
        out += "#define yylval ((YYSTYPE*)YY_G->yylval_r)\n";
    if (f.options.bison_locations)
        out += "#define yylloc ((YYLTYPE*)YY_G->yylloc_r)\n";

    // YY_DECL: lets users override the yylex signature (commonly used
    // by flex+bison setups that thread extra state through). The
    // default below is what we'd emit otherwise; users `#define
    // YY_DECL <their signature>` in %top { } or %{ ... %} before the
    // generated runtime to override.
    out += "#ifndef YY_DECL\n#define YY_DECL ";
    out += yylex_signature(f);
    out += "\n#endif\n\n";

    // yylex with action dispatch.
    if (f.options.uses_reject) {
        out += yy_lex_body_reject(f, nfa, rm, in.emit_line_directives);
    } else {
        out += yy_lex_body(f, d, nfa, rm, in.emit_line_directives);
    }
    out += "\n";

    // Optional runtime loader for tables emitted via --tables-file.
    if (in.emit_tables_loader) {
        bool compressed = in.compress == CompressMode::Compress;
        std::size_t ncls = static_cast<std::size_t>(d.nclasses);
        std::size_t ns   = d.states.size();
        std::size_t ncs  = d.cond_starts.size();
        std::size_t nr   = nfa.rule_trail.size();
        if (nr == 0) nr = 1;
        std::size_t pool = compressed ? static_cast<std::size_t>(compress_dfa(d).pool_size) : 0;

        out += "static int yy_read_be32(FILE *f, int *out_v) {\n";
        out += "    unsigned char b[4];\n";
        out += "    if (fread(b, 1, 4, f) != 4) return -1;\n";
        out += "    *out_v = (int)((unsigned)b[0]<<24 | (unsigned)b[1]<<16 |\n";
        out += "                    (unsigned)b[2]<<8  | (unsigned)b[3]);\n";
        out += "    return 0;\n";
        out += "}\n\n";

        out += "static int yy_read_int_array(FILE *f, int *dst, size_t n) {\n";
        out += "    for (size_t i = 0; i < n; ++i)\n";
        out += "        if (yy_read_be32(f, dst + i) != 0) return -1;\n";
        out += "    return 0;\n";
        out += "}\n\n";

        out += "int yytables_fload(const char *path) {\n";
        out += "    FILE *f = fopen(path, \"rb\");\n";
        out += "    if (!f) return -1;\n";
        out += "    int magic, version, name_len, flags;\n";
        out += "    int got_nclasses, got_nstates, got_nrules, got_pool;\n";
        out += "    if (yy_read_be32(f, &magic) || magic != (int)0xF13C57B1) goto bad;\n";
        out += "    if (yy_read_be32(f, &version) || version != 1) goto bad;\n";
        out += "    if (yy_read_be32(f, &name_len)) goto bad;\n";
        out += "    if (fseek(f, name_len, SEEK_CUR) != 0) goto bad;\n";
        out += "    if (yy_read_be32(f, &flags)) goto bad;\n";
        out += "    if (yy_read_be32(f, &got_nclasses)) goto bad;\n";
        out += "    if (yy_read_be32(f, &got_nstates))  goto bad;\n";
        out += "    if (yy_read_be32(f, &got_nrules))   goto bad;\n";
        out += "    if (yy_read_be32(f, &got_pool))     goto bad;\n";
        out += "    if (got_nclasses != " + std::to_string(ncls) + ") goto bad;\n";
        out += "    if (got_nstates  != " + std::to_string(ns)   + ") goto bad;\n";
        out += "    if (fread(yy_ec, 1, 256, f) != 256) goto bad;\n";
        if (compressed)
            out += "    if (fread(yy_meta, 1, " + std::to_string(ncls) + ", f) != " + std::to_string(ncls) + ") goto bad;\n";
        else
            out += "    { unsigned char yy_skip[" + std::to_string(ncls) + "]; if (fread(yy_skip, 1, " + std::to_string(ncls) + ", f) != " + std::to_string(ncls) + ") goto bad; }\n";

        if (compressed) {
            out += "    if (got_pool != " + std::to_string(pool) + ") goto bad;\n";
            out += "    if (yy_read_int_array(f, yy_base, " + std::to_string(ns) + ")) goto bad;\n";
            out += "    if (yy_read_int_array(f, yy_def , " + std::to_string(ns) + ")) goto bad;\n";
            out += "    if (yy_read_int_array(f, yy_nxt , " + std::to_string(pool) + ")) goto bad;\n";
            out += "    if (yy_read_int_array(f, yy_chk , " + std::to_string(pool) + ")) goto bad;\n";
        } else {
            out += "    if (yy_read_int_array(f, (int*)yy_nxt, " + std::to_string(ns * ncls) + ")) goto bad;\n";
        }
        out += "    if (yy_read_int_array(f, yy_accept_normal, " + std::to_string(ns) + ")) goto bad;\n";
        out += "    if (yy_read_int_array(f, yy_accept_eol,    " + std::to_string(ns) + ")) goto bad;\n";
        out += "    int got_ncond;\n";
        out += "    if (yy_read_be32(f, &got_ncond) || got_ncond != " + std::to_string(ncs) + ") goto bad;\n";
        out += "    if (yy_read_int_array(f, yy_cond_normal, " + std::to_string(ncs) + ")) goto bad;\n";
        out += "    if (yy_read_int_array(f, yy_cond_bol,    " + std::to_string(ncs) + ")) goto bad;\n";
        out += "    if (yy_read_int_array(f, yy_rule_trail_len, " + std::to_string(nr) + ")) goto bad;\n";
        out += "    fclose(f);\n";
        out += "    return 0;\n";
        out += "bad:\n";
        out += "    fclose(f);\n";
        out += "    return -1;\n";
        out += "}\n\n";
    }

    // (yywrap default emission moved earlier so YY_SKIP_YYWRAP
    // reaches the runtime template before its `extern int yywrap`
    // declaration.)

    // User epilogue.
    if (!f.section3.empty()) {
        out += "/* user epilogue */\n";
        emit_line_directive(out, f.section3_loc.file, f.section3_loc.line,
                            in.emit_line_directives);
        out += f.section3;
        if (out.back() != '\n') out += "\n";
    }

    return out;
}

std::string emit_h(const CodegenInput& in) {
    const auto& f = *in.file;
    std::string h;
    std::string guard = "LEX_GENERATED_HEADER_H";
    h += "#ifndef "; h += guard; h += "\n";
    h += "#define "; h += guard; h += "\n\n";
    h += "#include <stdio.h>\n";
    h += "#include <stddef.h>\n\n";

    h += "#define YY_REENTRANT ";
    h += (f.options.reentrant ? "1" : "0");
    h += "\n";

    h += "typedef struct yy_buffer_state *YY_BUFFER_STATE;\n";
    if (f.options.reentrant)
        h += "typedef void *yyscan_t;\n";
    h += "\n";

    h += "/* start conditions */\n";
    h += "#define INITIAL 0\n";
    for (std::size_t i = 0; i < f.conds.size(); ++i) {
        h += "#define ";
        h += f.conds[i].name;
        h += " ";
        h += std::to_string(i + 1);
        h += "\n";
    }
    h += "\n";

    if (f.options.reentrant) {
        h += "/* reentrant API */\n";
        h += "int   yylex_init(yyscan_t *scanner);\n";
        h += "int   yylex_init_extra(void *user, yyscan_t *scanner);\n";
        h += "int   yylex_destroy(yyscan_t scanner);\n";
        if (f.options.bison_bridge && f.options.bison_locations)
            h += "int   yylex(void *yylval, void *yylloc, yyscan_t scanner);\n";
        else if (f.options.bison_bridge)
            h += "int   yylex(void *yylval, yyscan_t scanner);\n";
        else
            h += "int   yylex(yyscan_t scanner);\n";
        h += "char *yyget_text(yyscan_t);  void  yyset_text(char*, yyscan_t);\n";
        h += "int   yyget_leng(yyscan_t);\n";
        h += "int   yyget_lineno(yyscan_t); void  yyset_lineno(int, yyscan_t);\n";
        h += "FILE *yyget_in(yyscan_t);     void  yyset_in (FILE*, yyscan_t);\n";
        h += "FILE *yyget_out(yyscan_t);    void  yyset_out(FILE*, yyscan_t);\n";
        h += "void *yyget_extra(yyscan_t);  void  yyset_extra(void*, yyscan_t);\n";
        h += "YY_BUFFER_STATE yy_create_buffer(FILE*, int, yyscan_t);\n";
        h += "void            yy_delete_buffer(YY_BUFFER_STATE, yyscan_t);\n";
        h += "void            yy_switch_to_buffer(YY_BUFFER_STATE, yyscan_t);\n";
        h += "void            yypush_buffer_state(YY_BUFFER_STATE, yyscan_t);\n";
        h += "void            yypop_buffer_state(yyscan_t);\n";
        h += "YY_BUFFER_STATE yy_scan_string(const char*, yyscan_t);\n";
        h += "YY_BUFFER_STATE yy_scan_bytes (const char*, int, yyscan_t);\n";
        h += "YY_BUFFER_STATE yy_scan_buffer(char*, size_t, yyscan_t);\n";
        h += "void            yyrestart    (FILE*, yyscan_t);\n";
        h += "void            yy_push_state(int, yyscan_t);\n";
        h += "void            yy_pop_state (yyscan_t);\n";
        h += "int             yy_top_state (yyscan_t);\n";
    } else {
        h += "/* non-reentrant API */\n";
        h += "extern char *yytext;\n";
        h += "extern int   yyleng;\n";
        h += "extern int   yylineno;\n";
        h += "extern FILE *yyin;\n";
        h += "extern FILE *yyout;\n";
        h += "int  yylex   (void);\n";
        h += "int  yywrap  (void);\n";
        h += "void yyrestart(FILE*);\n";
        h += "YY_BUFFER_STATE yy_create_buffer(FILE*, int);\n";
        h += "void            yy_delete_buffer(YY_BUFFER_STATE);\n";
        h += "void            yy_switch_to_buffer(YY_BUFFER_STATE);\n";
        h += "void            yypush_buffer_state(YY_BUFFER_STATE);\n";
        h += "void            yypop_buffer_state(void);\n";
        h += "YY_BUFFER_STATE yy_scan_string(const char*);\n";
        h += "YY_BUFFER_STATE yy_scan_bytes (const char*, int);\n";
        h += "YY_BUFFER_STATE yy_scan_buffer(char*, size_t);\n";
        h += "void            yy_push_state(int);\n";
        h += "void            yy_pop_state (void);\n";
        h += "int             yy_top_state (void);\n";
    }

    h += "\n#endif\n";
    return h;
}

} // namespace lexcpp
