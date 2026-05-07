# Inside lex and flex: how Unix builds scanners from regular expressions

**Lex and flex convert regular expressions into high-performance C scanners through a pipeline of automata-theoretic transformations: Thompson's construction builds an NFA, subset construction determinizes it into a DFA, and the DFA is encoded into compressed transition tables embedded in generated C code.** Flex, written by Vern Paxson in 1987 at Lawrence Berkeley National Laboratory, replaced AT&T lex with a fundamentally faster architecture—block-buffered I/O, equivalence-class-reduced alphabets, comb-compressed transition tables, and a tight inner loop that GCC compiles to roughly **12 machine instructions** per input character. The result is scanners that run **3–4× faster** than lex's output while producing object files one-quarter the size. This document traces every stage of that pipeline in implementation detail, from the regex parser to the runtime scanning loop.

---

## 1. The compilation pipeline: regex to machine code

Both lex and flex follow the same conceptual pipeline, established by the Dragon Book (Aho, Sethi, Ullman, 1986) and rooted in foundational work by Thompson (1968), Rabin and Scott (1959), and McNaughton and Yamada (1960):

```
Lexer Specification (.l file)
    │
    ▼
[1] Parse regular expressions; build NFA via Thompson's construction
    ├── One NFA fragment per rule, each with a tagged accepting state
    └── Combined via a new start state with ε-transitions to all fragments
    │
    ▼
[2] NFA → DFA via subset construction (powerset construction)
    ├── Each DFA state = a set of NFA states
    └── Accepting DFA states tagged with highest-priority rule number
    │
    ▼
[3] (Optional) DFA minimization
    └── Flex skips this; uses table compression instead
    │
    ▼
[4] Table generation and compression
    ├── Lex: yycrank[] verify-advance pairs + yysvec[] state vector
    └── Flex: base/def/nxt/chk arrays + equivalence classes
    │
    ▼
[5] Code generation → lex.yy.c
    └── Table-driven yylex() with maximal-munch scanning loop
```

The specification file has three sections separated by `%%`: definitions (named patterns, C preamble code), rules (regex–action pairs), and user subroutines (copied verbatim into the output).

---

## 2. Thompson's construction: regular expressions to NFAs

Ken Thompson's 1968 CACM paper introduced the algorithm that converts a regular expression into an ε-NFA with guaranteed properties: **exactly one start state** (no incoming edges), **exactly one accepting state** (no outgoing edges), **at most two outgoing transitions per state** (either one labeled transition or up to two ε-transitions), and **linear size**—for a regex of length *m*, the NFA has at most 2*m* states and 4*m* transitions.

### The compilation process

The regex is first parsed with explicit concatenation operators inserted, then converted to postfix notation via the shunting-yard algorithm. A stack-based compiler scans the postfix left-to-right, maintaining a stack of NFA *fragments*—each fragment has a start state and a list of dangling output arrows not yet connected to anything.

### Construction rules for each operator

**Single character `a`:** Create a state *s* with one transition labeled `a`. Push a fragment starting at *s* with one dangling arrow.

**Concatenation `e₁e₂`:** Pop fragments *e₂* then *e₁*. Patch *e₁*'s dangling arrows to *e₂*'s start state. Push a new fragment with *e₁*'s start and *e₂*'s dangling arrows. This chains the two sub-NFAs end-to-start.

**Alternation `e₁|e₂`:** Pop both fragments. Create a new Split state with ε-transitions to both start states. Push a fragment starting at the Split, with dangling arrows from both branches combined. The resulting NFA forks into either sub-expression.

**Kleene star `e*`:** Pop fragment *e*. Create a Split state with one ε-transition to *e*'s start and one dangling bypass arrow. Patch *e*'s output back to the Split (creating the loop). Push the fragment starting at Split with the bypass arrow dangling. This allows zero or more repetitions.

**Plus `e⁺`:** Like Kleene star, but the fragment starts at *e*'s original start state (not the Split), forcing at least one pass through *e* before the loop-or-exit decision.

**Optional `e?`:** Create a Split state with one ε-transition to *e*'s start and one bypass arrow. Push a fragment starting at Split with dangling arrows from both *e*'s output and the bypass. This matches zero or one occurrence.

### Combining multiple lexer rules

For a lexer with rules R₁, R₂, …, Rₙ, each regex Rᵢ is compiled into a separate Thompson NFA with its own accepting state tagged with rule number *i*. A new global start state is created with ε-transitions to every sub-NFA's start state. This single combined NFA can simultaneously recognize all patterns. The rule tags propagate through subsequent stages to enable disambiguation.

---

## 3. Subset construction: NFA to DFA

The subset construction algorithm (also called powerset construction), formalized by Rabin and Scott in 1959 and presented as Algorithm 3.20 in the Dragon Book, converts the combined NFA into a DFA where each DFA state corresponds to a **set of simultaneously active NFA states**.

### The ε-closure and move operations

The ε-closure of a set *S* of NFA states is the set of all states reachable from any state in *S* by following zero or more ε-transitions. It is computed by a simple depth-first or breadth-first traversal starting from *S*, following only ε-edges. The move operation `move(T, a)` returns the set of NFA states reachable from any state in set *T* on a single transition labeled *a* (no ε-transitions).

### The worklist algorithm

```
q₀' ← ε-closure({NFA start state})      // DFA start state
Dstates ← {q₀'};  worklist ← [q₀']

while worklist is not empty:
    T ← worklist.removeFirst()
    for each input symbol a ∈ Σ:
        U ← ε-closure(move(T, a))
        if U ≠ ∅ and U ∉ Dstates:
            add U to Dstates and worklist
        Dtran[T, a] ← U

// Accepting states: any T containing an NFA accepting state
// Priority: tag each such T with the lowest-numbered rule among its NFA accept states
```

For each unprocessed DFA state *T* and each input symbol *a*, the algorithm computes the set of NFA states reachable on that input (with ε-closure), checks whether this set has been seen before, and records the transition. **The worst case is exponential**—an *n*-state NFA can yield up to 2ⁿ DFA states—but for practical programming-language lexers, the blowup is modest, typically comparable to the NFA size.

### Handling rule priority

When a DFA state contains multiple NFA accepting states from different rules, it is tagged with the **lowest-numbered rule** (highest priority = listed first in the `.l` file). This resolves same-length match ambiguity at construction time, so the runtime scanner never needs to compare rule numbers.

---

## 4. DFA minimization: theory versus practice

### Hopcroft's O(n log n) algorithm

The theoretically optimal DFA minimization algorithm, published by John Hopcroft in 1971, uses **partition refinement**. The algorithm starts with a coarse partition separating states by their accepting-rule tags (states accepting rule 1, rule 2, …, rule *k*, and non-accepting states form the initial groups). It iteratively splits groups: for a splitter set *A* and input symbol *c*, compute X = {states whose *c*-transition lands in *A*}. Any group *Y* that is split by X (i.e., Y ∩ X ≠ ∅ and Y \ X ≠ ∅) is replaced by its two halves.

Hopcroft's key optimization: when splitting *Y* into two parts, only the **smaller half** is added to the worklist. This ensures each transition participates in O(log n) splits, yielding total time **O(n · |Σ| · log n)**. The critical constraint for lexers is that states accepting different rules must never be merged, which is enforced by the initial partition.

### Flex skips minimization entirely

A notable practical finding: **flex does not perform DFA minimization**. Analysis of flex's source code and the YooLex reimplementation project confirms that flex relies entirely on table compression (equivalence classes, default-state chains, comb packing) to achieve compactness. The subset construction already produces a reasonably compact DFA for typical lexer specifications, and the compression layers handle remaining redundancy more effectively than merging states would. AT&T lex similarly relied on its compression scheme (character equivalence via `yymatch[]` and fallback states via `yyother`) rather than explicit minimization.

---

## 5. How AT&T lex encodes the DFA in generated C

The original lex (Lesk and Schmidt, 1975) generates `lex.yy.c` with a characteristic structure. Eric Schmidt wrote most of the code; Mike Lesk designed the system and authored the paper. The generated file contains seven major sections.

### Table structures

The DFA is encoded in three core data structures:

**`yycrank[]`** is the main transition table, an array of `struct yywork { YYTYPE verify, advance; }` pairs. Each state has a base pointer into this array. To look up a transition for state *s* on character *c*, the scanner computes `yyt = s->yystoff + c`, then checks `yyt->verify == s`. If verified, the next state is `yyt->advance`. This is a **verify-advance sparse table**—rows from different states overlap in the array, with the verify field disambiguating ownership.

**`yysvec[]`** is the state vector, one entry per DFA state:

```c
struct yysvf {
    struct yywork *yystoff;   // base pointer into yycrank
    struct yysvf *yyother;    // fallback state for default transitions
    int *yystops;             // pointer into yyvstop (NULL if non-accepting)
};
```

The `yyother` field enables **default-state compression**: if a transition is not found in the current state's row, the scanner follows `yyother` to try a fallback state that shares most of the same transitions.

**`yyvstop[]`** stores accepting-rule numbers. Each accepting state's `yystops` pointer indexes into this flat array, with entries terminated by 0. Negative values indicate trailing context rules.

**`yymatch[256]`** maps each byte value to a character equivalence class representative. When using compressed (negative-offset) states, the scanner first tries the raw character, then falls back to `yymatch[c]` to find the equivalence class representative.

### The yylex() / yylook() split

AT&T lex separates the scanning loop into two functions. `yylex()` contains a switch statement dispatching user actions by rule number. It calls `yylook()` in a loop; `yylook()` performs the actual DFA simulation and returns the matched rule number. User actions from each rule become `case` bodies in the switch. The REJECT macro is implemented as `{ nstr = yyreject(); goto yyfussy; }`, which jumps back into the switch to try the next-best match.

### Input handling

Original lex uses **character-at-a-time I/O** through `getc(yyin)`, with a pushback buffer (`yysbuf[]` / `yysptr`) for `unput()`. The `input()` macro reads from the pushback stack first, falling back to `getc()`. The matched text accumulates in `yytext[]`, a fixed-size character array of `YYLMAX` bytes (typically 200). This design is simple but slow—every character requires a function call through stdio.

### Start conditions

AT&T lex supports only **inclusive start conditions** (`%Start` / `%S`). Each condition occupies two consecutive DFA start states—one for normal entry and one for beginning-of-line (BOL). `BEGIN(n)` sets `yybgin = yysvec + 1 + n`. BOL is handled by incrementing the state pointer when the previous character was a newline. Rules without a start condition prefix are compiled into all start states.

---

## 6. The runtime scanning loop and longest-match semantics

The maximal-munch (longest-match) algorithm is the heart of every lex/flex scanner. It works by **never stopping at the first accepting state**—instead, the scanner records the most recent accepting state and continues reading as long as valid DFA transitions exist.

### The algorithm in detail

```
while input is not exhausted:
    state ← start_state
    last_accept_state ← NONE
    last_accept_pos ← current_pos
    
    // Forward scan: advance DFA as far as possible
    while state ≠ DEAD and not EOF:
        if state is accepting:
            last_accept_state ← state
            last_accept_pos ← current_pos
        c ← next character
        state ← transition[state][c]
    
    // Decision
    if last_accept_state ≠ NONE:
        retract input to last_accept_pos
        yytext ← matched text; yyleng ← length
        execute action for last_accept_state's rule
    else:
        // Default rule: echo one character, advance by 1
        output(current character)
```

Consider rules `foo` and `foobar` with input "foob#". The scanner reaches an accepting state after "foo" (recording it), continues through "foob" (non-accepting but valid transitions toward "foobar"), hits '#' with no transition (dead state), then **backtracks** to the recorded position after "foo", pushes 'b' and '#' back into the input, and returns the `foo` token.

### Rule priority for ties

When two rules match the same length, the one listed first in the `.l` file wins. This is resolved at DFA construction time: each accepting DFA state stores only the highest-priority rule number.

### The default rule and `-s` suppression

If no accepting state is ever reached during the forward scan, the scanner executes the **default rule**: consume exactly one character, copy it to stdout, and resume. This means the minimal legal flex input `%%` (no rules) produces a scanner that copies stdin to stdout character by character. The `-s` flag suppresses this behavior, causing an abort with "flex scanner jammed" on unmatched input—useful for verifying scanner completeness.

### Worst-case quadratic behavior

Thomas Reps proved in 1998 (ACM TOPLAS) that the standard maximal-munch algorithm exhibits **O(n²) worst-case time** in input length. This occurs when the scanner repeatedly reads far ahead, fails to match, backtracks, outputs one default character, and repeats. Reps showed that memoizing which DFA state was reached at each input position reduces this to guaranteed **O(n) time**, though neither lex nor flex implements this optimization.

---

## 7. Flex's architectural improvements over AT&T lex

Flex (Fast Lexical Analyzer Generator) was written by Vern Paxson around 1987, with key contributions from Van Jacobson (fast table design) and Kevin Gong (implementation). It completely supplanted AT&T lex due to superior speed, reliability, and free availability under a BSD license.

### Fundamental design changes

**Self-contained scanners.** Flex-generated scanners require no external runtime library (AT&T lex requires `-ll`). With `%option noyywrap`, the scanner is entirely self-sufficient.

**Pointer-based yytext.** Flex defaults to `%pointer` mode where `yytext` is a `char*` pointing directly into the input buffer, avoiding the copy into a fixed-size array that AT&T lex performs. This enables arbitrarily long tokens and eliminates per-character copy overhead. The `%array` directive restores lex-compatible behavior.

**Block-buffered I/O.** Flex reads input in **8 KB blocks** via `fread()` (or `read()` with `-Cr`), compared to lex's character-at-a-time `getc()`. The `YY_INPUT(buf, result, max_size)` macro is user-overridable. Two sentinel NUL bytes at the end of each buffer allow the inner loop to detect end-of-buffer without an explicit bounds check on every character.

**Exclusive start conditions.** Flex adds `%x` exclusive start conditions alongside `%s` inclusive ones. In exclusive mode, only rules explicitly tagged with that condition are active—a critical feature for cleanly handling multi-mode scanning (comments, string literals, etc.). Each start condition occupies two DFA start states (normal + BOL), with `BEGIN(sc)` setting `yy_start = 1 + 2*sc`.

**Correct macro expansion.** Flex parenthesizes pattern definitions when expanding them, fixing a longstanding AT&T lex bug where `foo{NAME}?` with `NAME [A-Z][A-Z0-9]*` would bind `?` to the wrong subexpression.

**No hard limits.** AT&T lex had fixed limits on NFA states, DFA states, and rules. Flex dynamically allocates all internal structures, supporting arbitrarily large scanners.

### The scanning loop difference

AT&T lex's `yylook()` records the full sequence of DFA states on a stack during the forward scan, then walks backward through the stack to find the last accepting state. Flex tracks `yy_last_accepting_state` and `yy_last_accepting_cpos` **during** the forward scan, checking `yy_accept[state]` at each step. This is simpler and avoids the post-scan backtrack traversal.

---

## 8. Table compression: how flex shrinks the DFA

Flex's table compression operates in multiple layers, each reducing a different dimension of the transition table. The default mode (`-Cem`) applies all layers.

### Equivalence classes (-Ce)

Flex analyzes all rules to identify characters that **always cause identical transitions in every DFA state**. These are grouped into equivalence classes. A mapping table `yy_ec[256]` translates each input byte to its class number. For a typical programming-language scanner, this reduces the effective alphabet from 256 to **30–60 classes**—a 4–8× reduction in table width. The runtime cost is one extra array lookup per character.

Critically, flex performs equivalence class analysis **before NFA-to-DFA conversion**, rewriting the NFA to use class-labeled transitions. This dramatically accelerates subset construction by reducing the number of per-state transitions to explore.

### Meta-equivalence classes (-Cm)

After DFA construction, flex identifies equivalence classes that behave identically across all DFA states—a second level of grouping. The `yy_meta[]` table maps equivalence classes to meta-classes, further reducing the effective alphabet for the sparse-table representation. The runtime cost is one or two additional comparisons per character.

### Comb/row-displacement compression

The core sparse-table technique derives from Stephen Johnson's 1975 yacc paper and is formalized in the Dragon Book (Section 3.9.8). It uses **four arrays**:

- **`yy_base[state]`**: Offset for each state's transitions in the shared pool
- **`yy_def[state]`**: Default/fallback state for transitions not explicitly stored
- **`yy_nxt[index]`**: Next-state values in the shared pool
- **`yy_chk[index]`**: Ownership verification (which state allocated this slot)

The lookup algorithm:

```c
ec = yy_ec[input_char];                    // equivalence class
while (yy_chk[yy_base[state] + ec] != state) {
    state = yy_def[state];                  // follow default chain
    if (state >= threshold) ec = yy_meta[ec]; // apply meta-class
}
next_state = yy_nxt[yy_base[state] + ec];
```

**Row displacement** is the key insight: since most DFA states have mostly identical transitions (differing only on a few characters), each state stores only its **differences** from a chosen default state. The non-default entries are packed into the shared `yy_nxt`/`yy_chk` pool using a greedy algorithm that overlaps sparse rows like interlocking comb teeth, placing each state's entries at the lowest offset where no collisions occur. States with identical transition vectors share the same base offset.

### The compression spectrum

Flex offers a range of compression/speed tradeoffs, from smallest/slowest to largest/fastest:

- **`-Cem`** (default): All compression layers active
- **`-Cm`**: Meta-equivalence classes + comb compression, no character ECs
- **`-Ce`**: Character ECs + comb compression, no meta-ECs
- **`-C`**: Comb compression only
- **`-Cfe`**: Full uncompressed tables with equivalence classes (good production compromise)
- **`-Cf`**: Full uncompressed two-dimensional `yy_nxt[state][ec]` array—maximum speed

With `-Cf` or `-CF`, the inner loop reduces to a single direct array index per character with no verify step, no default-chain following, and no meta-class mapping. These modes also bypass stdio, using `read()` directly.

---

## 9. Flex's buffer management architecture

Flex's `YY_BUFFER_STATE` is a pointer to `struct yy_buffer_state`, which contains the character buffer (`yy_ch_buf`), its size (`yy_buf_size`, default **16,384 bytes**), current position, associated `FILE*`, and status flags. Two sentinel NUL bytes at the buffer's end let the scanning loop detect end-of-buffer conditions without per-character bounds checking.

### Buffer operations

Flex provides a complete buffer management API: `yy_create_buffer()` and `yy_delete_buffer()` for lifecycle management; `yy_switch_to_buffer()` for switching between buffers; `yypush_buffer_state()` and `yypop_buffer_state()` for a built-in buffer stack (ideal for `#include` processing); and `yy_scan_string()`, `yy_scan_bytes()`, and `yy_scan_buffer()` for scanning from memory. The last of these scans in-place without copying, provided the last two bytes are NUL sentinels.

When the scanner reaches the end of the current buffer, it saves match state, shifts unmatched characters to the buffer's beginning, calls `YY_INPUT()` to refill, and resumes. If the current token exceeds the buffer size, the buffer is dynamically resized—though this triggers a full rescan of the token-so-far and is deliberately slow. On EOF, `yywrap()` is called to determine whether another input source is available.

AT&T lex's `unput()` pushes characters onto a separate stack (`yysbuf`). Flex's `unput()` inserts characters directly back into the input buffer, which is why it invalidates `yytext` in pointer mode (it shifts buffer contents). The flex manual recommends `yyless()` (a cheap macro that adjusts a pointer) over `unput()` (an expensive function) whenever possible.

---

## 10. REJECT, trailing context, and their performance costs

### REJECT: the most expensive feature

The `REJECT` action directs the scanner to discard the current match and find the **next-best alternative**—either a different rule matching the same length or any rule matching a shorter prefix. When any rule uses REJECT, flex fundamentally restructures the generated scanner:

- A **full state history** (`yy_state_buf`) records every DFA state visited during the forward scan, not just the last accepting one.
- The `yy_accept[]` array is replaced by an `yy_acclist[]` that enumerates **all** accepting rules at each state, not just the highest-priority one.
- The scanning loop gains a backtracking mechanism that iterates through `yy_acclist[]` and decrements match length to find successive matches.
- **Dynamic buffer resizing is disabled**, since the state buffer must remain synchronized with the character buffer.

The flex manual is explicit: "REJECT is the most expensive feature. If it is used in ANY of the scanner's actions, it slows down ALL of the scanner's matching." It prevents use of fast/full table modes and can cause **quadratic worst-case behavior**. The performance-report flag (`-p`) warns about its presence.

### Trailing context

The trailing context operator `/` (e.g., `abc/def`) matches `abc` only when followed by `def`, returning `def` to the input. **Fixed-length trailing context** (where either the leading or trailing part has a known length) is cheap—just an arithmetic adjustment to `yyleng` after matching. **Variable-length trailing context** (both parts variable) forces flex to use a mechanism nearly identical to REJECT: recording the full state history and backtracking to locate the boundary. The flex manual rates this as "almost the same performance penalty as REJECT." Certain variable-length trailing context patterns produce incorrect results; flex detects some of these and emits "dangerous trailing context" warnings.

---

## 11. Historical performance and why flex wins

Benchmarks from Paxson's original release (Sun 3/60, C tokenizer processing 685K characters across 28,088 lines) illustrate the magnitude of flex's improvements:

| Configuration | Generation time | Object size | Scan time |
|---|---|---|---|
| AT&T lex | 83.0 s | 41.0 KB | 29.8 s |
| flex `-Cem` (default) | 3.9 s | 9.4 KB | 19.3 s |
| flex `-Cfe` | 7.1 s | 49.6 KB | 9.0 s |
| flex `-Cf` (full tables) | 15.0 s | 126.5 KB | 7.8 s |

At the default compression level, flex produces a scanner that is **one-quarter the size** and runs **35% faster**, generated in **one-twentieth the time**. With full tables (`-Cf`), scanning is **3.8× faster** than AT&T lex.

The speedup comes from multiple sources: the tight **12-instruction inner loop**; **block I/O** instead of per-character `getc()`; **equivalence class reduction** of the DFA alphabet before subset construction (making generation itself I/O-bound for uncompressed tables); **hash-based DFA state comparison** during subset construction (avoiding expensive set equality checks); **pointer-based yytext** avoiding per-character copies; and the elimination of the external runtime library's function-call overhead.

---

## 12. Table serialization format

Flex's `--tables-file` option writes DFA tables to an external binary file (magic number **`0xF13C57B1`**) instead of embedding them as C arrays. The scanner loads tables at runtime via `yytables_fload()` and frees them with `yytables_destroy()`. This is useful for applications with multiple scanners used at different times, since compiled-in tables can never be freed.

The file format consists of one or more **table sets**, each containing a header (magic, version string, scanner name, total size) followed by individual table descriptors. Each descriptor carries a type ID (e.g., `YYTD_ID_ACCEPT` = 0x01, `YYTD_ID_BASE` = 0x02, `YYTD_ID_NXT` = 0x08), data-width flags (8/16/32-bit), dimension sizes, and the raw table data. All integers use network byte order. Tables may be serialized in a smaller type (e.g., int8) and expanded at runtime. Multiple scanner table sets can be concatenated in one file, keyed by the scanner prefix (e.g., `yy` or a custom `%option prefix`). The `--tables-verify` flag generates both embedded and serialized tables, with runtime verification that they match.

---

## Conclusion

The lex/flex pipeline is a masterclass in applied automata theory. Thompson's construction provides a clean, linear-size NFA from any regular expression. Subset construction determinizes it at the cost of potential (but rarely realized) exponential blowup. Rather than minimizing the DFA explicitly, flex achieves compactness through a layered compression scheme—equivalence classes, meta-equivalence classes, and comb/row-displacement packing—that reduces table size by an order of magnitude while preserving O(1) amortized transition lookup.

The deepest insight in flex's design is that **equivalence class analysis before DFA construction** simultaneously accelerates generation (fewer transitions to explore) and compresses output (narrower tables). Combined with block I/O, pointer-based token access, and a scanning loop that compiles to a dozen instructions, this yields scanners that process input at near-memory-bandwidth speeds. The principal cost decisions are clear: REJECT and variable-length trailing context impose structural changes that degrade the entire scanner, while the `-Cfe` compression level offers the best practical tradeoff between speed and size for production use.

The theoretical foundation—Thompson (1968), Rabin and Scott (1959), Hopcroft (1971), Johnson (1975), and the Dragon Book's synthesis of these results—remains fully intact in flex's implementation. What Paxson and Jacobson added was the engineering: how to make the theory fast on real hardware, with real I/O systems, for real programming languages.