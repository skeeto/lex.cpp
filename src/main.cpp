#include "codegen.hpp"
#include "diag.hpp"
#include "dfa.hpp"
#include "nfa.hpp"
#include "regex.hpp"
#include "source.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

constexpr std::string_view kVersion = "lex.cpp 0.1.0";

void print_usage(std::FILE* out) {
    std::fprintf(out,
        "Usage: lex [OPTIONS] [FILE]\n"
        "Generate a C scanner from a lex/flex source file.\n"
        "\n"
        "  -o, --outfile=FILE        write scanner to FILE (default lex.yy.c)\n"
        "  -t, --stdout              write scanner to stdout\n"
        "  -i, --case-insensitive    pattern matching ignores ASCII case\n"
        "      --yylineno            track line number in yylineno\n"
        "  -P, --prefix=STRING       use STRING instead of \"yy\" as prefix\n"
        "  -s, --nodefault           suppress default rule\n"
        "  -h, --help                show this help and exit\n"
        "  -V, --version             show version and exit\n"
        "\n"
        "If FILE is omitted, lex reads stdin.\n");
}

struct Args {
    std::string output_path;
    std::string prefix;
    std::string input_path;
    bool to_stdout = false;
    bool case_insensitive = false;
    bool yylineno = false;
    bool nodefault = false;
    bool prefix_set = false;
    bool noline = false;
    std::string header_path;     // empty == no header file
};

[[nodiscard]] bool starts_with(std::string_view s, std::string_view p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

int parse_args(int argc, char** argv, Args& out, lexcpp::Diagnostics& diag) {
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "-h" || a == "--help") {
            print_usage(stdout);
            std::exit(0);
        } else if (a == "-V" || a == "--version") {
            std::printf("%.*s\n", static_cast<int>(kVersion.size()), kVersion.data());
            std::exit(0);
        } else if (a == "-t" || a == "--stdout") {
            out.to_stdout = true;
        } else if (a == "-i" || a == "--case-insensitive") {
            out.case_insensitive = true;
        } else if (a == "--yylineno") {
            out.yylineno = true;
        } else if (a == "-s" || a == "--nodefault") {
            out.nodefault = true;
        } else if (a == "-L" || a == "--noline") {
            out.noline = true;
        } else if (starts_with(a, "--header-file=")) {
            out.header_path = std::string(a.substr(14));
        } else if (a == "--header-file") {
            // optional argument; if next arg looks like a value, take it
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                ++i;
                out.header_path = argv[i];
            } else {
                out.header_path = "_default_";  // placeholder
            }
        } else if (a == "-o") {
            if (++i >= argc) { diag.error({}, "-o requires an argument"); return 2; }
            out.output_path = argv[i];
        } else if (starts_with(a, "--outfile=")) {
            out.output_path = std::string(a.substr(10));
        } else if (a == "-P") {
            if (++i >= argc) { diag.error({}, "-P requires an argument"); return 2; }
            out.prefix = argv[i];
            out.prefix_set = true;
        } else if (starts_with(a, "--prefix=")) {
            out.prefix = std::string(a.substr(9));
            out.prefix_set = true;
        } else if (a == "--") {
            if (++i < argc) out.input_path = argv[i];
        } else if (!a.empty() && a[0] == '-' && a != "-") {
            diag.error({}, "unknown option: " + std::string(a));
            return 2;
        } else {
            if (!out.input_path.empty()) {
                diag.error({}, "multiple input files are not supported");
                return 2;
            }
            out.input_path = std::string(a);
        }
    }
    if (out.output_path.empty() && !out.to_stdout)
        out.output_path = "lex.yy.c";
    return 0;
}

[[nodiscard]] std::string slurp(std::istream& in) {
    std::ostringstream ss;
    ss << in.rdbuf();
    return std::move(ss).str();
}

// Normalise text for the parser: strip a leading UTF-8 BOM and collapse
// CRLF -> LF so the line-oriented parser is OS-independent.
void normalise_source(std::string& s) {
    if (s.size() >= 3 &&
        static_cast<unsigned char>(s[0]) == 0xef &&
        static_cast<unsigned char>(s[1]) == 0xbb &&
        static_cast<unsigned char>(s[2]) == 0xbf) {
        s.erase(0, 3);
    }
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') continue;
        out.push_back(s[i]);
    }
    s.swap(out);
}

} // namespace

int main(int argc, char** argv) {
    lexcpp::Diagnostics diag;
    Args args;
    if (int rc = parse_args(argc, argv, args, diag); rc != 0) return rc;

    std::string source;
    std::string source_label;
    if (args.input_path.empty() || args.input_path == "-") {
        source_label = "<stdin>";
        source = slurp(std::cin);
    } else {
        source_label = args.input_path;
        std::ifstream f(args.input_path, std::ios::binary);
        if (!f) {
            diag.error({}, "cannot open " + args.input_path);
            return 1;
        }
        source = slurp(f);
    }
    normalise_source(source);

    auto file_opt = lexcpp::parse_lex_file(source_label, source, diag);
    if (!file_opt || !diag.ok()) return 1;
    auto& file = *file_opt;

    // CLI overrides %option.
    if (args.case_insensitive) file.options.case_insensitive = true;
    if (args.yylineno)         file.options.yylineno = true;
    if (args.nodefault)        file.options.nodefault = true;
    if (args.prefix_set)       file.options.prefix = args.prefix;

    // Scan all action bodies for the REJECT keyword (whole word).
    auto contains_word = [](const std::string& s, std::string_view word) {
        std::size_t pos = 0;
        while ((pos = s.find(word, pos)) != std::string::npos) {
            bool lb = pos == 0 ||
                !(std::isalnum(static_cast<unsigned char>(s[pos - 1])) ||
                  s[pos - 1] == '_');
            std::size_t end = pos + word.size();
            bool rb = end >= s.size() ||
                !(std::isalnum(static_cast<unsigned char>(s[end])) ||
                  s[end] == '_');
            if (lb && rb) return true;
            pos = end;
        }
        return false;
    };
    for (const auto& r : file.rules) {
        if (contains_word(r.action, "REJECT")) {
            file.options.uses_reject = true;
            break;
        }
    }

    // Build NFA.
    std::vector<std::string> cond_names{"INITIAL"};
    std::vector<std::uint8_t> cond_excl{0};
    std::unordered_map<std::string, std::int32_t> cond_id{{"INITIAL", 0}};
    for (const auto& sc : file.conds) {
        cond_id[sc.name] = static_cast<std::int32_t>(cond_names.size());
        cond_names.push_back(sc.name);
        cond_excl.push_back(sc.exclusive ? 1 : 0);
    }

    lexcpp::NFA nfa;
    lexcpp::init_nfa(nfa, cond_names, cond_excl);

    // Macro resolver: returns the raw definition string.
    std::unordered_map<std::string, std::string> defs;
    for (const auto& [k, v] : file.defs) defs[k] = v;
    auto resolver = [&](std::string_view name) -> std::optional<std::string> {
        auto it = defs.find(std::string(name));
        if (it == defs.end()) return std::nullopt;
        // Wrap in parens to preserve precedence.
        return "(" + it->second + ")";
    };

    std::int32_t nfa_rule_id = 0;
    for (std::size_t i = 0; i < file.rules.size(); ++i) {
        auto& r = file.rules[i];
        lexcpp::RuleSites sites;
        sites.any_state = r.any_state;
        for (const auto& cn : r.conds) {
            auto it = cond_id.find(cn);
            if (it == cond_id.end()) {
                diag.error(r.loc, "unknown start condition: " + cn);
                return 1;
            }
            sites.conds.push_back(it->second);
        }

        if (r.eof) {
            lexcpp::add_eof_rule(nfa, static_cast<std::int32_t>(i), sites);
            continue;
        }

        lexcpp::SourceLoc rl = r.loc;
        auto pp = lexcpp::parse_pattern(r.pattern, resolver,
                                        file.options.case_insensitive,
                                        diag, rl);
        if (!pp.tree) {
            diag.error(rl, "failed to parse pattern: " + r.pattern);
            return 1;
        }
        lexcpp::add_rule_to_nfa(nfa, pp.tree.get(), nfa_rule_id++,
                                sites, pp.trail_len);
    }
    if (!diag.ok()) return 1;

    // Build DFA. Equivalence classes enabled by default; -f / --full
    // (added in Phase 13) will let the user request the dense form.
    lexcpp::DFA dfa = lexcpp::build_dfa(nfa, /*use_eclasses=*/true);

    // Codegen.
    lexcpp::CodegenInput cg{&file, &nfa, &dfa};
    cg.output_path = args.to_stdout ? std::string("<stdout>") : args.output_path;
    cg.emit_line_directives = !args.noline;
    auto out = lexcpp::emit_c(cg);

    if (args.to_stdout) {
        std::fwrite(out.data(), 1, out.size(), stdout);
    } else {
        std::ofstream of(args.output_path, std::ios::binary);
        if (!of) {
            diag.error({}, "cannot open " + args.output_path + " for writing");
            return 1;
        }
        of.write(out.data(), static_cast<std::streamsize>(out.size()));
    }

    // Companion header.
    std::string hpath = args.header_path;
    if (!hpath.empty()) {
        if (hpath == "_default_") {
            hpath = args.output_path.empty() ? std::string("lex.yy.h")
                                             : args.output_path;
            auto dot = hpath.find_last_of('.');
            if (dot != std::string::npos) hpath.replace(dot, std::string::npos, ".h");
            else hpath += ".h";
        }
        auto h = lexcpp::emit_h(cg);
        std::ofstream of(hpath, std::ios::binary);
        if (!of) {
            diag.error({}, "cannot open " + hpath + " for writing");
            return 1;
        }
        of.write(h.data(), static_cast<std::streamsize>(h.size()));
    }
    return diag.ok() ? 0 : 1;
}
