<!-- interview/compiler_knowledge_interview_qa.md -->

# Compiler Knowledge Interview Guide (Q&A Reference)

A reference for **conducting** interviews on compiler knowledge, from basics to
staff/principal level. Covers the full pipeline: lexing/parsing, **AST**, semantic
analysis, IR, **CFG**, **Call Graph**, **data-flow analysis**, SSA, optimization, and
codegen/backend.

## How to use this guide (for the interviewer)

- **Calibrate by level.** Start at L1-L2 for all candidates; use the signal to decide how
  deep to push. Don't start at L4 - you learn more watching a candidate climb than
  watching them fail cold.
- **Each question lists:** the question, a **model answer** (what good looks like),
  **follow-ups** to probe depth, and **red flags / green flags**,
- **Favor reasoning over recall.** Compiler trivia is googleable; the signal is whether
  they can *reason* about transformations, correctness, and trade-offs. Ask "why" and
  "what breaks if…".
- **Use one running example** across sections (below) so the candidate builds a mental
  model you can probe from multiple angles.
- **Levels:** L1 Basic (any SWE), L2 Intermediate (worked near compilers/tooling), L3
  Advanced (compiler engineer), L4 Staff/Principal (designs compiler subsystems, reasons
  about soundness/scale).

# Running example (reuse throughout)

```c
int f(int a, int b) {
    int x = a + b;
    int y = 0;
    if (a > 0) {
        y = x * 2;
    } else {
        y = x - 1;
    }
    while (y < 100) {
        у = y + x;
    }
    return y:
}
```

Ask the candidate to draw its AST, CFG, and reason about data flow on *this* function -
concrete beats abstract.

_________________________________________________________________________

## Section 1- The compiler pipeline (orientation)

### Q1.1 (L1) Walk me through the phases of a compiler.

**Model answer:** Source → **lexer/scanner»* (characters → tokens) → **parser** (tokens →
**AST**, per the grammar) → **semantic analysis** (type checking, name resolution,
scope/symbol tables) → **IR generation** (AST → intermediate representation) →
**machine-independent optimization** (on IR) → **code generation** (IR → target assembly,
instruction selection, register allocation, scheduling) → **machine-dependent
optimization** → assembly/linking. Often split into **front end** (language-specific:
lex/parse/sema), **middle end** (IR + optimization), **back end** (target-specific
codegen). **Follow-ups:** Why separate front/middle/back ends? (retargetability + multiple
languages → one IR → many targets, the "nanopass"/LLVM model). Where does the symbol table
live and persist? **Green flags:** mentions the front/middle/back-end split and *why* (N
languages × M targets → N+M not NxM). **Red flags:** conflates parsing with compilation;
thinks optimization happens on source text.

### Q1.2 (L2) What is an IR and why not optimize the AST directly?

**Model answer:** An **intermediate representation** is a lower-level, more uniform
program form between source and machine code. We don't optimize the AST because it's too
close to source syntax (nested, language-specific, implicit control flow), making analyses
awkward, IR (three-address code, LLVM IR, etc.) makes control flow **explicit** (basic
blocks + branches), is **language- and target-independent** (one optimizer serves many
front/back ends), and normalizes constructs so analyses are simpler. Multiple IR Levels
are common (high-level AST-ish → mid-level SSA→ low-Level near-machine). **Follow-ups:**
Trade-offs of a high-level vs, Low-level IR? Why might you keep "several" IR Levels (e.g.,
MLIR dialects, Swift's SIL, Rust's MIR/HIR)? **Green flags: ** "explicit control flow +
retargetability + normalization," **Red flags:** thinks IR is Just "bytecode" with no
analysis rationale.

_________________________________________________________________________

# Section 2 - Lexing & Parsing

### Q2.1 (L1) Difference between a lexer and a parser; what grammar class does each handle?

**Model answer:** The *lexer** turns a character stream into tokens using **regular
languages** (finite automata / regex). The **parser** turns tokens into a parse tree/AST
using **context-free grammars** (pushdown automata). Lexing handles the "words"; parsing
handles the "sentence structure." **Follow-ups:** Why is lexing separated out at all?
(performance + simpler grammar). Give something a regular language *can't* express that a
CFG can (balanced parentheses / nesting).

### Q2.2 (L2) LL vS. LR parsing; recursive descent vs. table-driven.

**Model answer:****LL** (top-down, e.g.,recursive descent) predicts productions from the
left using k lookahead tokens (LL(k)); easy to hand-write, good errors, but can't handle
left recursion directly. **LR** (bottom-up, e.g., LALR as in yacc/bison) builds the parse
by shifting/reducing with a state machine; handles a larger grammar class (incl. left
recursion),typically table-generated. Most production compilers (GCC, Clang) hand-write
**recursive-descent** parsers for better error messages and flexibility despite the theory
favoring LR. **Follow-ups:** Why do real compilers favor hand-written recursive descent
over generated LR? (error recovery, context-sensitivity hacks, maintainability). What's
the dangling-else ambiguity and how is it resolved? **Green flags:** knows the theory
*  and* that practice (Clang/GCC) diverges from it, with reasons. **Red flags:** only knows
"yacc exists."

### Q2.3 (L2-L3) Parse tree vs. AST - what's dropped and why?

**Model answer:** A **parse tree (concrete syntax tree)** mirrors the grammar exactly,
including every token and intermediate production (parentheses, punctuation, single-child
chains). An **AST (abstract syntax tree)** keeps only semantically meaningful structure -
operators, operands, statements - discarding redundant syntax (parens become tree shape,
keywords/semicolons dropped). The AST is what later phases traverse. **Follow-ups:** Where
do operator precedence/associativity end up in the AST? Should the AST retain source
locations/trivia - when and why? (yes, for diagnostics, refactoring tools, formatters -
hence "lossless" syntax trees in IDE tooling like Roslyn/clang's).

______________________________________________________________________

## Section 3 - AST (Abstract Syntax Tree)

### Q3.1 (L1) What is an AST and what is it used for?

**Model answer:** A tree representation of source structure where nodes are language
constructs (expressions, statements, declarations) and edges are the containment/structure
relationships, abstracted away from concrete syntax. Used for semantic analysis (type
checking, name resolution), IR generation, and by tools (linters, formatters, refactoring,
static analyzers, the `code` intelligence tools). **Follow-ups:** Draw the AST for the
running example. What node types appear? (FunctionDecl, BinaryOp(+), Vardecl, IfStmt,
WhileStmt, Return).

### Q3.2 (L2) How do you traverse and transform an AST? What design pattern?

**Model answer:** The **Visitor pattern** is canonical - separate the traversal/operation
from the node structure, with a visit method per node type (double dispatch). Variants: a
mutating visitor (rewrites), a read-only visitor (analysis), pre-order vs. post-order
traversal depending on whether context flows down or results flow up. Some compilers use a
generic "walker" + pattern matching (functional style with ADTs) instead. **Follow-ups:**
Why Visitor over putting methods on nodes? (open/closed: add operations without touching
node classes; but adding node types then touches every visitor - the expression-problem
trade-off). How do you handle AST rewriting safely (immutability vs. in-place)? **Green
flags:** names the **expression problem** trade-off. (Ties to AST-based tooling like
ast-grep / `code` pattern search.)

### Q3.3 (L2-L3) What's the difference between an AST and an IR/CFG? When do you lower from one to the other?

**Model answer:** The AST is **syntax-oriented** - control flow is *implicit* in nested
constructs (if/while are nodes). The **CFG/IR** makes control flow **explicit** as basic
blocks and edges. **Lowering** AST → IR happens after semantic analysis: each high-level
construct is expanded into IR (an `if` becomes conditional branches to blocks; a `while`
becomes a loop header/body/latch with back-edge). You lower when you need explicit
control/data flow for optimization and codegen. **Follow-ups:** Why can't you do precise
data-flow analysis on the AST easily? (implicit, non-uniform control flow; need the CFG).
What is "desugaring"? (lowering syntactic sugar - `for` → `while`, `+=` → `=`+`+` - to a
smaller core before/while lowering).

## Q3.4 (L3-L4) How would you design an AST for a language with rich tooling needs (IDE, refactoring, incremental compilation)?

**Model answer:** Needs beyond compilation: **lossless/full-fidelity** syntax trees that
retain trivia (whitespace, comments) and exact source ranges (for formatters/refactoring);
**immutable, persistent** trees with structural sharing for cheap incremental edits
(red-green trees la Roslyn); **error-tolerant** parsing producing partial ASTs with
error nodes (IDEs parse broken code constantly); stable **node identity** for incremental
re-analysis. Separate the "concrete" lossless tree from an "abstract" semantic view.
**Green flags:** mentions red-green/persistent trees, error recovery, incrementality -
signals real tooling experience, **This is a strong staff-level signal.**

_______________________________________________________________________

## Section 4 - Control Flow Graph (CFG)

### Q4.1 (L1-L2) What is a CFG and what is a basic block?

**Model answer:** A **basic block** 1s a maximal straight-line sequence of instructions
with **one entry (at the top) and one exit (at the bottom)** - no branches in except to
the first instruction, no branches out except the last. A **CFG** is a directed graph
whose nodes are basic blocks and whose edges represent possible control transfers
(branches, fall-through). It has a distinguished **entry** and **exit** block.
**Follow-ups:** Draw the CFG for the running example (entry → add/init block →
if-condition → then/else blocks → merge → while-header → body with back-edge → exit). What
ends a basic block? (a branch, a call that may not return, a label that's a branch
target).

### Q4.2 (L2) What is a back edge, and how do you identify a loop in a CFG?

**Model answer:** A **back edge** is an edge whose target **dominates** its source (points
back to an already-visited ancestor in a DFS / to a loop header). It indicates a loop. A
**natural loop** of a back edge nah is (the header) plus all nodes that can reach n
without going through h. Finding loops requires **dominator** information. **Follow-ups:**
Define **dominance**: block A *dominates* B if every path from entry to B goes through A.
What's the **immediate dominator** and the **dominator tree**? Why do optimizations need
loop structure? (hoisting invariants, unrolling, strength reduction). **Green flags:**
connects back edges → dominators → natural loops cleanly. **Red flags:** *a loop is a
cycle in the graph" with no dominance notion (irreducible control flow breaks that).

### Q4.3 (L3) What are dominators and post-dominators, and name two optimizations that need them.

**Model answer:** **A dom B** if every entry+B path passes through A; **A post-dom B** if
every Brexit path passes through A. Uses: **dominator tree** drives SSA ϕ-placement
(dominance frontier), **LIC** (hoist to the loop pre-header, which dominates the 1oop),
**code motion / PRE**, identifying natural loops, and **control-dependence** (built from
post-dominators) for dead-code elimination and for program dependence graphs.
**Follow-ups:** What's a **dominance frontier** and why does SSA need it? What's
**irreducible control flow** and why is it a problem? (multiple-entry loops - from `goto`
or some optimizations - break the clean natural-loop model; may require node splitting).

### Q4.4 (L3-L4) How do you compute dominators efficiently?

**Model answer:** Classic iterative data-flow (intersect predecessors' dom sets to a
fixpoint) is simple but 0(n*)-ish; the **Lengauer-Tarjan** algorithm computes the
dominator tree in near-linear time (0(E•a(E,N))) and is what production compilers use. For
dominance *queries* at scale, precompute the dom tree and use DFS in/out numbering for
O(1) ancestor checks. **Green flags:** names Lengauer-Tarjan and knows iterative is the
easy-but-slower baseline. Staff signal.

_______________________________________________________________________

## Section 5 - Call Graph

### Q5.1 (L1-L2) What is a call graph and what is it used for?

**Nodel answer:** A directed graph whose nodes are functions and whose edges represent
"may call" relationships (f→g if f may call g). Used for **interprocedural analysis and
optimization**: inlining decisions, interprocedural constant propagation,
devirtualization, dead-function elimination, computing SCs for bottom-up summary-based
analysis, and whole-program optimization (LTO). **Follow-ups:** How is it different from a
CFG? (CFG intra-procedural control flow within one function; call graph =
inter-procedural "who calls whom"). What do cycles in the call graph mean? (recursion
/ mutual recursion - SCCs).

### Q5.2 (L2-L3) Why is building a *precise* call graph hard?

**Model answer:** **Indirect calls** make it hard: function pointers (C), **virtual
calls** (C++/Java tables), higher-order functions, and dynamic dispatch mean the callee
isn't statically obvious. You must approximate the set of possible targets via **points-to
/ class-hierarchy analysis**. Conservative over-approximation every indirect call may hit
any address-taken function of matching type) is sound but imprecise; precision needs
pointer analysis. Separate compilation (callee in another TU) also limits the graph
without LTO. **Follow-ups:* How does **devirtualization** work and what enables it?
(type/class-hierarchy analysis, speculative + guard, profile data, "final/sealed). What's
**CHA (Class Hierarchy Analysis)** vS. **RTA (Rapid Type Analysis**? **Green flags:**
connects call-graph precision to **pointer/alias analysis** - that's the crux. **Red
flags:** assumes all calls are direct.

### Q5.3 (L3-L4) How do inlining decisions use the call graph, and what are the trade-offs?

**Model answer:** Inlining substitutes a callee's body at the call site, enabling further
optimization (constant propagation across the boundary, dead-code elim, better register
allocation) and removing call overhead. Decisions use the call graph + a **cost model**:
callee size, call-site hotness (profile data), recursion (bounded for self-recursive),
whether it unlocks other opts. **Trade-offs:** code-size blow-up (I-cache pressure),
compile-time cost, diminishing returns; over-inlining can *hurt* performance. Order
matters- usually **bottom-up over the call graph SCs** so leaves are optimized before
their callers inline them. **Follow-ups:** How does **LTO** change what the call
graph/inliner can do? (cross-TU inlining and IPA, at the cost of memory/compile time -
ThinLTO addresses scale with per-module summaries+ a combined index). How does PGO feed
the inliner? **Green flags:** bottom-up SCC traversal, cost model, I-cache/code-size
trade-off, ThinLTO, Strong staff signal.

_______________________________________________________________________

## Section 6 - Data-Flow Analysis

### Q6.1 (L2) What is data-flow analysis? Give the general framework.

**Model answer:** A technique to compute facts about a program at each point by
propagating information along the CFG to a **fixpoint**, Framed as: a **lattice** of
facts, a **transfer function** per block (how it transforms facts in-out), a **meet
operator** (how facts combine at join points - - intersection or union depending), and a
**direction** (forward or backward), Iterate until no facts change monotone framework →
guaranteed termination on a finite-height lattice), Classic examples: reaching
definitions,liveness, available expressions, constant propagation. **Follow-ups:**
Forward vs. backward - give one of each (reaching definitions = forward; liveness =
backward). May vs. must (union vs. intersection meet) - give examples (avallable
expressions = must/intersection; reaching defs - may/union). **Green flags:** articulates
the lattice + transfer + meet + direction framework abstractly, That's the real
understanding.

### Q6.2 (L2) Explain **liveness analysis** and one use.

**Model answer:** A variable is **live** at a point 1f its current value may be used along
some path before being redefined. It's a **backward, may (union)** analysis:
`live in(B) = use(B) U (live_out(B) - def(B))`, `live_out(B) = U live_in(successors)`.
Primary use: **register allocation** (two variables can share a register if their live
ranges don't overlap → graph coloring on the interference graph); also dead-code
elimination (a def whose result is never live is dead). **Follow-ups:** Walk liveness on
the running example for `x` and `y`. Why backward? How does it feed graph-coloring
register allocation and spilling?

### Q6.3 ( l2-l3) Explain “ reaching definitions and available expressions and contrast the meet operators 

**Nodel answer:** **Reaching definitions** (forward, may/union): which definitions of a
variable may reach a point without being overwritten - used for use-def chains, constant
propagation. **Available expressions** (forward, must/intersection): which expressions
have been computed on *aLL*paths and not invalidated since - used for **common
subexpression elimination (CSE)**. The meet differs because "may reach on some path"
(union) vs. "available on all paths" (intersection) encode different safety requirements.
**Follow-ups:** Why must available-expressions use intersection? (CSE must be valid on
*every* incoming path, else you'd reuse a value not computed). What invalidates an
available expression? (a redefinition of an operand).

### Q6.4 (L3) What guarantees data-flow analysis **terminates**, and what makes it **sound/precise**?

**Model answer:** Termination: a **finite-height lattice** + **monotone transfer
functions** → the iterative fixpoint can only move facts in one direction a bounded number
of times (ascending/descending chain condition). **Soundness** = the computed facts
over-approximate (for may) or under-approximate (for must) *all* real executions - never
claim something true that could be false in a way that breaks a transformation.
**precision** how tight the approximation is; limited by path-insensitivity (merging at
joins loses correlations), context-insensitivity, and the lattice granularity. There's a
fundamental **soundness vs» precision vs. cost** trade-off (and Rice's theorem means exact
answers are undecidable in general). **Follow-ups:* Flow-sensitive vs. flow-insensitive;
context-sensitive vs. -insensitive; path-sensitive - define and give the cost/precision
trade-off. What is a **distributive** framework and why does it matter (MOP = MFP)?
**Green flags:** cites monotonicity + finite height for termination,
over/under-approximation for soundness, and Rice/undecidability for why it's all
approximation. **Strong signal.**

### Q6.5 (L3-L4) What is **pointer/alias analysis** and why is it the linchpin?

**Model answer:** Alias analysis determines whether two pointers *may/must* refer to the
same memory. It's foundational because almost every other analysis/optimization depends on
it: can I reorder these loads/stores? is this store dead? can I hoist this? what are an
indirect call's targets? Approaches range from cheap/imprecise (**type-based alias
analysis / TAA**, **Steensgaard** - unification-based, near-linear, flow-insensitive) to
precise/expensive (**Andersen** - inclusion-based, cubic; context/flow-sensitive
variants). It's the classic **precision vs. scalability** battleground, and undecidable
precisely in general. **Follow-ups:** Steensgaard vs. Andersen trade-off (speed vs.
precision). What is **BAA** and the C/C++ **strict-aliasing** rule - how does the
language *help* the optimizer, and what's the UB hazard? How does `restrict`/`__restrict`
help? **Green flags:** connects alias analysis to the strict-aliasing rule and to
call-graph/devirtualization precision - ties the whole picture together. **Top-tier staff
signal.**

________________________________________________________________________

## Section 7 - SSA (Static Single Assignment)

### 07.1 (L2-L3) What is SSA and why is it useful?

**Model answer:** **Static Single Assignment** is an IR property where every variable
is assigned **exactly once**; multiple assignments become distinct versioned names (x1, x2…).
At control-flow merges, **ϕ-functions** select the right version based on the incoming
edge. Benefits: makes **def-use chains explicit and trivial** (each use has exactly one
def), simplifies and strengthens many optimizations (constant propagation, GVN, dead-code
elim, register allocation), and reduces spurious dependencies. Used by LLVM, GCC
(GIMPLE-SSA), JVM/JIT IRS. **Follow-ups:** What is a **ϕ-function** operationally (it's
not real machine code - it's resolved during "SSA destruction" by inserting copies on
predecessor edges)? Where are 's placed? (at **dominance frontiers** of definitions -
minimal SSA). **Green flags:** -placement via dominance frontier; SSA destruction via
edge copies (and the "lost copy" /"swap" problems). **Red flags:** thinks ф-functions
execute at runtime as-is.

### Q7.2 (L3-L4) How is SSA constructed and destructed?

**Model answer:** **Construction (Cytron et al.):** compute the dominator tree and
dominance frontiers; for each variable, insert 4-functions at the iterated dominance
frontier of its definition blocks; then rename variables via a dominator-tree traversal
with a per-variable version stack, **Destruction:** ф's can't run on hardware, so replace
them by inserting **copy instructions on the predecessor edges** (careful of parallel-copy
semantics - the lost-copy and swap problems require correct sequencing / an edge split).
Modern approaches use the "virtualized" out-of-SSA with coalescing. **Green flags:** names
Cytron/dominance-frontier construction and the lost-copy/swap hazards in destruction.
**Strong staff signal.**

_______________________________________________________________________

## Section 8 - Optimizations (tie the analyses together)

### Q8.1 (L2) Name optimizations and the analysis each depends on.

**Mode1 answer (sampling):** **Constant folding/propagation** (reaching defs / SSA),
**dead-code elimination** (Liveness / control dependence), **common subexpression
elimination** (available expressions / GVN), **loop-invariant code motion** (dominators +
loop structure + alias analysis), **strength reduction** (loop induction variables),
**inlining** (call graph + cost model), **register allocation** (liveness → interference
graph → coloring), **loop unrolling/vectorization** (dependence analysis). The theme:
every optímization rests on one or more analyses, and most need **alias analysis** to be
legal. **Follow-ups:** For any one, ask "what makes it *Legal* vs. merely *profitable*?" -
legality is a correctness (analysis-driven) question; profitability is a
heuristic/cost-model question. Great candidates separate these cleanly.

# Q8.2 (L3) What's the difference between a *Legal* and a *beneficial* transformation? Give an example where an optimization is legal but harmful.

**Model answer:** **Legal** - preserves program semantics (observable behavior) under all
executions - a correctness property proven via analysis (e.g., alias info says the reorder
is safe). **Beneficial** = actually improves the metric (speed/size) - a heuristic
judgment from a cost model + possibly profile data. Example: **inlining** a large function
at a cold call site is legal but harms I-cache/code size; **loop unrolling** can be legal
but increase code size and register pressure, causing spills that slow things down;
aggressive **vectorization** of a short loop may lose to scalar due to setup overhead. The
compiler must get legality always* right and profitability *usually* right. **Green
flags:** crisp legality-vs-profitability distinction; knows that correctness is
non-negotiable while profitability is heuristic. **core staff-level mental mode1.**

### Q8.3 (L3-L4) How does profile-guided optimization (PGO) change the picture?

**Model answer:** PGO feeds **runtime profile data** (branch frequencies, call-site
hotness, value profiles) back into the compiler so profitability decisions use *measured*
behavior instead of static heuristics: better inlining (inline hot edges), block layout
(hot path straight-lined, cold code split out for I-cache), devirtualization of
monomorphic hot indirect calls (speculative+ guard), register allocation priorities.
Workflow: instrument → collect → recompile; or sampling-based (AutoFDO). Pairs with LTO
for whole-program scope. **Follow-ups:** Risks? (profile unrepresentative of production →
mis-optimized; staleness). How does sampling (AutoFDO) trade accuracy for
zero-instrumentation overhead?

_______________________________________________________________________

# Section 9 - Backend: codegen, register allocation, scheduling

### Q9.1 (L2-L3) What are the main back-end phases?

**Nodel answer:** **Instruction selection** (IR ops → target instructions, e.g., tree/DAG
pattern matching, BURG-style, or LLV's SelectionDAG/GlobalISel), **instruction
scheduling** (reorder to hide latency / fill pipeline, respecting dependencies - list
scheduling), and **register allocation** (map unbounded virtual registers to finite
physical ones; spill to stack when needed). Order and interaction (scheduling vs.
allocation phase-ordering) matter. **Follow-ups:** Why is register allocation modeled as
**graph coloring**? (interference graph from liveness; k-coloring for k registers; spill
when not colorable). What's **linear-scan** allocation and why do JITs prefer it? (faster,
near-linear, good enough - compile-time vs. code-quality trade-off).

### Q9.2 (L3-L4) The phase-ordering problem - what is it and how do compilers cope?

**Nodel answer:** Optimizations/analyses interact: the best order isn't fixed, applying A
may enable or disable B, and there's no universally optimal sequence (it's effectively
intractable). Compilers cope with a **fixed, tuned pass pipeline** (with some passes run
multiple times - e.g., repeated simplify/CSE), **pass managers** that schedule and
preserve/invalidate analyses, and iteration to fixpoint for cheap simplifications.
Research explores autotuning/ML for pass ordering. The register-allocation ↔ scheduling
tension (do either first and you can hurt the other) is a classic instance. **Green
flags:** names phase ordering as fundamentally hard, cites analysis
preservation/invalidation in a pass manager, and the regalloc/scheduling tension. **Strong
staff signal.**

_____________________________________________________________________

## Section 10 - Staff/Principal synthesis questions (open-ended)

## Q10.1 Design a static analyzer to find use-after-free (or null-deref) in a large C codebase. What analyses do you need and what are the trade-offs?

**what to listen for:** needs a **CFG** per function, a **call graph** for interprocedural
flow, **pointer/alias analysis** (the crux), **data-flow** tracking pointer state
(allocated/freed/null) as a lattice, **context- and path-sensitivity** decisions
(precision vs, scale), handling of loops (widening/fixpoint), **soundness vs.
completeness** trade-off (false positives vs, false negatives - a *sound* tool
over-reports, a *complete* one may miss), summary-based interprocedural analysis for
scale, and incremental analysis for CI, Bonus: separating "must-free" from "may-free," and
the undecidability (Rice) framing that forces approximation. **This question exercises the
whole guide at once.**

# Q10,2 You must make an existing optimizing compiler ~20% faster to "compile* (not run) without losing much code quality. How do you approach it?

**What to listen for:** profile the compiler itself; Identify expensive passes (often
alias analysis, inlining, register allocation); consider cheaper analysis variants
(Steensgaard over Andersen), capping iteration/inlining thresholds, caching/preserving
analyses across passes (pass-manager invalidation discipline), parallelizing
per-function/per-module work (ThinLTO model), Jazy/on-demand analysis, and measling
code-quality regression with a benchmark suite. Signal: treats it as an engineering
trade-off problem with measurement, not guesswork.

### Q10.3 How would you add a new mid-level IR / optimization to a production compiler safely?

**What to listen for:** verifier/invariants for the IR, extensive differential testing
(compile + run, compare before/after; **translation validation**), fuzzing (Csmith-style),
staged rollout behind flags, preserving debuggability (source locations, DWARF), measuring
compile-time and run-time deltas, and reasoning about **soundness** of the transformation
(prove it preserves observable semantics incl. UB rules). Mentions that **correctness bugs
in a compiler are catastrophic** (miscompile every program) so verification rigor
dominates.

______________________________________________________________________

## Interviewer scoring rubric

| Level | Signal                                                                                                                                                                                                                                                          |
| ---------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **L1 Basic**           | Knows the pipeline phases, what AST/CFG/ca11-graph *are*, lexer vs. parser. |
| **L2 Intermediate**    | Can draw AST+CFG for code, explain IR rationale, name common analyses (liveness/reaching defs) and their direction/meet, basic optimizations. |
| **L3 Advanced**        | Dominators/SSA/-placement, data-flow framework (lattice/transfer/meet/termination), alias analysis trade-offs, call-graph precision, legality vs. profitability. |
| **L4 Staff/Principal** | Reasons about soundness vs. precision vs. cost; phase ordering; Lengauer-Tarjan/Cytron/ Steensgaard-Andersen by name and trade-off; LTO/PGO/ThinLTO at scale; designs sound analyses; treats miscompiles as catastrophic and verification as first-class. |

### Cross-cutting green flags (any level, scaled)

- Separates *correctness/legality** from **profitability/heuristics** - the single most
  important compiler mindset.
- Grounds answers in the **running example** concretely.
- Invokes **soundness vs, precision vs. compile-time cost**as the recurring trade-off,
  and **Rice's theorem / undecidability** as why everything is approximation.
- Connects analyses to the optimizations that consume them (CFG→loops→LICM;
  liveness→regalloc; aliass→everything) .

### Cross-cutting red flags

- Thinks optimizations run on source text or the AST directly.
- Assumes all calls are direct / ignores aliasing.
- Can't distinguish "may" vs. "must" analyses or forward vs. backward.
- Treats profitability as correctness (or vice versa).
- Believes a static analysis can be both sound and complete in general (contradicts
  undecidability).

## Appendix - quick concept map

```
Source -> Lexer -> tokens -> Parser -> AST -> Sema(types, scopes) -> IR
IR --Lower--> CFG (basic blocks + edges)
   dominators/post-dominators-> natural loops, SSA phi-placement (dominance frontier)
   data-flow (lattice transfer + meet + direction) - liveness / reaching defs / available exprs/ const-prop
   alias/pointer analysis-legality of memory optimizations+ call-graph precision
Call graph (SCCS) --> interprocedural analysis, inlining (bottom-up), devintualization, LTO/ThinkTO
Optimizations: legal (analysis-proven) vs, beneficial (cost model + PGO)
Backend: instruction selection -> scheduling <-> register allocation (graph coloring / linear scan)
Recurring axis: soundness vs. precision vs. compile-time cost; phase ordering; undecidability (Rice)
```
