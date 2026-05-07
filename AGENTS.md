# AGENTS.md

Guidance for AI coding agents (Claude Code, Cursor, etc.) working on this
repository.

## What this project is

`lex.cpp` is a flex-compatible scanner generator: it reads `.l` files and
emits **C** that defines `yylex()` and a small runtime. The agent-relevant
fact is that the project is **test-driven against the real flex 2.6.4**:
every behavioural change has to keep the differential CTest suite green.

## Build & test loop

```sh
cmake -S . -B build -DLEXCPP_LEX_READY=ON   # ON enables differential tests
cmake --build build -j
ctest --test-dir build --output-on-failure
```

When iterating: re-run only the affected tests.

```sh
ctest --test-dir build -R 162    # one test
ctest --test-dir build -R reject # all reject tests
```

After CMake variable changes, **always re-configure** before re-running. New
test directories aren't picked up by an incremental rebuild.

To check sanitizer cleanliness:

```sh
cmake -S . -B build-san -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DLEXCPP_LEX_READY=ON -DLEXCPP_SANITIZE=ON
cmake --build build-san -j && ctest --test-dir build-san
```

To run a fuzz smoke (clang only):

```sh
cmake -S . -B build-fuzz -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DLEXCPP_BUILD_FUZZ=ON -DLEXCPP_BUILD_TESTS=OFF
cmake --build build-fuzz -j
./build-fuzz/fuzz/fuzz_regex   -max_total_time=30 fuzz/corpus/regex
./build-fuzz/fuzz/fuzz_lfile   -max_total_time=30 fuzz/corpus/lfile
./build-fuzz/fuzz/fuzz_runtime -max_total_time=30 fuzz/corpus/runtime
```

## Code organisation

| File | Responsibility |
|---|---|
| `src/main.cpp` | CLI parsing, drives the pipeline. Defines `core_main(argc, argv)` -- the entry lives in the platform layer below. |
| `src/platform.hpp` | I/O abstraction: move-only `File`, UTF-8 paths, `stdin_/stdout_/stderr_stream()`, `slurp`. Everything in `src/` above this layer goes through it -- no `<cstdio>` / `<iostream>` / `<fstream>`. |
| `src/platform_posix.cpp` | POSIX backend: stdio-based `File`, paths pass through, `int main(argc, argv)` treats argv as UTF-8. Defines `main` unless `LEXCPP_PLATFORM_NO_MAIN` is set. |
| `src/platform_windows.cpp` | Windows backend: `_wfopen` + UTF-8 path conversion, `O_BINARY` on stdio, `main()` ignores argv and re-fetches it via `GetCommandLineW + CommandLineToArgvW` to avoid ANSI codepage corruption. Same `LEXCPP_PLATFORM_NO_MAIN` guard. Links `shell32`. |
| `src/diag.{hpp,cpp}` | error / warning collection. `snprintf` formats into a stack buffer; the actual write goes through `platform::log_stderr`. |
| `src/source.{hpp,cpp}` | parses `.l` into a `LexFile` AST |
| `src/regex.{hpp,cpp}` | parses one regex pattern into a `Node` tree (and `parse_pattern` for `r/s`) |
| `src/nfa.{hpp,cpp}` | Thompson construction; per-rule BOL/EOL/trail flags |
| `src/dfa.{hpp,cpp}` | subset construction; each `DFAState` has `accept_normal`, `accept_eol`, `accept_list` |
| `src/eclass.{hpp,cpp}` | byte-equivalence-class refinement (computed from NFA edges, before subset construction) |
| `src/dfa.{hpp,cpp}` | subset construction; `DFAState` has `accept_normal/accept_eol/accept_list/boundary_rules`; also `compress_dfa` for comb packing |
| `src/codegen.{hpp,cpp}` | emits the C scanner: prelude, tables, runtime, `yylex` body, epilogue. Per-mode tables: `-f` (dense 256), `-Cfe` (dense nclasses), `-Cem` (yy_base/def/nxt/chk + yy_meta) |
| `src/tables.{hpp,cpp}` | binary table-file serialiser used by `--tables-file` (network-order; magic 0xF13C57B1) |
| `runtime/runtime.c.in` | the C runtime helpers (buffers, BEGIN/ECHO/yyless/unput/input, yymore, push/pop state, init/destroy in reentrant mode). Embedded as `kRuntimeTemplate` via `cmake/EmbedFile.cmake`. |
| `lib/{yywrap,main}.c` | tiny static archive (`liblex.a`) with default `yywrap` and `main`; pulled in only when not user-provided. Each function in its own TU so the linker doesn't drag both. |

Pipeline: `parse_lex_file → parse_pattern (per rule) → init_nfa →
add_rule_to_nfa → compute_eclasses → build_dfa → [compress_dfa] → emit_c [+ emit_h] [+ write_tables_file]`.

## Conventions

* C++ headers use `.hpp`. C runtime files use `.c.in`/`.c`.
* No exceptions. Errors flow through `Diagnostics`; functions return
  `std::optional` or a tagged status.
* `-Werror` clean on gcc 13 and clang 18 with `-Wall -Wextra -Wpedantic
  -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Wcast-align
  -Wnon-virtual-dtor -Woverloaded-virtual -Wnull-dereference
  -Wdouble-promotion -Wformat=2`. New code must hold this bar.
* Use `static_cast<>` not C-style casts.
* `src/` must stay portable: only standard C++20 headers, no
  `<unistd.h>`/`fork`/`pipe`. POSIX-only APIs (`fmemopen`) are confined to
  `fuzz/`, which is clang-Linux-only anyway.
* Generated C must compile under MSVC, gcc, and clang. Use C99 features only;
  no compound literals as initialisers in tables, etc.
* Default to writing no comments; only add one when the *why* is non-obvious.

## Testing model

The differential harness (`tests/differential.cmake` +
`tests/run_diff.cmake`) is the contract: for each `tests/cases/<NNN_name>/`,

1. Run `flex` on `scanner.l`, compile, run with `input.txt` on stdin.
2. Run `lex` on the same `.l`, compile, run.
3. Compare stdout byte-for-byte.

To add a feature, write a case **first** and confirm it passes against flex
(it'll fail on the lex side until you implement). Then make `lex` match.

`tests/unit/test_internal.cpp` is a small no-deps unit harness for internal
APIs (regex parser error paths, codegen text inspection, etc.).

## Sharp edges

These are the surprising bits — read these before touching the runtime.

### The macro / field-name collision

`runtime/runtime.c.in` exposes `yytext`, `yyleng`, `yy_buf`, `yy_buf_pos`,
`yy_buf_end`, `yy_at_bol`, `yy_buffer_stack`, etc. as `#define`s that resolve
through `YY_G->...` (reentrant) or globals (non-reentrant).

Because the C preprocessor expands these tokens *unconditionally*, `b->yy_buf`
inside a runtime helper would expand to `b->(YY_CURRENT_BUFFER->yyb_data)` —
syntactically nonsense.

**Rule:** struct field names never overlap with `#define`d macros. The buffer
struct uses `yyb_data`, `yyb_size`, `yyb_pos`, `yyb_end`, `yyb_at_bol`,
`yyb_done`. The scanner-state struct uses `yytext_r`, `yyleng_r`,
`yy_buffer_stack_r`, etc. Public macros (`yytext`, `yy_buf`, ...) point at
the suffixed fields.

If you add a new field, give it a `_r` (or `yyb_`) suffix.

### Reentrant via `YY_DECLPM` / `YY_CALLPM`

Helpers take a varying parameter list:

```c
static void yy_refill(YY_DECLPM);          // void  | yyscan_t yyscanner
yy_refill(YY_CALLPM);                       // ()    | (yyscanner)
```

For helpers with their own arguments, use `YY_DECLPM_C` / `YY_CALLPM_C`
(the `_C` form starts with a comma so `(int x, yyscan_t s)` works). The
codegen-emitted `yylex` body **always** writes `func(YY_CALLPM)`; both modes
expand correctly.

### REJECT, EOL, and trailing context don't compose

The default `yy_lex_body` handles longest-match including the EOL
(`accept_eol`) and trailing-context rewind. The REJECT-aware body is a
separate function (`yy_lex_body_reject`) — selected when `uses_reject` is set,
which is detected by scanning every action's source for the whole-word token
`REJECT`.

REJECT mode currently does NOT honour `accept_eol`. Mixing `$` with REJECT in
the same `.l` may diverge from flex.

### `yymore()` and the buffer

`yymore()` doesn't move bytes — it just sets `yy_more_flag = 1`. Next
iteration, the yylex body slides `yytext` left by `yy_more_offset` bytes and
grows `yy_len`. Those bytes are still in `yy_buf` because the runtime slurps
the entire input up front; never trim `yy_buf` in helpers.

### Macros over `extern char *yytext` in reentrant mode

Reentrant mode redefines `yytext` to a struct accessor (`((yyguts_t*)
yyscanner)->yytext_r`). Therefore `extern char *yytext;` *must not* appear in
user code under `%option reentrant` — it expands to garbage. Tell users to
call `yyget_text(scanner)` instead.

### Init order in the generated file

The order in the emitted `.c` is fixed and non-trivial:

1. `#include <stdio.h>` …
2. `#define YY_REENTRANT 0/1`
3. Forward typedefs (`YY_BUFFER_STATE`, `yyscan_t`)
4. **User `%{ ... %}` block** (so it can refer to those typedefs)
5. Prefix `#define`s (yy* → <prefix>*)
6. Condition `#define`s (`INITIAL = 0`, `STR = 1`, …)
7. `YY_TRACK_LINENO`, `YY_ARRAY`, `YY_EXTRA_TYPE`
8. **Tables** (`yy_ec`, then either `yy_nxt[s][cls]` *or*
   `yy_base/yy_def/yy_nxt/yy_chk` + `yy_meta`, plus `yy_accept_*`,
   `yy_cond_*`, `yy_rule_trail_len`, optionally `yy_accept_off/pool` and
   `yy_boundary_off/pool`)
9. Embedded **runtime helpers** from `runtime.c.in`
10. `YY_TRANSITION` macro (per-mode lookup)
11. `yylex` body (with action dispatch)
12. Optional `yytables_fload` when `--tables-file` is set
13. `yywrap` if `noyywrap`
14. **User section 3** verbatim

Steps 4 and 14 get `#line` brackets when `emit_line_directives` is on. Don't
reorder these — moving the user prologue after the tables breaks any user code
that defines `YYSTYPE` / `YY_USER_ACTION` / `YY_DECL`.

### Compression menu

`-f` / `-Cfe` / `-Cem` (the default) feed `compress_mode` into
`CodegenInput`. The lookup macro `YY_TRANSITION(s, b)` is emitted
differently per mode; both `yy_lex_body` and `yy_lex_body_reject`
unconditionally call it, so adding a new mode only touches the macro
emission and the table emission.

Naming caveat: there's a *local* variable in the yylex body called `yy_mb`
("match base"). The original was `yy_base`, but it collided with the new
`yy_base[]` table when comb compression landed. If you add another
top-level table, watch for similar collisions.

### `--tables-file` runtime loader

When `--tables-file` is set, codegen drops `const` from every table and
emits `int yytables_fload(const char *path)`. The reader walks our
network-order binary (`tables.cpp::serialise_tables`), validates magic +
dimensions, then `fread`s each table back into the static arrays.

If you add a new table to codegen.cpp, you must:
1. Append it to `tables.cpp::serialise_tables` in the same order codegen
   emits it.
2. Append the matching `yy_read_int_array(...)` call in the codegen-emitted
   `yytables_fload` body.

The two writers/readers are coupled by *order*, not by name. Mismatch =
silent garbage at runtime. Keep the order documented in `tables.hpp`'s
header comment.

### `%option array`

Selects a fixed `char yytext[YYLMAX]` instead of the default `char *yytext`.
The yylex body has three places that previously did `yytext = yy_buf +
something` and now branch on `f.options.array`:
- match path: `memcpy` into yytext, NUL-terminate, truncate at YYLMAX-1.
- EOF path:   `yytext[0] = 0;`.
- yymore path: skipped under array (yymore + array isn't defined by flex).

The runtime's `yyguts_t::yytext_r` is `char[YYLMAX]` under `YY_ARRAY`,
`char *` otherwise. Same `yytext` macro works in both cases (array decays
to pointer when read; only assignment differs).

### Variable trailing context

`r/s` with variable-length `s` works via NFA boundary markers. A
`NodeKind::TrailBoundary` produces an NFA state with `is_boundary = true`;
`add_rule_to_nfa` collects all such states per rule into
`nfa.rule_boundary_states`. DFA's `accept_for` records, per state, which
rules' boundary markers are in its NFA-set. The runtime maintains
`yy_boundary_pos[NRULES]` during the scan and rewinds `yy_len` to the
boundary on accept.

A `dangerous trailing context` warning fires when both `r` and `s` have
variable length (`fixed_length(r) < 0` is the trigger).

### Phase 11+ artefacts

Iteration 1's plan listed phases A–F; Iteration 2 added 0–10; Iteration 3
added 11–16; Iteration 3+ (this round) added 17–21. The plan file
(`/root/.claude/plans/i-want-a-unix-glimmering-elephant.md`) tracks
intent. Don't trust commit-message phase numbers as a definitive index;
they're snapshots in time.

## How to extend

If you're adding a flex-compatible feature, the typical sequence is:

1. **Find the flex behaviour first**: `apt-get install flex`, write a tiny
   `.l`, run it on the input you'll use as the test, compare your assumption
   to reality. Flex's docs lie about edge cases more often than you'd think.
2. Write the differential test case under `tests/cases/<NNN_name>/`.
   Re-configure CMake so it picks up the new directory. Confirm it passes
   against flex with `LEXCPP_LEX_READY=OFF`.
3. Decide which layer owns the feature:
   * New `%option` flag → `source.cpp::apply_option`, mirror in `LexFile::Options`.
   * New regex syntax → `regex.cpp` + a `NodeKind` if it's a new node.
   * New runtime API → `runtime/runtime.c.in`. Reuse `YY_DECLPM`/`YY_CALLPM`.
   * New table → `codegen.cpp` (emit it next to the existing `append_*_array`
     calls) and reference from the runtime / the yylex body.
4. Re-run the full suite (`ctest`), ASan suite (`build-san`), and a 30 s
   fuzz smoke. Don't push if any are red.

## Sub-tasks the agent should *not* attempt

* Adding `-Cf` / `-CF` table compression. Out of scope for now; would touch
  every emitter and runtime path.
* Generating a C++ scanner class (`%option c++`). Would replace the runtime
  wholesale.
* m4 backend.

If a user request lands here, push back, propose a smaller subset, and ask.

## Pitfalls observed in past sessions

* **CMake doesn't auto-discover new test dirs** in incremental builds; you
  must re-run `cmake -S . -B build` after adding `tests/cases/<NEW>/`.
* **Generated scanners trigger `-Werror=implicit-function-declaration`** under
  modern clang; the test harness already passes `-w
  -Wno-error=implicit-function-declaration -Wno-error=int-conversion
  -Wno-error=incompatible-pointer-types`. Don't remove those flags.
* **`flex` and `clang ASan` runtime libs** must be installed in the
  environment; the GitHub Actions matrix does this, but a local clean machine
  needs `apt install flex llvm clang clang-tools libclang-rt-18-dev gcovr`.
* **Differential tests fail mysteriously on UB**: when a test prints garbage
  for both flex and lex (e.g., the bison-bridge case with a plain `int
  YYSTYPE`), suspect undefined behaviour in the *test*, not the implementation.
  Use a `union` / `struct` for `YYSTYPE`.
* **`yylineno_r` initial value differs by mode**: `1` in non-reentrant
  (matches flex), `0` in reentrant (also matches flex — yes, it's
  inconsistent). Don't unify them.
