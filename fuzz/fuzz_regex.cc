// Fuzz the regex parser + NFA + DFA pipeline.

#include "diag.hpp"
#include "regex.hpp"
#include "nfa.hpp"
#include "dfa.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size > 4096) return 0;   // bound to avoid quadratic explosion
    std::string_view src(reinterpret_cast<const char*>(data), size);
    lexcpp::Diagnostics diag;
    auto resolver = [](std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    };
    auto tree = lexcpp::parse_regex(src, resolver, /*ci*/false, diag, {});
    if (!tree || !diag.ok()) return 0;
    lexcpp::NFA nfa;
    lexcpp::init_nfa(nfa, {"INITIAL"}, {0});
    lexcpp::add_rule_to_nfa(nfa, tree.get(), 0, {});
    auto dfa = lexcpp::build_dfa(nfa);
    (void)dfa;
    return 0;
}
