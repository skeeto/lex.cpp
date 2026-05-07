#include "tables.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>

namespace lexcpp {
namespace {

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xff));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
    out.push_back(static_cast<std::uint8_t>((v >>  8) & 0xff));
    out.push_back(static_cast<std::uint8_t>( v        & 0xff));
}
void put_u8 (std::vector<std::uint8_t>& out, std::uint8_t  v) { out.push_back(v); }
void put_i32(std::vector<std::uint8_t>& out, std::int32_t  v) { put_u32(out, static_cast<std::uint32_t>(v)); }

} // namespace

std::vector<std::uint8_t> serialise_tables(const NFA& nfa, const DFA& dfa,
                                           bool compressed,
                                           const std::string& scanner_name) {
    std::vector<std::uint8_t> out;
    put_u32(out, 0xF13C57B1u);
    put_u32(out, 1u);
    put_u32(out, static_cast<std::uint32_t>(scanner_name.size()));
    for (char c : scanner_name) put_u8(out, static_cast<std::uint8_t>(c));

    std::uint32_t flags = compressed ? 1u : 0u;
    put_u32(out, flags);

    std::uint32_t nclasses = static_cast<std::uint32_t>(dfa.nclasses);
    std::uint32_t nstates  = static_cast<std::uint32_t>(dfa.states.size());
    std::uint32_t nrules   = static_cast<std::uint32_t>(nfa.rule_trail.size());
    std::uint32_t pool_size = 0;

    CompressedDFA c{};
    if (compressed) {
        c = compress_dfa(dfa);
        pool_size = static_cast<std::uint32_t>(c.pool_size);
    }

    put_u32(out, nclasses);
    put_u32(out, nstates);
    put_u32(out, nrules);
    put_u32(out, pool_size);

    for (unsigned i = 0; i < 256; ++i)
        put_u8(out, dfa.eclasses.ec[i]);

    for (std::uint32_t i = 0; i < nclasses; ++i) {
        std::uint8_t v = i < dfa.meta.size()
            ? dfa.meta[i] : static_cast<std::uint8_t>(i);
        put_u8(out, v);
    }

    if (compressed) {
        for (auto v : c.yy_base) put_i32(out, v);
        for (auto v : c.yy_def)  put_i32(out, v);
        for (auto v : c.yy_nxt)  put_i32(out, v);
        for (auto v : c.yy_chk)  put_i32(out, v);
    } else {
        for (const auto& st : dfa.states) {
            for (std::uint32_t cl = 0; cl < nclasses; ++cl)
                put_i32(out, st.next[cl]);
        }
    }

    for (const auto& st : dfa.states) put_i32(out, st.accept_normal);
    for (const auto& st : dfa.states) put_i32(out, st.accept_eol);

    put_u32(out, static_cast<std::uint32_t>(dfa.cond_starts.size()));
    for (const auto& cs : dfa.cond_starts) put_i32(out, cs.normal);
    for (const auto& cs : dfa.cond_starts) put_i32(out, cs.bol);

    for (auto v : nfa.rule_trail) put_i32(out, v);

    return out;
}

bool write_tables_file(const std::string& path,
                       const NFA& nfa, const DFA& dfa,
                       bool compressed,
                       const std::string& scanner_name) {
    auto bytes = serialise_tables(nfa, dfa, compressed, scanner_name);
    std::ofstream os(path, std::ios::binary);
    if (!os) return false;
    os.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
    return os.good();
}

} // namespace lexcpp
