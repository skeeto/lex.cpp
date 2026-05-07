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

void append_int_array(std::string& out, std::string_view name,
                      std::string_view ctype,
                      const std::vector<long long>& values) {
    out += "static const ";
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
    out += "static const ";
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

const char* kPrefixSyms[] = {
    "yytext", "yyleng", "yylineno", "yyin", "yyout",
    "yylex", "yywrap", "yyrestart",
    "yy_scan_string", "yy_scan_buffer", "yy_scan_bytes",
    "yy_create_buffer", "yy_delete_buffer", "yy_switch_to_buffer",
    "yy_flush_buffer",
};

void emit_prefix_defines(std::string& out, const std::string& pfx) {
    if (pfx == "yy") return;
    for (const char* sym : kPrefixSyms) {
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
            out += " return 0;\n";
        } else {
            const auto& r = f.rules[static_cast<std::size_t>(per_cond[c])];
            out += " {\n";
            out += r.action;
            out += "\n    } break;\n";
        }
    }
    out += "    default: return 0;\n";
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

std::string yy_lex_body(const LexFile& f, const DFA& dfa, const NFA& nfa,
                        const RuleMap& rm, bool line_directives) {
    (void)dfa;
    std::ostringstream s;
    s << yylex_signature(f) << " {\n";
    if (f.options.bison_bridge)
        s << "    YY_G->yylval_r = yylval_param;\n";
    if (f.options.bison_locations)
        s << "    YY_G->yylloc_r = yylloc_param;\n";
    s << "    if (!yyin) yyin = stdin;\n";
    s << "    if (!yyout) yyout = stdout;\n";
    s << "    int yy_first = !yy_init_done;\n";
    s << "    yy_init_default_buffer(YY_CALLPM);\n";
    s << "    if (yy_first) {\n";
    s << "        #ifdef YY_USER_INIT\n";
    s << "        YY_USER_INIT\n";
    s << "        #endif\n";
    s << "    }\n";
    s << "    for (;;) {\n";
    s << "        yy_text_unseal(YY_CALLPM);\n";
    s << "        if (yy_buf_pos >= yy_buf_end) {\n";
    s << "            int yy_rule = -1;\n";
    s << "            yytext = yy_buf + yy_buf_pos;\n";
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
    s << "            return 0;\n";
    s << "        }\n";

    s << "        int yy_state = yy_at_bol\n";
    s << "            ? yy_cond_bol[yy_start]\n";
    s << "            : yy_cond_normal[yy_start];\n";
    s << "        size_t yy_base = yy_buf_pos;\n";
    s << "        size_t yy_scan = yy_base;\n";
    s << "        int    yy_acc_n = -1;\n";
    s << "        size_t yy_acc_n_len = 0;\n";
    s << "        int    yy_acc_e = -1;\n";
    s << "        size_t yy_acc_e_len = 0;\n";
    s << "        if (yy_accept_normal[yy_state] >= 0) {\n";
    s << "            yy_acc_n = yy_accept_normal[yy_state]; yy_acc_n_len = 0;\n";
    s << "        }\n";
    s << "        if (yy_accept_eol[yy_state] >= 0) {\n";
    s << "            yy_acc_e = yy_accept_eol[yy_state]; yy_acc_e_len = 0;\n";
    s << "        }\n";
    s << "        while (yy_scan < yy_buf_end) {\n";
    s << "            unsigned char yy_c = (unsigned char)yy_buf[yy_scan];\n";
    s << "            int yy_next = yy_nxt[yy_state][yy_c];\n";
    s << "            if (yy_next < 0) break;\n";
    s << "            yy_state = yy_next;\n";
    s << "            yy_scan++;\n";
    s << "            int yy_an = yy_accept_normal[yy_state];\n";
    s << "            int yy_ae = yy_accept_eol[yy_state];\n";
    s << "            if (yy_an >= 0) { yy_acc_n = yy_an; yy_acc_n_len = yy_scan - yy_base; }\n";
    s << "            if (yy_ae >= 0) { yy_acc_e = yy_ae; yy_acc_e_len = yy_scan - yy_base; }\n";
    s << "        }\n";

    s << "        int    yy_rule = -1;\n";
    s << "        size_t yy_len = 0;\n";
    s << "        if (yy_acc_e >= 0 && yy_acc_e_len > 0) {\n";
    s << "            size_t yy_after = yy_base + yy_acc_e_len;\n";
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
        s << "            fprintf(stderr, \"scanner jammed\\n\"); exit(2);\n";
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

    s << "        yytext = yy_buf + yy_base;\n";
    s << "        {\n";
    s << "            int yy_trail = yy_rule_trail_len[yy_rule];\n";
    s << "            if (yy_trail > 0 && (size_t)yy_trail <= yy_len) {\n";
    s << "                yy_len -= (size_t)yy_trail;\n";
    s << "            }\n";
    s << "        }\n";
    s << "        yyleng = (int)yy_len;\n";
    s << "        yy_text_save = yytext[yy_len];\n";
    s << "        yytext[yy_len] = 0;\n";
    s << "        yy_text_active = 1;\n";
    s << "        yy_buf_pos = yy_base + yy_len;\n";
    if (f.options.yylineno) {
        s << "        for (size_t yy_i = 0; yy_i < yy_len; ++yy_i)\n";
        s << "            if (yytext[yy_i] == '\\n') yylineno++;\n";
    }
    s << "        yy_at_bol = (yy_len > 0 && yytext[yy_len - 1] == '\\n');\n";
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
    s << "\n    }\n";
    s << "}\n";
    return s.str();
}

} // namespace

std::string emit_c(const CodegenInput& in) {
    const auto& f = *in.file;
    const auto& d = *in.dfa;
    const auto& nfa = *in.nfa;
    RuleMap rm = make_rule_map(f);

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
    out += "#include <string.h>\n\n";

    // Reentrant toggle (must precede the typedefs below).
    out += "#define YY_REENTRANT ";
    out += (f.options.reentrant ? "1" : "0");
    out += "\n";

    // Forward typedefs so user code in %{ ... %} can name yyscan_t.
    out += "typedef struct yy_buffer_state *YY_BUFFER_STATE;\n";
    if (f.options.reentrant)
        out += "typedef struct yyguts_t *yyscan_t;\n";
    out += "\n";

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
    emit_prefix_defines(out, f.options.prefix);

    // Condition constants
    emit_cond_defines(out, f);

    // Track-yylineno toggle for the runtime helpers.
    out += "#define YY_TRACK_LINENO ";
    out += (f.options.yylineno ? "1" : "0");
    out += "\n";
    // Extra-type for yyextra (default void*).
    out += "#ifndef YY_EXTRA_TYPE\n";
    out += "#define YY_EXTRA_TYPE ";
    out += (f.options.extra_type.empty() ? "void *" : f.options.extra_type);
    out += "\n#endif\n\n";

    // Tables.
    {
        std::vector<long long> nxt;
        nxt.reserve(d.states.size() * 256);
        for (const auto& st : d.states) {
            for (unsigned i = 0; i < 256; ++i) nxt.push_back(st.next[i]);
        }
        append_2d_array(out, "yy_nxt", "int", d.states.size(), 256, nxt);

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
    }
    out += "\n";

    // Embed runtime helpers.
    out += "/* runtime helpers (from runtime/runtime.c.in) */\n";
    out += std::string(kRuntimeTemplate);
    if (out.back() != '\n') out += "\n";
    out += "\n";

    // Bison-bridge: yylval is the *pointer* (matches flex). Users write
    // yylval->field. Outside yylex, callers use yyget_lval(scanner).
    if (f.options.bison_bridge)
        out += "#define yylval ((YYSTYPE*)YY_G->yylval_r)\n";
    if (f.options.bison_locations)
        out += "#define yylloc ((YYLTYPE*)YY_G->yylloc_r)\n";

    // yylex with action dispatch.
    out += yy_lex_body(f, d, nfa, rm, in.emit_line_directives);
    out += "\n";

    // yywrap default if requested.
    if (f.options.noyywrap) {
        if (f.options.reentrant)
            out += "int yywrap(yyscan_t s) { (void)s; return 1; }\n\n";
        else
            out += "int yywrap(void) { return 1; }\n\n";
    }

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
        h += "typedef struct yyguts_t *yyscan_t;\n";
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
