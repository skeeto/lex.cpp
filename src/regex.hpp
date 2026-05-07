#pragma once

#include "diag.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lexcpp {

// A bitset over the 256 byte values used in character classes.
struct ByteSet {
    std::array<std::uint64_t, 4> w{};

    constexpr void add(unsigned b) noexcept       { w[(b >> 6) & 3] |=  (1ULL << (b & 63)); }
    constexpr bool test(unsigned b) const noexcept{ return (w[(b >> 6) & 3] >> (b & 63)) & 1ULL; }
    constexpr void invert() noexcept              { for (auto& x : w) x = ~x; }
    constexpr void clear()  noexcept              { w = {}; }
    constexpr bool empty() const noexcept         { for (auto x : w) if (x) return false; return true; }
};

enum class NodeKind : std::uint8_t {
    Empty,      // matches the empty string
    Class,      // ByteSet
    Concat,     // a then b
    Alt,        // a or b
    Star, Plus, Question,
    Repeat,     // {n,m}; if m == kInf, unbounded
    AnchorBOL,  // ^
    AnchorEOL,  // $
    TrailBoundary, // r/s separator: zero-width marker for variable-length trail
};

struct Node {
    NodeKind kind = NodeKind::Empty;
    ByteSet  cls{};
    std::unique_ptr<Node> a, b;
    std::uint32_t lo = 0, hi = 0;
    static constexpr std::uint32_t kInf = 0xffffffffu;
};

using NodePtr = std::unique_ptr<Node>;

// Caller must supply a macro-lookup callback (`{NAME}` expansion).
using MacroResolver =
    std::function<std::optional<std::string>(std::string_view name)>;

// Pattern-level result: AST plus pre-computed flags for the rule.
struct ParsedPattern {
    NodePtr tree;
    int     trail_len = 0;          // 0 == no trailing context
    bool    eol_anchored = false;
};

[[nodiscard]] NodePtr parse_regex(std::string_view src,
                                  const MacroResolver& macros,
                                  bool case_insensitive,
                                  Diagnostics& diag,
                                  const SourceLoc& loc);

// Pattern-level entry point that handles `r/s` trailing context.
// `s` must be fixed-length in our minimal scope; otherwise diagnoses.
[[nodiscard]] ParsedPattern parse_pattern(std::string_view src,
                                          const MacroResolver& macros,
                                          bool case_insensitive,
                                          Diagnostics& diag,
                                          const SourceLoc& loc);

// Returns the byte length of a regex tree if it's fixed; -1 otherwise.
[[nodiscard]] int fixed_length(const Node* n);

} // namespace lexcpp
