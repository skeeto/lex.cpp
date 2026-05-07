#include "codegen.hpp"
#include "diag.hpp"
#include "dfa.hpp"
#include "nfa.hpp"
#include "platform.hpp"
#include "regex.hpp"
#include "source.hpp"
#include "tables.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

constexpr std::string_view kVersion = "lex.cpp 0.1.0\n";
constexpr std::string_view kUsage =
    "Usage: lex [OPTIONS] [FILE]\n"
    "Generate a C scanner from a lex/flex source file.\n"
    "\n"
    "  -o, --outfile=FILE        write scanner to FILE (default lex.yy.c)\n"
    "  -t, --stdout              write scanner to stdout\n"
    "  -i, --case-insensitive    pattern matching ignores ASCII case\n"
    "      --yylineno            track line number in yylineno\n"
    "  -P, --prefix=STRING       use STRING instead of \"yy\" as prefix\n"
    "  -s, --nodefault           suppress default rule\n"
    "  -L, --noline              omit #line directives\n"
    "  -f, --full                no compression (-Cf alias)\n"
    "      -Cfe                  ECs only (no comb)\n"
    "      -Cem, --compress      ECs + meta-ECs + comb (default)\n"
    "      --header-file[=PATH]  also write a companion .h\n"
    "      --tables-file=PATH    also write a binary table dump\n"
    "  -S, --skeleton=PATH       use PATH as runtime template instead of embedded\n"
    "  -h, --help                show this help and exit\n"
    "  -V, --version             show version and exit\n"
    "\n"
    "If FILE is omitted or '-', lex reads stdin.\n"
    "Multiple FILEs are concatenated in order before parsing.\n";

struct Args {
    std::string output_path;
    std::string prefix;
    std::vector<std::string> input_paths;
    bool to_stdout = false;
    bool case_insensitive = false;
    bool yylineno = false;
    bool nodefault = false;
    bool prefix_set = false;
    bool noline = false;
    std::string header_path;     // empty == no header file
    std::string tables_path;     // empty == no .tbl emission
    std::string skeleton_path;   // empty == use embedded runtime
    lexcpp::CompressMode compress = lexcpp::CompressMode::Compress;
};

[[nodiscard]] bool starts_with(std::string_view s, std::string_view p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

int parse_args(int argc, const std::string_view* argv, Args& out,
               lexcpp::Diagnostics& diag) {
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "-h" || a == "--help") {
            lexcpp::platform::stdout_stream().write_all(kUsage);
            std::exit(0);
        } else if (a == "-V" || a == "--version") {
            lexcpp::platform::stdout_stream().write_all(kVersion);
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
        } else if (a == "-f" || a == "--full" || a == "-Cf" || a == "--Cf") {
            out.compress = lexcpp::CompressMode::Full;
        } else if (a == "-Cfe" || a == "--Cfe") {
            out.compress = lexcpp::CompressMode::FullEc;
        } else if (a == "-Cem" || a == "--Cem" || a == "--compress") {
            out.compress = lexcpp::CompressMode::Compress;
        } else if (starts_with(a, "--tables-file=")) {
            out.tables_path = std::string(a.substr(14));
        } else if (a == "-S" || a == "--skeleton") {
            if (++i >= argc) { diag.error({}, "-S requires an argument"); return 2; }
            out.skeleton_path = std::string(argv[i]);
        } else if (starts_with(a, "-S")) {
            out.skeleton_path = std::string(a.substr(2));
        } else if (starts_with(a, "--skeleton=")) {
            out.skeleton_path = std::string(a.substr(11));
        } else if (starts_with(a, "--header-file=")) {
            out.header_path = std::string(a.substr(14));
        } else if (a == "--header-file") {
            if (i + 1 < argc && !argv[i + 1].empty() && argv[i + 1][0] != '-') {
                ++i;
                out.header_path = std::string(argv[i]);
            } else {
                out.header_path = "_default_";
            }
        } else if (a == "-o") {
            if (++i >= argc) { diag.error({}, "-o requires an argument"); return 2; }
            out.output_path = std::string(argv[i]);
        } else if (starts_with(a, "-o")) {
            out.output_path = std::string(a.substr(2));
        } else if (starts_with(a, "--outfile=")) {
            out.output_path = std::string(a.substr(10));
        } else if (a == "-P") {
            if (++i >= argc) { diag.error({}, "-P requires an argument"); return 2; }
            out.prefix = std::string(argv[i]);
            out.prefix_set = true;
        } else if (starts_with(a, "-P")) {
            out.prefix = std::string(a.substr(2));
            out.prefix_set = true;
        } else if (starts_with(a, "--prefix=")) {
            out.prefix = std::string(a.substr(9));
            out.prefix_set = true;
        } else if (a == "--") {
            while (++i < argc) out.input_paths.emplace_back(argv[i]);
        } else if (!a.empty() && a[0] == '-' && a != "-") {
            diag.error({}, "unknown option: " + std::string(a));
            return 2;
        } else {
            out.input_paths.emplace_back(a);
        }
    }
    if (out.output_path.empty() && !out.to_stdout)
        out.output_path = "lex.yy.c";
    return 0;
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

bool write_string_to_path(std::string_view path, std::string_view payload,
                          lexcpp::Diagnostics& diag) {
    auto f = lexcpp::platform::open_write(path);
    if (!f.ok()) {
        diag.error({}, "cannot open " + std::string(path) + " for writing");
        return false;
    }
    if (!f.write_all(payload)) {
        diag.error({}, "write failed: " + std::string(path));
        return false;
    }
    return true;
}

} // namespace

int core_main(int argc, const std::string_view* argv) {
    lexcpp::Diagnostics diag;
    Args args;
    if (int rc = parse_args(argc, argv, args, diag); rc != 0) return rc;

    std::string source;
    std::string source_label;
    if (args.input_paths.empty() ||
        (args.input_paths.size() == 1 && args.input_paths[0] == "-")) {
        source_label = "<stdin>";
        source = lexcpp::platform::slurp(lexcpp::platform::stdin_stream());
    } else {
        source_label = args.input_paths[0];
        for (std::size_t i = 0; i < args.input_paths.size(); ++i) {
            const auto& path = args.input_paths[i];
            std::string chunk;
            if (path == "-") {
                chunk = lexcpp::platform::slurp(lexcpp::platform::stdin_stream());
            } else {
                auto in = lexcpp::platform::open_read(path);
                if (!in.ok()) {
                    diag.error({}, "cannot open " + path);
                    return 1;
                }
                chunk = lexcpp::platform::slurp(in);
            }
            if (i > 0 && !source.empty() && source.back() != '\n')
                source.push_back('\n');
            source.append(chunk);
        }
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

    std::unordered_map<std::string, std::string> defs;
    for (const auto& [k, v] : file.defs) defs[k] = v;
    auto resolver = [&](std::string_view name) -> std::optional<std::string> {
        auto it = defs.find(std::string(name));
        if (it == defs.end()) return std::nullopt;
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

    bool use_ec   = args.compress != lexcpp::CompressMode::Full;
    bool use_meta = args.compress == lexcpp::CompressMode::Compress;
    lexcpp::DFA dfa = lexcpp::build_dfa(nfa, use_ec, use_meta);

    std::string skeleton_buf;
    if (!args.skeleton_path.empty()) {
        auto in = lexcpp::platform::open_read(args.skeleton_path);
        if (!in.ok()) {
            diag.error({}, "cannot open skeleton: " + args.skeleton_path);
            return 1;
        }
        skeleton_buf = lexcpp::platform::slurp(in);
    }

    lexcpp::CodegenInput cg;
    cg.file = &file;
    cg.nfa  = &nfa;
    cg.dfa  = &dfa;
    cg.output_path = args.to_stdout ? std::string("<stdout>") : args.output_path;
    cg.emit_line_directives = !args.noline;
    cg.compress = args.compress;
    cg.emit_tables_loader = !args.tables_path.empty();
    if (!args.skeleton_path.empty()) cg.runtime_override = skeleton_buf;
    auto out = lexcpp::emit_c(cg);

    if (args.to_stdout) {
        if (!lexcpp::platform::stdout_stream().write_all(out)) {
            diag.error({}, "write to stdout failed");
            return 1;
        }
    } else if (!write_string_to_path(args.output_path, out, diag)) {
        return 1;
    }

    if (!args.tables_path.empty()) {
        if (!lexcpp::write_tables_file(
                args.tables_path, nfa, dfa,
                args.compress == lexcpp::CompressMode::Compress,
                "yy")) {
            diag.error({}, "cannot write tables file: " + args.tables_path);
            return 1;
        }
    }

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
        if (!write_string_to_path(hpath, h, diag)) return 1;
    }
    return diag.ok() ? 0 : 1;
}
