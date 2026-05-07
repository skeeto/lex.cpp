// Internal unit tests. No external deps; tiny framework below.

#include "diag.hpp"
#include "source.hpp"
#include "regex.hpp"
#include "nfa.hpp"
#include "dfa.hpp"
#include "codegen.hpp"
#include "tables.hpp"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

namespace {

int g_pass = 0, g_fail = 0;

#define CHECK(cond) do {                                          \
    if (cond) { ++g_pass; }                                       \
    else      { ++g_fail; std::fprintf(stderr,                    \
                  "  FAIL: %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

void test_parse_simple() {
    std::fprintf(stderr, "test_parse_simple\n");
    lexcpp::Diagnostics d;
    auto r = lexcpp::parse_lex_file("<t>",
        "%option noyywrap\n%%\n[a-z]+ ECHO;\n%%\n", d);
    CHECK(r.has_value());
    CHECK(d.ok());
    CHECK(r->options.noyywrap);
    CHECK(r->rules.size() == 1);
    CHECK(r->rules[0].pattern == "[a-z]+");
}

void test_unknown_option() {
    std::fprintf(stderr, "test_unknown_option\n");
    lexcpp::Diagnostics d;
    auto r = lexcpp::parse_lex_file("<t>",
        "%option noyywrap nosuchflag\n%%\nfoo ECHO;\n%%\n", d);
    CHECK(r.has_value());           // still ok -- only a warning
    CHECK(d.ok());                   // warnings are not errors
}

void test_unsupported_option() {
    std::fprintf(stderr, "test_unsupported_option\n");
    lexcpp::Diagnostics d;
    auto r = lexcpp::parse_lex_file("<t>",
        "%option c++\n%%\nfoo ECHO;\n%%\n", d);
    CHECK(!d.ok());
    CHECK(d.error_count() >= 1);
    (void)r;
}

void test_reentrant_option() {
    std::fprintf(stderr, "test_reentrant_option\n");
    lexcpp::Diagnostics d;
    auto r = lexcpp::parse_lex_file("<t>",
        "%option reentrant noyywrap\n%%\nfoo ECHO;\n%%\n", d);
    CHECK(r.has_value());
    CHECK(d.ok());
    CHECK(r->options.reentrant);
}

void test_bison_bridge_option() {
    std::fprintf(stderr, "test_bison_bridge_option\n");
    lexcpp::Diagnostics d;
    auto r = lexcpp::parse_lex_file("<t>",
        "%option reentrant bison-bridge bison-locations noyywrap\n"
        "%%\nfoo ECHO;\n%%\n", d);
    CHECK(r.has_value());
    CHECK(r->options.bison_bridge);
    CHECK(r->options.bison_locations);
}

void test_known_ignored() {
    std::fprintf(stderr, "test_known_ignored\n");
    lexcpp::Diagnostics d;
    auto r = lexcpp::parse_lex_file("<t>",
        "%option noyywrap 8bit batch ecs\n%%\nfoo ECHO;\n%%\n", d);
    CHECK(r.has_value());
    CHECK(d.ok());
}

void test_section_dividers() {
    std::fprintf(stderr, "test_section_dividers\n");
    lexcpp::Diagnostics d;
    auto r = lexcpp::parse_lex_file("<t>",
        "%option noyywrap\n%%\n[a-z]+ ECHO;\n%%\nint main(void){return 0;}\n", d);
    CHECK(r.has_value());
    CHECK(r->section3.find("int main") != std::string::npos);
}

void test_macro_undefined() {
    std::fprintf(stderr, "test_macro_undefined\n");
    lexcpp::Diagnostics d;
    auto resolver = [](std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    };
    auto t = lexcpp::parse_regex("{NOSUCH}", resolver, false, d, {});
    CHECK(!d.ok());
    (void)t;
}

void test_macro_recursive() {
    std::fprintf(stderr, "test_macro_recursive\n");
    lexcpp::Diagnostics d;
    auto resolver = [](std::string_view name) -> std::optional<std::string> {
        if (name == "A") return std::string("({A})");
        return std::nullopt;
    };
    auto t = lexcpp::parse_regex("{A}", resolver, false, d, {});
    CHECK(!d.ok());
    (void)t;
}

void test_unmatched_paren() {
    std::fprintf(stderr, "test_unmatched_paren\n");
    lexcpp::Diagnostics d;
    auto resolver = [](std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    };
    auto t = lexcpp::parse_regex("(abc", resolver, false, d, {});
    CHECK(!d.ok());
    (void)t;
}

void test_unterminated_class() {
    std::fprintf(stderr, "test_unterminated_class\n");
    lexcpp::Diagnostics d;
    auto resolver = [](std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    };
    auto t = lexcpp::parse_regex("[abc", resolver, false, d, {});
    CHECK(!d.ok());
    (void)t;
}

void test_unterminated_quote() {
    std::fprintf(stderr, "test_unterminated_quote\n");
    lexcpp::Diagnostics d;
    auto resolver = [](std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    };
    auto t = lexcpp::parse_regex("\"abc", resolver, false, d, {});
    CHECK(!d.ok());
    (void)t;
}

void test_bare_brace() {
    std::fprintf(stderr, "test_bare_brace\n");
    lexcpp::Diagnostics d;
    auto resolver = [](std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    };
    // `{` not followed by digit or known macro -> literal brace.
    auto t = lexcpp::parse_regex("{x", resolver, false, d, {});
    CHECK(d.ok());
    CHECK(t != nullptr);
}

void test_case_insensitive_class() {
    std::fprintf(stderr, "test_case_insensitive_class\n");
    lexcpp::Diagnostics d;
    auto resolver = [](std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    };
    auto t = lexcpp::parse_regex("[a-z]", resolver, true, d, {});
    CHECK(d.ok());
    CHECK(t != nullptr);
    // Class should now contain both cases of every letter.
    CHECK(t->kind == lexcpp::NodeKind::Class);
    CHECK(t->cls.test('a'));
    CHECK(t->cls.test('A'));
    CHECK(t->cls.test('Z'));
}

void test_repeat_with_inf() {
    std::fprintf(stderr, "test_repeat_with_inf\n");
    lexcpp::Diagnostics d;
    auto resolver = [](std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    };
    auto t = lexcpp::parse_regex("a{2,}", resolver, false, d, {});
    CHECK(d.ok());
    CHECK(t != nullptr);
}

void test_compile_empty_node() {
    std::fprintf(stderr, "test_compile_empty_node\n");
    lexcpp::NFA nfa;
    lexcpp::init_nfa(nfa, {"INITIAL"}, {0});
    auto empty = std::make_unique<lexcpp::Node>();
    empty->kind = lexcpp::NodeKind::Empty;
    lexcpp::add_rule_to_nfa(nfa, empty.get(), 0, {});
    auto dfa = lexcpp::build_dfa(nfa);
    CHECK(!dfa.states.empty());
}

void test_codegen_smoke() {
    std::fprintf(stderr, "test_codegen_smoke\n");
    lexcpp::Diagnostics d;
    auto file = lexcpp::parse_lex_file("<t>",
        "%option noyywrap\n%%\n[a-z]+ ECHO;\n%%\n", d);
    CHECK(file.has_value());
    lexcpp::NFA nfa;
    lexcpp::init_nfa(nfa, {"INITIAL"}, {0});
    auto resolver = [](std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    };
    auto t = lexcpp::parse_regex(file->rules[0].pattern, resolver, false, d, {});
    CHECK(t != nullptr);
    lexcpp::add_rule_to_nfa(nfa, t.get(), 0, {});
    auto dfa = lexcpp::build_dfa(nfa);
    lexcpp::CodegenInput cg_in;
    cg_in.file = &*file; cg_in.nfa = &nfa; cg_in.dfa = &dfa;
    auto code = lexcpp::emit_c(cg_in);
    CHECK(code.find("int yylex") != std::string::npos);
    CHECK(code.find("yywrap") != std::string::npos);
}

void test_escape_b() {
    std::fprintf(stderr, "test_escape_b\n");
    lexcpp::Diagnostics d;
    auto resolver = [](std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    };
    auto t = lexcpp::parse_regex("\\b", resolver, false, d, {});
    CHECK(d.ok());
    CHECK(t != nullptr);
    CHECK(t->kind == lexcpp::NodeKind::Class);
    CHECK(t->cls.test('\b'));
}

void test_unexpected_after_alt() {
    std::fprintf(stderr, "test_unexpected_after_alt\n");
    lexcpp::Diagnostics d;
    auto resolver = [](std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    };
    // ')' at top level after parse_alt completes is unexpected.
    auto t = lexcpp::parse_regex("a)b", resolver, false, d, {});
    CHECK(!d.ok());
    (void)t;
}

void test_class_high_byte_neg() {
    std::fprintf(stderr, "test_class_high_byte_neg\n");
    lexcpp::Diagnostics d;
    auto resolver = [](std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    };
    auto t = lexcpp::parse_regex("[^\\x80-\\xff]", resolver, false, d, {});
    CHECK(d.ok());
    CHECK(t != nullptr);
    CHECK(t->kind == lexcpp::NodeKind::Class);
    CHECK(t->cls.test('a'));
    CHECK(!t->cls.test(0x80));
}

void test_octal_escape() {
    std::fprintf(stderr, "test_octal_escape\n");
    lexcpp::Diagnostics d;
    auto resolver = [](std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    };
    auto t = lexcpp::parse_regex("\\101", resolver, false, d, {});
    CHECK(d.ok());
    CHECK(t->cls.test('A'));
}

void test_section3_only() {
    std::fprintf(stderr, "test_section3_only\n");
    lexcpp::Diagnostics d;
    auto r = lexcpp::parse_lex_file("<t>",
        "%option noyywrap\n%%\n%%\nint dummy = 42;\n", d);
    CHECK(r.has_value());
    CHECK(r->rules.empty());
    CHECK(r->section3.find("dummy") != std::string::npos);
}

void test_pipe_action() {
    std::fprintf(stderr, "test_pipe_action\n");
    lexcpp::Diagnostics d;
    auto r = lexcpp::parse_lex_file("<t>",
        "%option noyywrap\n%%\n"
        "if  |\n"
        "for printf(\"KW\");\n"
        "%%\n", d);
    CHECK(r.has_value());
    CHECK(d.ok());
    CHECK(r->rules.size() == 2);
    CHECK(r->rules[0].action == "|");
}

void test_start_cond_decl() {
    std::fprintf(stderr, "test_start_cond_decl\n");
    lexcpp::Diagnostics d;
    auto r = lexcpp::parse_lex_file("<t>",
        "%option noyywrap\n"
        "%s INC1 INC2\n"
        "%x EXC\n"
        "%%\n"
        "<INC1>foo ECHO;\n"
        "<*>bar ECHO;\n"
        "%%\n", d);
    CHECK(r.has_value());
    CHECK(r->conds.size() == 3);
    CHECK(r->conds[0].name == "INC1");
    CHECK(r->conds[2].exclusive);
}

void test_header_emit() {
    std::fprintf(stderr, "test_header_emit\n");
    lexcpp::Diagnostics d;
    auto file = lexcpp::parse_lex_file("<t>",
        "%option noyywrap\n%s STR\n%%\nfoo ECHO;\n%%\n", d);
    CHECK(file.has_value());
    lexcpp::NFA nfa;
    lexcpp::init_nfa(nfa, {"INITIAL", "STR"}, {0, 0});
    auto resolver = [](std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    };
    auto t = lexcpp::parse_regex("foo", resolver, false, d, {});
    lexcpp::add_rule_to_nfa(nfa, t.get(), 0, {});
    auto dfa = lexcpp::build_dfa(nfa);
    lexcpp::CodegenInput in;
    in.file = &*file; in.nfa = &nfa; in.dfa = &dfa;
    auto h = lexcpp::emit_h(in);
    CHECK(h.find("YY_BUFFER_STATE") != std::string::npos);
    CHECK(h.find("#define INITIAL 0") != std::string::npos);
    CHECK(h.find("#define STR 1") != std::string::npos);
    CHECK(h.find("extern char *yytext") != std::string::npos);
    CHECK(h.find("yylex   (void)") != std::string::npos);
}

void test_header_reentrant() {
    std::fprintf(stderr, "test_header_reentrant\n");
    lexcpp::Diagnostics d;
    auto file = lexcpp::parse_lex_file("<t>",
        "%option reentrant noyywrap\n%%\nfoo ECHO;\n%%\n", d);
    CHECK(file.has_value());
    lexcpp::NFA nfa;
    lexcpp::init_nfa(nfa, {"INITIAL"}, {0});
    auto resolver = [](std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    };
    auto t = lexcpp::parse_regex("foo", resolver, false, d, {});
    lexcpp::add_rule_to_nfa(nfa, t.get(), 0, {});
    auto dfa = lexcpp::build_dfa(nfa);
    lexcpp::CodegenInput in;
    in.file = &*file; in.nfa = &nfa; in.dfa = &dfa;
    auto h = lexcpp::emit_h(in);
    CHECK(h.find("typedef struct yyguts_t *yyscan_t;") != std::string::npos);
    CHECK(h.find("yylex_init(yyscan_t") != std::string::npos);
    CHECK(h.find("yyget_text(yyscan_t)") != std::string::npos);
    CHECK(h.find("extern char *yytext") == std::string::npos);
}

void test_tables_serialise() {
    std::fprintf(stderr, "test_tables_serialise\n");
    lexcpp::Diagnostics d;
    auto file = lexcpp::parse_lex_file("<t>",
        "%option noyywrap\n%%\n[a-z]+ ECHO;\n%%\n", d);
    CHECK(file.has_value());
    lexcpp::NFA nfa;
    lexcpp::init_nfa(nfa, {"INITIAL"}, {0});
    auto resolver = [](std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    };
    auto t = lexcpp::parse_regex("[a-z]+", resolver, false, d, {});
    lexcpp::add_rule_to_nfa(nfa, t.get(), 0, {});
    auto dfa = lexcpp::build_dfa(nfa, /*ec=*/true, /*meta=*/true);
    auto bytes = lexcpp::serialise_tables(nfa, dfa, /*compressed=*/true, "yy");
    CHECK(bytes.size() > 16);
    // Magic in network order: 0xF1 3C 57 B1
    CHECK(bytes[0] == 0xF1 && bytes[1] == 0x3C
       && bytes[2] == 0x57 && bytes[3] == 0xB1);
    // Version = 1
    CHECK(bytes[4] == 0 && bytes[5] == 0 && bytes[6] == 0 && bytes[7] == 1);
}

void test_dangerous_trail_warning() {
    std::fprintf(stderr, "test_dangerous_trail_warning\n");
    lexcpp::Diagnostics d;
    auto resolver = [](std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    };
    // r=[a-z]+ (variable) and s=[0-9]+ (variable) -- ambiguous.
    auto pp = lexcpp::parse_pattern("[a-z]+/[0-9]+", resolver, false, d, {});
    CHECK(pp.tree != nullptr);
    CHECK(pp.trail_len == -1);
    CHECK(d.ok());                      // warnings, not errors
}

void test_safe_trail_no_warning() {
    std::fprintf(stderr, "test_safe_trail_no_warning\n");
    lexcpp::Diagnostics d;
    auto resolver = [](std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    };
    // Fixed r ("if") + variable s = unambiguous boundary; no warning.
    auto pp = lexcpp::parse_pattern("\"if\"/[ \\t]+", resolver, false, d, {});
    CHECK(pp.tree != nullptr);
    CHECK(pp.trail_len == -1);
    CHECK(d.ok());
}

void test_meta_classes() {
    std::fprintf(stderr, "test_meta_classes\n");
    // A grammar where two classes (lower-case letters and underscore)
    // would behave identically across the DFA after EC reduction:
    // pattern [a-z_]+ collapses them.
    lexcpp::Diagnostics d;
    auto file = lexcpp::parse_lex_file("<t>",
        "%option noyywrap\n%%\n[a-z_]+ ECHO;\n%%\n", d);
    CHECK(file.has_value());
    lexcpp::NFA nfa;
    lexcpp::init_nfa(nfa, {"INITIAL"}, {0});
    auto resolver = [](std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    };
    auto t = lexcpp::parse_regex("[a-z_]+", resolver, false, d, {});
    lexcpp::add_rule_to_nfa(nfa, t.get(), 0, {});
    auto dfa = lexcpp::build_dfa(nfa, /*ec=*/true, /*meta=*/true);
    CHECK(dfa.nmeta > 0);
    CHECK(dfa.nmeta <= dfa.nclasses);   // meta should never grow alphabet
}

void test_top_block() {
    std::fprintf(stderr, "test_top_block\n");
    lexcpp::Diagnostics d;
    auto r = lexcpp::parse_lex_file("<t>",
        "%top {\n#define _GNU_SOURCE 1\n}\n"
        "%option noyywrap\n%%\nfoo ECHO;\n%%\n", d);
    CHECK(r.has_value());
    CHECK(d.ok());
    CHECK(r->section_top.find("_GNU_SOURCE") != std::string::npos);
}

void test_line_directives() {
    std::fprintf(stderr, "test_line_directives\n");
    lexcpp::Diagnostics d;
    auto file = lexcpp::parse_lex_file("scanner.l",
        "%option noyywrap\n"
        "%{\nint x = 0;\n%}\n"
        "%%\n"
        "[a-z]+ ECHO;\n"
        "%%\n"
        "int main(void){ return 0; }\n", d);
    CHECK(file.has_value());
    lexcpp::NFA nfa;
    lexcpp::init_nfa(nfa, {"INITIAL"}, {0});
    auto resolver = [](std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    };
    auto t = lexcpp::parse_regex(file->rules[0].pattern, resolver, false, d, {});
    lexcpp::add_rule_to_nfa(nfa, t.get(), 0, {});
    auto dfa = lexcpp::build_dfa(nfa);
    {
        lexcpp::CodegenInput in;
        in.file = &*file; in.nfa = &nfa; in.dfa = &dfa;
        in.output_path = "out.c";
        in.emit_line_directives = true;
        auto code = lexcpp::emit_c(in);
        CHECK(code.find("#line ") != std::string::npos);
        CHECK(code.find("\"scanner.l\"") != std::string::npos);
    }
    {
        lexcpp::CodegenInput in;
        in.file = &*file; in.nfa = &nfa; in.dfa = &dfa;
        in.output_path = "out.c";
        in.emit_line_directives = false;
        auto code = lexcpp::emit_c(in);
        CHECK(code.find("#line ") == std::string::npos);
    }
}

void test_diag_warn() {
    std::fprintf(stderr, "test_diag_warn\n");
    lexcpp::Diagnostics d;
    d.warn({"<f>", 1, 2}, "watch out");
    CHECK(d.ok());                  // warnings don't break ok()
    CHECK(d.error_count() == 0);
    d.error({}, "bad");
    CHECK(!d.ok());
}

} // namespace

int main() {
    test_parse_simple();
    test_unknown_option();
    test_unsupported_option();
    test_known_ignored();
    test_section_dividers();
    test_macro_undefined();
    test_macro_recursive();
    test_unmatched_paren();
    test_unterminated_class();
    test_unterminated_quote();
    test_bare_brace();
    test_case_insensitive_class();
    test_repeat_with_inf();
    test_compile_empty_node();
    test_codegen_smoke();
    test_escape_b();
    test_unexpected_after_alt();
    test_class_high_byte_neg();
    test_octal_escape();
    test_section3_only();
    test_pipe_action();
    test_start_cond_decl();
    test_reentrant_option();
    test_bison_bridge_option();
    test_meta_classes();
    test_dangerous_trail_warning();
    test_safe_trail_no_warning();
    test_tables_serialise();
    test_top_block();
    test_header_emit();
    test_header_reentrant();
    test_line_directives();
    test_diag_warn();

    std::fprintf(stderr, "\nUnit tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
