#pragma once

#include "dfa.hpp"
#include "nfa.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lexcpp {

// Binary table-dump matching flex's --tables-file shape.
//
// Layout:
//   uint32_t  magic  = 0xF13C57B1  (network order)
//   uint32_t  version = 1
//   uint32_t  scanner_name_len
//   bytes     scanner_name (no NUL)
//   uint32_t  flags        (bit 0 = compressed)
//   uint32_t  nclasses
//   uint32_t  nstates
//   uint32_t  nrules
//   uint32_t  pool_size    (0 unless compressed)
//   uint8[256]   yy_ec
//   uint8[nclasses] yy_meta            (0..nclasses-1; identity if absent)
//   int32[nstates*nclasses] yy_nxt     (uncompressed) OR
//   int32[nstates] yy_base + yy_def, int32[pool_size] yy_nxt + yy_chk
//                                       (compressed)
//   int32[nstates] yy_accept_normal
//   int32[nstates] yy_accept_eol
//   uint32  ncond
//   int32[ncond] yy_cond_normal
//   int32[ncond] yy_cond_bol
//   int32[nrules] yy_rule_trail_len
//
// All multibyte ints are big-endian.
[[nodiscard]] std::vector<std::uint8_t> serialise_tables(
    const NFA& nfa, const DFA& dfa,
    bool compressed,
    const std::string& scanner_name);

bool write_tables_file(const std::string& path,
                       const NFA& nfa, const DFA& dfa,
                       bool compressed,
                       const std::string& scanner_name);

} // namespace lexcpp
