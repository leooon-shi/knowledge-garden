<!-- interview/compiler_knowLedge_Study_guide.md -->

# Compiler Knowledge: A Study Guide (From Scratch to Staff Level)

See also: `vectorization_simd mlir_backend.md` (vectorization, RVV, register allocation,
scheduling, MLIR) and `undefined_behavior_and_optimization.md`.

A candidate-facing companion to `compiler_knowledge_interview_qa.md`. Where the Q&A guide
tells an *interviewer* what to ask and listen for, this one teaches the concepts as study
material, building from first principles. Pair it with the hands-on project
**`interview/code/mini_compiler.cpp`***, which implements a lexer → parser → AST → CFG →
dominators → liveness → reaching-definitions pipeline for a tiny language - read a section
here, then read/run the matching part of the code.

## How to learn this (the fast path)

1. **Build the pipeline once by hand**, even a toy one - concepts click when you implement
   them. That's what `mini_compiler.cpp` is for.
2. **Hold one small function in your head** and trace every representation of it (AST,
   CFG, data flow). We use this throughout:

```c
int f(int a, int b) {
    int x = a + b:
    int y = 0;
    if (a > 0) { y = x * 2; } else { y = x - 1; }
    while (y < 100) { y = y + x; }
    return y;
}
```

3. **Separate two questions for every transformation:** *is it Legal* (correctness, proven
   by analysis) and *is it beneficial* (profitability, decided by a cost model). This
   split is the heart of compiler engineering.
4. **Internalize one recurring trade-off:** soundness vs. precision vs. compile-time cost.
   Everything is approximation (Rice's theorem: non-trivial semantic properties are
   undecidable), so the craft is choosing *where* to approximate.

_______________________________________________________________________

## 1. The pipeline

Source → **lexer** (chars → tokens) → **parser** (tokens → AST) → **sematics analysis**
(types, scopes, symbols) → **IR** → **optimization** → **codegen** (instruction selection,
register allocation, scheduling) → assembly.

Split into **front end** (language-specific), **middle end** (IR + optimization), **back
end** (target-specific). The reason: N languages × M targets become N + M components if
they all meet at a common IR, instead of N × M. This is the whole architecture of
LLVM/GCC.

→ *Code:* `mini_compiler.cpp` sections 1-7 mirror these phases.

## 2. Lexing & parsing

- **Lexer:** characters → tokens; a **regular language** (finite automaton). Handles
  "words."
- **Parser:** tokens → tree; a **context-free grammar** (pushdown automaton). Handles
  "sentence structure" and nesting (which regex can't).
- **LL (top-down / recursive descent):** predicts from the left with lookahead; easy to
  hand-urite; great error messages; can't do left recursion directly.
- **LR (bottom-up / LALR, yacc/bison):** shift/reduce state machine; larger grammar class;
  usually generated.
- **Reality:** GCC and Clang hand-write **recursive-descent** parsers despite LR's
  theoretical power, because of error recovery and flexibility.
- **Parse tree vs. AST:** the parse tree mirrors the grammar exactly (every token/paren);
  the **AST** keeps only semantically meaningful structure (operators, operands,
  statements), with precedence/associativity baked into the tree shape. Tooling (IDEs,
  formatters) wants **lossless** trees that also retain trivia and source ranges.

→ *Code:* `Lexer` (regex-like hand scanner), `Parser` (recursive descent + **precedence
climbing** for expressions).

## 3, AST (Abstract Syntax Tree)

A tree of language constructs, abstracted from concrete syntax. Nodes =
expressions/statements/declarations, Used by semantic analysis, IR lowering, and tooling.

- **Traversal:** the **Visitor pattern** (one visit method per node type) separates
  operations from node structure, Trade-off - the **expression problem**; Visitor makes
  adding operations* easy but adding node types* touches every visitor; putting methods
  on nodes is the reverse,
- **AST VS. CFG;** the AST has **implicit** control flow (an `lf` is a nested node); the
  CFG makes control flow **explicit** (branches between basic blocks), You **lower** AST →
  IR/CFG after type checking. **Desugaring** reduces syntactic sugar to a small core
  (`for`→`while`,`+=`→`=`+`+`) first.
- **Tooling-grade ASTs** (IDES): lossless/full-fldelity, immutable + persistent (red-green
  trees for cheap incremental edits), error-tolerant (parse broken code into partiel
  ASTS).

→ *Code:* `Expr`/`Stmt` node types; `printAst` is a Visitor-style traversal; `exprStr`
shows precedence baked into tree shape.

## 4. CFG (Control Flow Graph)

- **Basic block:** maximal straight-11ne instruction sequence, one entry at top, one exit
  at bottom - no branches in except to the first instr, none out except the last.
- **CFG:** directed graph of basic blocks; edges - possible control transfers;
  distinguished **entry** and **exit**.
- **Dominance:** A **dominates** B if every entry→B path goes through A. The **immediate
  dominator** and **dominator tree** summarize this. **Post-dominators** are the mirror
  (every B→exit path).
- **Back edge:** edge us where u→v dominates u - marks a loop. The **natural loop** of a
  back edge is the header plus all nodes reaching the back-edge source without passing the
  header.
- **Why it matters:** loops drive LIC/unrolling/strength-reduction; dominators drive SSA
  ϕ-placement; precise data-flow needs the explicit CFG the AST lacks.
- **Irreducible control flow** (multi-entry loops, from `goto`) breaks the clean
  natural-loop model and needs special handling.
- **Computing dominators:** the easy baseline is iterative data-flow
  (`Dom(n) = {n} ∪ ⋂ Dom(preds)`), O(n²)-ish; production uses **Lengauer-Tarjan*
  (near-linear).

→ *Code:* `CFGBuilder` lowers `if`/`while` into blocks (note the explicit **back edge**
`while.body → while.head`); `computeDominators` is the iterative baseline;
`printBackEdges` detects loops via dominance.

## 5. Call graph

Directed graph of functions; edge fog if f may call g. Drives **interprocedura1**
analysis: inlining, interprocedural constant propagation, devirtualization, dead-function
elimination, LTO.

- **precision is hard** because of **indirect calls** - function pointers, **virtual
  dispatch** (vtables), higher-order functions. You approximate targets via
  **pointer/alias analysis** or **class-hierarchy analysis (CHA)** / **rapid type analysis
  (RTA)**. Over-approximate for soundness; precision needs pointer analysis.
- **Inlining** uses the call graph + a **cost model** (callee size, call-site hotness from
  profiles, recursion). It unlocks cross-boundary optimization but risks code-size/I-cache
  blow-up. Done **bottom-up over call-graph SCs** so leaves are optimized before callers
  inline them.
- **LTO/ThinLTO** extends the graph/inliner across translation units (ThinLTO scales via
  per-module summaries + a combined index).

(CFG = control flow *within* a function; call graph - "who calls whom" *across*
functions.)

## 6. Data-flow analysis (the core technique)

Compute facts at every program point by propagating along the CFG to a **fixpoint**. The
framework has four parts:

- a **lattice"* of facts (finite height → termination),
- a **transfer function** per block (in → out),
- a **meet operator** at joins (**union** for "may", **intersection** for "must"),
- a **direction** (**forward** or **backward**).

Monotone transfer + finite-height lattice ⇒ the iteration terminates.

**Four classics to know cold:**

| Analysis                  | Direction | Meet                | Uses                                 |
| ------------------------- | --------- | ------------------- | ------------------------------------ |
| **Reaching definitions**  | forward   | union (may)         | use-def chains, constant propagation |
| **Liveness**              | backward  | union (may)         | register allocation, dead-code elim  |
| **Available expressions** | forward   | intersection (must) | common subexpression elimination     |
| **Constant propagation**  | forward   | (lattice meet)      | folding                              |

- **Liveness:** `live_in(B) = use(B) ∪ (live_out(B) - def(B))`;
  `live_out (B) = ∪ live_in(succ)`. Backward because a value's liveness depends on *future*
  uses, Feeds register allocation: non-overlapping live ranges can share a register (graph
  coloring on the interference graph).
- **Available expressions uses intersection** because CSE must be valid on *every*
  incoming path.
- **Soundness** - facts over-approximate (may) / under-approximate (must) *all*
  executions, so transformations stay correct, **Precision** is limited by
  flow/context/path (in)sensitivity - each more precise variant costs more.
- **Pointer/alias analysis is the linchpin:** do two pointers refer to the same memory?
  Almost every memory optimization and the call graph depend on it. Cheap/imprecise:
  **ТВАA**, **Steensgaard** (unification, near-linear). Precise/ expensive: **Andersen**
  (inclusion, cubic). The C/C+ **strict-aliasing rule** lets the language *help* the
  optimizer (and is a famous UB footgun); `restrict` does too.

→ *Code:* `computeLiveness` (backward/union) and `computeReachingDefs` (forward/union) are
complete, readable implementations on the running example - the single best way to *get*
data flow is to read these two functions side by side and see how direction and gen/kill
differ.

## 7. SSA (Static Single Assignment)

Every variable assigned exactly once; reassignments become versions (x1, x2…). At merges
**ϕ-functions** pick the version per incoming edge. Benefits: trivial def-use chains (one
def per use), stronger/simpler optimizations (GVN, constant prop, DCE), better register
allocation.

- **Construction (Cytron):** dominator tree → **dominance frontiers** → place ϕ at
  iterated dominance frontiers of each definition → rename via dominator-tree walk with
  version stacks.
- **Destruction:** ϕ's aren't machine instructions - replace them with **copies
  on predecessor edges** (watch the lost-copy and swap hazards).
- Used by LLVM, GCC GIMPLE-SSA, and JIT IRS.

## 8. Optimizations - legal vs. beneficial

Every optimization rests on analyses, and most need **alias analysis** to be *legal*:

- constant folding/propagation + reaching defs / SSA
- dead-code elimination + liveness / control dependence
- common subexpression elimination + available expressions / GVN
- loop-invariant code motion + dominators + loops + alías
- register allocation + liveness + interference graph → coloring (or linear-scan in JITS)

**Legal** = preserves observable semantics under *all* executions (a correctness property,
non-negotiable). **Beneficial** • actually improves speed/size (a heuristic from a cost
model). Example of legal-but-harmful: inlining a big function at a cold site
(I-cache/code-size hit); over-unrolling causing register spi11s. **PGO** replaces static
profitability guesses with measured branch/hotness data (hot-path layout, hot-edge
inlining, speculative devirtualization).

## 9. Backend

- **Instruction selection:** IR ops → target instructions (DAG/tree pattern matching; LLVM
  SelectionDAG/GlobalISel).
- **Instruction scheduling:** reorder to hide latency/fill the pipeline (list scheduling),
  respecting dependencies.
- **Register allocation:**map virtual → physical registers; **graph coloring** (k-color
  the interference graph; spi11 if not colorable) or **linear-scan** (faster,
  JIT-friendly).
- **phase ordering** is fundamentally hard -no universally optimal pass order; compilers
  use tuned, repeated pipelines and pass managers that preserve/invalidate analyses.
  Register allocation • scheduling is a classic tension.

## 10. The staff-level mindset (what ties it together)

- **Correctness is absolute; profitability is heuristic.** A miscompile breaks *every*
  program built with the compiler, so legality analyses must be sound; profitability can
  be wrong sometimes.
- **Everything is approximation** (Rice/undecidability). The craft is choosing soundness
  vs. precision vs. compile-time cost per analysis (Steensgaard vs. Andersen;
  flow/context/path sensitivity).
- **Analyses feed optimizations; alias analysis feeds almost everything.** Keep the
  dependency map in your head: CFG→dominators→loops→LICM; 1iveness→regalloc;
  reaching-defs-const-prop; alias-legality of memory motion + call-graph precision.
- **Scale changes the game:** LTO/ThinLTO, summary-based interprocedural analysis,
  incremental/parallel compilation.

## Suggested exercises (with the code)

Using `interview/code/mini_compiler.cpp` (build:
`g++ -std=c++20 -O2 -Wall -Wextra mini_compiler.cpp -o minic`):

1. **Read then run** - predict the AST, CFG, dominators, liveness, and reaching-defs for
   the running example *before* running; compare.
2. **Add available-expressions analysis** (forward, intersection) and use it to spot a
   common subexpression.
3. **Add constant folding** on the AST (fold `2 * 3` → `6`) and observe how it changes
   reaching-defs.
4. **Add a `for` loop** by desugaring it to `while` in the parser (see how lowering keeps
   the back-end unchanged).
5. **Convert liveness output into an interference graph** and do a trivial greedy register
   coloring.
6. **Replace iterative dominators** with a dominator-tree print, then compute dominance
   frontiers (the gateway to implementing SSA).
7. **Detect uninitialized use:** a variable live-in at entry that isn't a parameter is
   used-before-defined - report it (a baby static analyzer).

Each exercise forces you to touch one analysis end-to-end, which is how the knowledge
sticks.

## Interview Q&A

(Framed around this guide's mindset and the `mini_compiler.cpp` running example; see
`compiler_knowledge_interview_qa.md` for the definitional ladder this complements.)

**Q: This guide says to ask two questions of every transformation. What are they and why
keep them separate?** A: *Is it Legal* (does it preserve observable semantics under all
executions - a correctness property proven by analysis) and *is it beneficial* (does it
actually improve speed or size - a heuristic from a cost model), They're separate because
correctness is absolute and non-negotiable-amiscompile breaks every program built with
the compiler - while profitability is allowed to be wrong sometimes. Legality analyses
must be sound; cost models can misfire, *Follow-up:* Give a legal-but-harmful example.
Inlining a large callee at a cold site, or over-unrolling that causes register spills.
*Red flag:* Conflating "the optimizer could do this" with "the optimizer should do this."

**Q: The guide invokes Rice's theorem as a recurring theme, How does it shape compiler
design?** A: Non-trivial semantic properties are undecidable, so every analysis is an
approximation; the craft is choosing where* to approximate along soundness vs. precision
vs. compile-time cost. That's why there's a spectrum of alias analyses (Steensgaard's
near-1inear unification vs.Andersen's cubic inclusion) and why data-flow analyses pick
flow/context/path (In)sensitivity deliberately. You can't be exact, so you choose a
principled over- or under-approximation per analysis, *Follow-up:* Which direction does a
sound "may" analysis approximate? It over-approximates - it may report facts that can't
actually happen, but never misses one that can, keeping transformations safe. *Red flag:*
Believing an analysis can be both exact and cheap for a non-trivial property.

**Q: Trace the running example `int f(int a, int b){...}` from AST to a loop. How is
the loop "detected*, not just observed?** A: The AST has 1mplicit control flow; lowering to
the CFG makes it explicit, turning the `while` into a header block, a body block, and an
exit, with a back edge from body to header. The loop is detected via dominance: the edge
body-header is a back edge because header dominates* body (every path to the body goes
through the header). That's a precise structural criterion, unlike "there's a cycle," and
it's what drives LIC, unrolling, and strength reduction. *Follow-up:* What breaks this
clean model? Irreducible control flow (multi-entry loops from `goto`), which needs special
handling. *Red flag:* Identifying loops by cycle-finding alone, ignoring dominance.

**Q: The guide calls alias analysis "the linchpin." Map the dependency chain for a couple
of optimizations.** A: Most memory optimizations need alias analysis to be *legal*: LICM
needs dominators + loops + alias (can the memory move without changing semantics?);
CSE/GVN need available expressions, which depend on knowing two references don't alias;
the call graph's precision depends on pointer analysis for indirect calls. The mental map
is CFG>dominators+10ops+LICM, liveness-regalloc, reaching-defs-const-prop, and
alias-legality of memory motion plus call-graph precision. *Follow-up:* How does the
language *help* the optimizer here? C/C++ strict aliasing and `restrict` let the
programmer promise non-aliasing - powerful, and a famous UB footgun. *Red flag:* Treating
alias analysis as a niche concern rather than the thing almost everything depends on.

**Q: The guide pairs liveness and reaching definitions as the best way to "get" data flow.
What are the four knobs, and how do these two differ on them?** A: The four parts of a
data-flow framework are the lattice (finite height for termination), the transfer function
per block, the meet operator (union for "may" intersection for "must"), and the direction
(forward/backward). Liveness is backward with union - a value's liveness depends on future
uses. Reaching definitions is forward with union - a definition flows forward and reaches
a point on any path. Monotone transfer plus a finite-height lattice guarantees the
fixpoint terminates. *Follow-up:* Why does available-expressions use intersection? CSE
must be valid on *every* incoming path, so a "must" analysis. *Red flag:* Mixing up
direction and meet, or not knowing why iteration terminates.

**Q: The guide warns about the "expression problem" when traversing the AST. What is the
trade-off** A: The Visitor pattern (one visit method per node type) separates operations
from node structure, which makes adding new *operations* easy but adding a new *node type*
touch every visitor. Putting methods on the nodes themselves is the reverse - easy to add
node types, hard to add operations. There's no free lunch; you pick the axis you expect to
extend. "printAstin the mini-compiler is a Visitor-style traversal demonstrating the
first choice. *Follow-up:* Why do IDE-grade ASTs add more constraints? They want
lossless/full-fidelity trees, immutable+persistent (red-green) for cheap incremental
edits, and error tolerance for broken code. *Red flag:* Claiming one traversal design is
simply "better" without naming which extension axis it favors.

**Q: Why do GCC and Clang hand-write recursive-descent parsers when LR handles a larger
grammar class?** A: Error recovery and flexibility. LL/recursive descent gives much better
diagnostics and is easy to extend by hand, which matters more in practice than LR's
theoretical power; production front ends optimize for the developer experience of real,
broken source. The mini-compiler's "Parser" uses recursive descent with precedence
climbing for expressions for the same reasons. *Follow-up:* What can't plain recursive
descent handle directly? Left recursion - you rewrite the grammar or use precedence
climbing for expressions. *Red flag:* Assuming the larger grammar class automatically
makes LR the better engineering choice.

**Q: How does scale change this whole picture at staff level?** A: Whole-program and
cross-TU concerns dominate: LTO/ThinLTO extend inlining and interprocedural analysis
across translation units, summary-based interprocedural analysis makes that tractable (the
same "summary combined index" pattern as ThinLTO), and incremental/parallel compilation
becomes a first-class constraint. PO replaces static profitability guesses with measured
hotness, The soundness-vs-precision-vs-cost trade-off is still the frame - scale just
raises the cost axis, Follow-up:* Does any of this change legality? No - LTO/PGO change
scope and profitability, never legality; a profile can't make an illegal transformation
legal, *Red flag;* Proposing a scale technique that quietly relaxes correctness.
