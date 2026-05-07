// Fuzz the entire .l pipeline through codegen.

#include "diag.hpp"
#include "source.hpp"
#include "regex.hpp"
#include "nfa.hpp"
#include "dfa.hpp"
#include "codegen.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size > 8192) return 0;
    std::string_view src(reinterpret_cast<const char*>(data), size);
    lexcpp::Diagnostics diag;
    auto file = lexcpp::parse_lex_file("<fuzz>", src, diag);
    if (!file || !diag.ok()) return 0;

    std::vector<std::string> cond_names{"INITIAL"};
    std::vector<std::uint8_t> cond_excl{0};
    std::unordered_map<std::string, std::int32_t> cond_id{{"INITIAL", 0}};
    for (const auto& sc : file->conds) {
        cond_id[sc.name] = static_cast<std::int32_t>(cond_names.size());
        cond_names.push_back(sc.name);
        cond_excl.push_back(sc.exclusive ? 1 : 0);
    }

    lexcpp::NFA nfa;
    lexcpp::init_nfa(nfa, cond_names, cond_excl);

    std::unordered_map<std::string, std::string> defs;
    for (const auto& [k, v] : file->defs) defs[k] = v;
    auto resolver = [&](std::string_view name) -> std::optional<std::string> {
        auto it = defs.find(std::string(name));
        if (it == defs.end()) return std::nullopt;
        return "(" + it->second + ")";
    };

    std::int32_t nfa_rule_id = 0;
    for (std::size_t i = 0; i < file->rules.size(); ++i) {
        auto& r = file->rules[i];
        lexcpp::RuleSites sites;
        sites.any_state = r.any_state;
        for (const auto& cn : r.conds) {
            auto it = cond_id.find(cn);
            if (it == cond_id.end()) return 0;
            sites.conds.push_back(it->second);
        }
        if (r.eof) {
            lexcpp::add_eof_rule(nfa, static_cast<std::int32_t>(i), sites);
            continue;
        }
        auto tree = lexcpp::parse_regex(r.pattern, resolver,
                                        file->options.case_insensitive, diag, r.loc);
        if (!tree || !diag.ok()) return 0;
        lexcpp::add_rule_to_nfa(nfa, tree.get(), nfa_rule_id++, sites);
    }
    if (!diag.ok()) return 0;

    auto dfa = lexcpp::build_dfa(nfa);
    auto code = lexcpp::emit_c({&*file, &nfa, &dfa});
    (void)code;
    return 0;
}
