#pragma once

#include "diag.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lexcpp {

// A start-condition declaration (%s NAME or %x NAME).
struct StartCond {
    std::string name;
    bool exclusive = false;
};

// One rule: a set of start conditions (empty == INITIAL inclusive),
// a regex pattern (raw, pre-macro-expansion), and an action body.
struct Rule {
    SourceLoc loc;
    std::vector<std::string> conds;   // names; "*" means all
    bool any_state = false;           // true if <*>
    std::string pattern;              // raw text up to whitespace before action
    std::string action;                // verbatim C code
    bool eof = false;                 // <<EOF>>
};

// Result of parsing a .l file.
struct LexFile {
    SourceLoc origin;
    std::vector<std::pair<std::string, std::string>> defs; // name -> pattern
    std::vector<StartCond> conds;
    std::vector<Rule> rules;
    std::string section1_verbatim;     // %{ ... %} blocks from section 1
    std::string section3;              // user-code section verbatim

    struct Options {
        bool noyywrap = false;
        bool yylineno = false;
        bool case_insensitive = false;
        bool nodefault = false;
        bool debug = false;
        std::string prefix = "yy";
    } options;
};

[[nodiscard]] std::optional<LexFile> parse_lex_file(
    std::string_view path,
    std::string_view contents,
    Diagnostics& diag);

} // namespace lexcpp
