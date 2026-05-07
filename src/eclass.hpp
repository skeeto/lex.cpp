#pragma once

#include "nfa.hpp"

#include <array>
#include <cstdint>

namespace lexcpp {

// Byte-equivalence-class table.
struct Eclasses {
    std::array<std::uint8_t, 256> ec{};   // byte -> class id
    int nclasses = 0;
};

// Identity mapping: class i = byte i, 256 classes. Use in -f mode.
[[nodiscard]] Eclasses identity_eclasses();

// Refine the alphabet so that any NFA edge's ByteSet is a union of
// complete classes. Two bytes share a class iff every NFA edge either
// covers both or neither.
[[nodiscard]] Eclasses compute_eclasses(const NFA& nfa);

} // namespace lexcpp
