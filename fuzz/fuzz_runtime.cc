// Fuzz the generated scanner runtime by feeding random bytes through
// yylex(). The grammar is fixed (runtime_grammar.l), generated at
// build time, and linked in.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
    int yylex(void);
    void yyrestart(FILE*);
    extern char *yytext;
    extern int yyleng;
    extern FILE *yyin;
    extern FILE *yyout;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size > 65536) return 0;

    FILE* in  = fmemopen(const_cast<std::uint8_t*>(data), size, "rb");
    if (!in) return 0;
    static FILE* devnull = nullptr;
    if (!devnull) devnull = fopen("/dev/null", "wb");
    if (!devnull) { fclose(in); return 0; }

    yyrestart(in);
    yyout = devnull;

    while (yylex() != 0) { /* drain */ }

    fclose(in);
    return 0;
}
