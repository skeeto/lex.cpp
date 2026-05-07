# lex.cpp

A portable C++20 implementation of `lex` that aims to be a drop-in replacement
for [flex](https://github.com/westes/flex). It generates **C** scanners (the
same convention as flex), so existing flex+bison toolchains keep working.

* C++20, builds with CMake — no third-party deps.
* Linux, macOS, Windows (MSVC).
* UTF-8 safe by construction: regex matches on raw bytes, no `setlocale`,
  no `wctype.h`.
* Tables compressed via flex's equivalence-class + comb scheme by default
  (`-Cem`); `-Cfe` and `-f` also available.
* Reentrant (`%option reentrant`), bison-bridge, multi-buffer, REJECT,
  yymore, variable-length trailing context, `--header-file`,
  `--tables-file` with runtime loader.
* Differential-tested against `flex 2.6.4` byte-for-byte (~120 cases).
* Fuzzed under ASan + UBSan.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/bin/lex --version
```

`cmake --install build` lays out:

```
<prefix>/bin/lex                       # the generator
<prefix>/lib/liblex.a                  # default yywrap + main, link with -llex
<prefix>/lib/pkgconfig/lex.pc          # pkg-config --libs lex
```

Useful CMake cache options:

| Option | Default | What it does |
|---|---|---|
| `LEXCPP_BUILD_TESTS`    | `ON`  | Build the CTest suite |
| `LEXCPP_LEX_READY`      | `OFF` | Differential suite runs both flex and lex |
| `LEXCPP_TEST_COMPRESS`  | `""`  | Force a compression mode for the sweep (`-f` / `-Cfe` / `-Cem`) |
| `LEXCPP_BUILD_FUZZ`     | `OFF` | libFuzzer harnesses (clang only) |
| `LEXCPP_COVERAGE`       | `OFF` | `llvm-cov` instrumentation |
| `LEXCPP_SANITIZE`       | `OFF` | `-fsanitize=address,undefined` |
| `LEXCPP_WERROR`         | `OFF` | Treat warnings as errors |

```sh
ctest --test-dir build --output-on-failure
```

## Usage

```sh
lex [OPTIONS] [scanner.l]
```

Reads `scanner.l` (or stdin), emits a C source file that defines `yylex()`
plus the surrounding runtime.

| Flag | Meaning |
|---|---|
| `-o FILE`, `--outfile=FILE` | Output path (default `lex.yy.c`) |
| `-t`, `--stdout`            | Write to stdout instead |
| `-i`, `--case-insensitive`  | ASCII case-insensitive matching |
| `--yylineno`                | Track line numbers in `yylineno` |
| `-P STR`, `--prefix=STR`    | Replace the `yy` symbol prefix |
| `-s`, `--nodefault`         | Suppress the default rule |
| `-L`, `--noline`            | Don't emit `#line` directives |
| `-f`, `--full`              | Dense `yy_nxt[states][256]` table, no compression |
| `-Cfe`                       | Dense `yy_nxt[states][nclasses]` + `yy_ec` |
| `-Cem`, `--compress`        | Default: `yy_ec` + `yy_meta` + `yy_base/def/nxt/chk` |
| `--header-file[=PATH]`      | Also write a companion `.h` |
| `--tables-file=PATH`        | Also write a binary table dump (loadable via `yytables_fload`) |
| `-h`, `--help` / `-V`, `--version` | Self-evident |

## Supported flex features

### `.l` syntax

* Three-section format split on `%%`.
* Definitions: `name pattern` macros, `%{ ... %}` verbatim C, `%top { ... }`,
  `%s NAME`, `%x NAME`.
* `%option`: `noyywrap`, `yylineno`, `case-insensitive`, `prefix="..."`,
  `nodefault`, `debug`, `reentrant`, `bison-bridge`, `bison-locations`,
  `extra-type=...`, `array` / `pointer`, plus benign no-ops (`8bit`,
  `batch`, `ecs`, …).
* Rules: `pattern action`, `<SC,SC2>pattern`, `<*>pattern`, `<<EOF>>`,
  `|` rule sharing, brace-balanced action blocks.

### Regex

`.`, `*`, `+`, `?`, `|`, `()`, `[...]`, `[^...]`, `[a-z]`, `^`, `$`,
`\n \t \r \f \v \\ \" \a \b`, `\xHH`, `\NNN`, `"..."` literal strings,
`{name}` macro expansion (with recursion guard), `{n}` / `{n,m}` / `{n,}`
repetition, trailing context `r/s` (both fixed and variable length).

A `dangerous trailing context` warning fires when `r` and `s` are both
variable-length, matching flex's heuristic.

### Runtime API

* Globals (non-reentrant): `yytext`, `yyleng`, `yylineno`, `yyin`, `yyout`,
  `BEGIN`, `ECHO`, `INITIAL`, `yywrap`, `yyterminate`, `yyless`, `unput`,
  `input`, `yyrestart`, `yylex`, `yy_set_bol`, `YY_AT_BOL`.
* Multi-buffer: `yy_create_buffer`, `yy_delete_buffer`, `yy_flush_buffer`,
  `yy_switch_to_buffer`, `yypush_buffer_state`, `yypop_buffer_state`,
  `yy_scan_string`, `yy_scan_bytes`, `yy_scan_buffer`, `YY_CURRENT_BUFFER`.
* Start-condition stack: `yy_push_state`, `yy_pop_state`, `yy_top_state`.
* Hooks: `YY_USER_ACTION`, `YY_USER_INIT`, `YY_INPUT(buf, result, max_size)`
  (define before `%{ ... %}` to override input source / column tracking).
* Reentrant (`%option reentrant`): `yyscan_t`, `yylex_init`,
  `yylex_init_extra`, `yylex_destroy`, `yyget_*` / `yyset_*` accessors.
* Bison glue (`%option bison-bridge` [+ `bison-locations`]):
  `yylex(YYSTYPE*[, YYLTYPE*], yyscan_t)`; `yylval` / `yylloc` macros
  available in actions.
* `REJECT`: full priority-list fallback (rule-id ASC, length DESC).
* `yymore()`: prepends the previous match onto the next.
* `--header-file` emits a companion `.h` with prototypes and start-condition
  `#define`s (for cross-TU setups, typical with bison-generated parsers).
* `--tables-file=PATH` writes a binary in flex's table format (magic
  `0xF13C57B1`); the generated scanner ships with `int yytables_fload(const
  char *path)` that swaps the loaded data in at runtime.

### Compression

`-Cem` (default) packs transitions through a row-displacement comb plus
equivalence-class + meta-equivalence-class indirection:

| mode  | tables                                    | scanner size on `140_many_rules` |
|-------|-------------------------------------------|----------------------------------|
| `-f`  | `yy_nxt[states][256]`, no `yy_ec`         | 49 KB |
| `-Cfe`| `yy_nxt[states][nclasses]` + `yy_ec`      | 28 KB |
| `-Cem`| `yy_base/def/nxt/chk` + `yy_ec` + `yy_meta`| 25 KB |

### Linking against `liblex`

`.l` files that don't `%option noyywrap` need an external `yywrap()` (and
optionally a default `main()`). Link `-llex`:

```sh
lex foo.l                 # writes lex.yy.c
cc lex.yy.c -llex -o foo
```

`pkg-config --libs lex` returns the right `-L`/`-l` flags after install.

## Out of scope

* `%option c++` / C++ scanner classes.
* `%option lex-compat` / `posix-compat`.
* m4 backend.

## Repository layout

```
src/             # C++20 implementation
  main.cpp       # CLI driver
  diag.{hpp,cpp} # diagnostics
  source.{hpp,cpp} # .l source parser
  regex.{hpp,cpp}  # regex parser, NFA tree builder
  nfa.{hpp,cpp}    # Thompson NFA construction
  eclass.{hpp,cpp} # byte-equivalence-class refinement
  dfa.{hpp,cpp}    # subset construction + comb compression
  codegen.{hpp,cpp}# emits the C scanner + optional yytables_fload
  tables.{hpp,cpp} # binary table-file serialiser (--tables-file)
runtime/runtime.c.in # the C runtime helpers, embedded into output
lib/             # liblex.a sources (yywrap.c, main.c, lex.pc.in)
tests/
  cases/<NNN_name>/{scanner.l, input.txt}  # differential cases
  unit/test_internal.cpp                    # internals unit tests
  differential.cmake, run_diff.cmake        # CTest harness
fuzz/
  fuzz_regex.cc, fuzz_lfile.cc, fuzz_runtime.cc
  runtime_grammar.l, corpus/{regex,lfile,runtime}/
.github/workflows/ci.yml
```

## CI

GitHub Actions runs on every push:

* `linux-{gcc,clang}` × `{-Cem, -Cfe, -f}` — full differential suite + unit
  tests + install dry-run + `pkg-config --libs lex` check.
* `sanitizers` — ASan + UBSan, full suite.
* `coverage` — `llvm-cov` HTML report uploaded as an artifact.
* `fuzz-smoke` — 30 s of each libFuzzer harness.
* `reentrant-only` — focused run on `%option reentrant` cases.
* `macos`, `windows-msvc` — build + smoke.

## Status

* ~120 differential cases + 1 unit binary (~30 internal cases) — all passing
  against flex 2.6.4 in every compression mode.
* `-Werror` clean on gcc 13 and clang 18.
* Zero ASan/UBSan findings across the iteration's fuzz runs.
