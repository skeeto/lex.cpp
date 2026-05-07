#pragma once

#include "dfa.hpp"
#include "nfa.hpp"
#include "source.hpp"

#include <string>

namespace lexcpp {

enum class CompressMode {
    Full,        // -f      : dense yy_nxt[s][256], no yy_ec, no yy_meta
    FullEc,      // -Cfe    : dense yy_nxt[s][nclasses] + yy_ec
    Compress,    // -Cem    : yy_base/def/nxt/chk + yy_ec + yy_meta
};

struct CodegenInput {
    const LexFile* file = nullptr;
    const NFA*     nfa  = nullptr;
    const DFA*     dfa  = nullptr;
    std::string    output_path;            // e.g. "lex.yy.c"
    bool           emit_line_directives = true;
    CompressMode   compress = CompressMode::Compress;
};

[[nodiscard]] std::string emit_c(const CodegenInput& in);

// Emits a companion header for the generated scanner: typedefs,
// start-condition #defines, and extern/prototype declarations. The
// caller picks the path; we only build the text.
[[nodiscard]] std::string emit_h(const CodegenInput& in);

} // namespace lexcpp
