#include "eclass.hpp"

#include <vector>

namespace lexcpp {

Eclasses identity_eclasses() {
    Eclasses ec;
    for (int i = 0; i < 256; ++i) ec.ec[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
    ec.nclasses = 256;
    return ec;
}

namespace {
void refine(std::array<std::uint8_t, 256>& cls, int& nclasses, const ByteSet& B) {
    // For each existing class, compute (in_B, out_B). If both halves
    // are non-empty, allocate a new class id and reassign in_B bytes.
    std::vector<std::int32_t> remap(static_cast<std::size_t>(nclasses), -1);
    for (int c = 0; c < nclasses; ++c) {
        bool in_any = false, out_any = false;
        for (int b = 0; b < 256; ++b) {
            if (cls[static_cast<std::size_t>(b)] != static_cast<std::uint8_t>(c)) continue;
            if (B.test(static_cast<unsigned>(b))) in_any = true;
            else                                  out_any = true;
            if (in_any && out_any) break;
        }
        if (in_any && out_any) {
            remap[static_cast<std::size_t>(c)] = nclasses++;
            if (nclasses > 256) return;       // saturated
        }
    }
    for (int b = 0; b < 256; ++b) {
        std::int32_t c = cls[static_cast<std::size_t>(b)];
        std::int32_t r = remap[static_cast<std::size_t>(c)];
        if (r >= 0 && B.test(static_cast<unsigned>(b)))
            cls[static_cast<std::size_t>(b)] = static_cast<std::uint8_t>(r);
    }
}
} // namespace

Eclasses compute_eclasses(const NFA& nfa) {
    Eclasses ec;
    for (int i = 0; i < 256; ++i) ec.ec[static_cast<std::size_t>(i)] = 0;
    ec.nclasses = 1;
    for (const auto& st : nfa.states) {
        for (const auto& e : st.out) {
            if (e.on.empty()) continue;
            refine(ec.ec, ec.nclasses, e.on);
            if (ec.nclasses >= 256) return ec;
        }
    }
    return ec;
}

} // namespace lexcpp
