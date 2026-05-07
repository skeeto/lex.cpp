# lex.cpp

A portable C++20 implementation of `lex` that aims to be a drop-in replacement
for [flex](https://github.com/westes/flex) on the most common `.l` grammars.
It generates **C** scanners (the same convention as flex), so existing flex+bison
toolchains keep working.

* C++20, builds with CMake — no third-party deps.
* Linux, macOS, Windows (MSVC).
* UTF-8 safe by construction: regex matches on raw bytes, no `setlocale`,
  no `wctype.h`.
* Differential-tested against `flex 2.6.4` byte-for-byte (103 cases).
* Fuzzed under ASan + UBSan.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/src/lex --version
```

Useful CMake cache options:

| Option | Default | What it does |
|---|---|---|
| `LEXCPP_BUILD_TESTS` | `ON`  | Build the CTest suite |
| `LEXCPP_LEX_READY`   | `OFF` | Run the differential suite against `lex` (not just `flex`) |
| `LEXCPP_BUILD_FUZZ`  | `OFF` | Build the libFuzzer harnesses (clang only) |
| `LEXCPP_COVERAGE`    | `OFF` | Instrument for `llvm-cov` coverage |
| `LEXCPP_SANITIZE`    | `OFF` | `-fsanitize=address,undefined` |
| `LEXCPP_WERROR`      | `OFF` | Treat warnings as errors |

```sh
ctest --test-dir build --output-on-failure
```

## Usage

```sh
lex [OPTIONS] [scanner.l]
```

Reads `scanner.l` (or stdin), emits a C source file that defines `yylex()` and
the surrounding runtime.

| Flag | Meaning |
|---|---|
| `-o FILE`, `--outfile=FILE` | Output path (default `lex.yy.c`) |
| `-t`, `--stdout` | Write to stdout instead |
| `-i`, `--case-insensitive` | ASCII case-insensitive matching |
| `--yylineno` | Track line numbers in `yylineno` |
| `-P STR`, `--prefix=STR` | Replace the `yy` symbol prefix |
| `-s`, `--nodefault` | Suppress the default rule |
| `-L`, `--noline` | Don't emit `#line` directives |
| `--header-file[=PATH]` | Also write a companion `.h` |
| `-h`, `--help` / `-V`, `--version` | Self-evident |

## Supported flex features

### `.l` syntax

* Three-section format split on `%%`.
* Definitions: `name pattern` macros, `%{ ... %}` verbatim C, `%top { ... }`,
  `%s NAME`, `%x NAME`.
* `%option`: `noyywrap`, `yylineno`, `case-insensitive`, `prefix="..."`,
  `nodefault`, `debug`, `reentrant`, `bison-bridge`, `bison-locations`,
  `extra-type=...`, plus benign no-ops (`8bit`, `ecs`, `batch`, …).
* Rules: `pattern action`, `<SC,SC2>pattern`, `<*>pattern`, `<<EOF>>`,
  `|` rule sharing, brace-balanced action blocks.

### Regex

`.`, `*`, `+`, `?`, `|`, `()`, `[...]`, `[^...]`, `[a-z]`, `^`, `$`, `\n \t \r
\f \v \\ \" \a \b`, `\xHH`, `\NNN`, `"..."` literal strings, `{name}` macro
expansion (with recursion guard), `{n}` / `{n,m}` / `{n,}` repetition,
trailing context `r/s` (fixed-length tail).

### Runtime API

* Globals (non-reentrant): `yytext`, `yyleng`, `yylineno`, `yyin`, `yyout`,
  `BEGIN`, `ECHO`, `INITIAL`, `yywrap`, `yyterminate`, `yyless`, `unput`,
  `input`, `yyrestart`, `yylex`.
* Multi-buffer: `yy_create_buffer`, `yy_delete_buffer`, `yy_flush_buffer`,
  `yy_switch_to_buffer`, `yypush_buffer_state`, `yypop_buffer_state`,
  `yy_scan_string`, `yy_scan_bytes`, `yy_scan_buffer`, `YY_CURRENT_BUFFER`.
* Start-condition stack: `yy_push_state`, `yy_pop_state`, `yy_top_state`.
* Hooks: `YY_USER_ACTION`, `YY_USER_INIT` (define before `%{ ... %}`).
* Reentrant (`%option reentrant`): `yyscan_t`, `yylex_init`, `yylex_destroy`,
  `yylex_init_extra`, `yyget_*` / `yyset_*` for every state member.
* Bison glue (`%option bison-bridge`, `bison-locations`): `yylex(YYSTYPE*[,
  YYLTYPE*], yyscan_t)`; `yylval` / `yylloc` macros inside actions.
* `REJECT`: full priority-list fallback.
* `yymore()`: prepends the previous match onto the next.

### Code-quality features

* `#line N "scanner.l"` directives so debuggers and compiler errors point at
  the original `.l` instead of the generated C. Toggle with `-L`.
* `--header-file` for cross-TU setups (typical with bison-generated parsers).

## Out of scope

The following flex features are **not** implemented and will produce a
diagnostic:

* `%option c++` / C++ scanner classes.
* `%option lex-compat` / `posix-compat`.
* `%option array` / non-default `%pointer`.
* m4 backend, `--tables-file`, `-Cf`/`-CF` table-compression flags.
* Variable-length trailing context `r/s` where `s` has variable length.

## Repository layout

```
src/             # C++20 implementation
  main.cpp       # CLI driver
  diag.{hpp,cpp} # diagnostics
  source.{hpp,cpp} # .l source parser
  regex.{hpp,cpp}  # regex parser, NFA tree builder
  nfa.{hpp,cpp}    # Thompson NFA construction
  dfa.{hpp,cpp}    # subset construction
  codegen.{hpp,cpp}# emits the C scanner
runtime/runtime.c.in # the C runtime helpers, embedded into output
tests/
  cases/<NNN_name>/{scanner.l, input.txt}  # differential cases
  unit/test_internal.cpp                    # internals unit tests
  differential.cmake, run_diff.cmake        # CTest harness
fuzz/
  fuzz_regex.cc, fuzz_lfile.cc, fuzz_runtime.cc
  runtime_grammar.l, corpus/{regex,lfile,runtime}/
.github/workflows/ci.yml  # Linux × {gcc, clang}, sanitizers, coverage,
                          # fuzz smoke, macOS, Windows MSVC
```

## CI

GitHub Actions runs on every push:

* `linux-{gcc,clang}` — full differential suite + unit tests.
* `sanitizers` — ASan + UBSan.
* `coverage` — `llvm-cov` HTML report uploaded as an artifact.
* `fuzz-smoke` — 30 s of each libFuzzer harness.
* `macos`, `windows-msvc` — build + smoke.

## Status

* 103 differential cases + 1 unit binary (27 internal cases) — all passing
  against flex 2.6.4.
* `-Werror` clean on gcc 13 and clang 18.
* Zero ASan/UBSan findings across millions of fuzz iterations.
