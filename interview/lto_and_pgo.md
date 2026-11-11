<!-- interview/1to_and_pgo.md -->

# Link-Time Optimization (LTO) & Profile-Guided Optimization (PGO)

## Shared motivation

Both attack one limitation: **the compiler optimizes with incomplete information.**

- A classic compiler optimizes **one translation unit (TU) at a time**; the linker just
  concatenates objects. It can't inline across files, propagate constants
  interprocedurally across TUs, or devirtualize with whole-program type info. **LTO
  removes the TU boundary (scope).**
- A classic compiler **guesses which paths are hot** via static heuristics (loops hot,
  `else` cold) - often wrong. **GO replaces guesses with measured behavior (accuracy).**

Orthogonal-LTO expands the *scope*, PGO improves the *accuracy* of profitability - and
they compose multiplicatively.

_______________________________________________________________________

## Part 1 - Link-Time Optimization (LTO)

### What it is

Defer (part of) optimtzation + codegen to **link time**, when the whole program is
visible. Compilers emit **IR** (LLVM bitcode, GCC GIMPLE) Into object files; at link, an
LTO-plugin-aware linker hands all IR back to the compiler for **whole-program
optintzation**, then codegen.

```
classic:  TU1->obj1  TU2->obj2  ...  link(obj1,obj2) -> exe        (no cross-TU opt)
LTO:      TU1->IR1   TU2->IR2   ...  link: combine IR -> whole-program optimize -> codegen -> exe
```

### What it enables (interprocedural, across TUs)

- **Cross-module inlining** (biggest win; unlocks downstream opts).
- **Interprocedural constant propagation / dead-argument elimination** across TUs.
- **Whole-program devirtualization** (full class-hierarchy visibility -> direct calls ->
  inline).
- **Whole-program dead code/function elimination** (strips unreachable funcs -> smaller
  binary).
- **Better alias analysis / specialization** with more context.

### The scale problem -> ThinLTO

**Monolithic/full LTO** loads *all* IR into one process as a giant module: huge memory,
long serial link, no incrementality (change one file -> re-optimize everything).

**ThinLTO** (LLVM; GCC's WHOPR is analogous):

- Each TU emits IR *** a compact summary** (call graph, symbol defs/refs).
- A fast serial **thin link** reads only summaries, builds a **combined call-graph
  index**,
- **Per-module backend runs in parallel** cacheable/incremental, importing only needed.
  functions. Near-full-LTO benefit at near-regular-build scalability. The default "LTO"
  for large codebases.

### How to use

```bash
g++ -flto -O2 -c a.cpp -o a.o        # emit IR into the object
g++ -flto -O2 -c b.cpp -o b.o
g++ -flto -O2 a.o b.o-o app          # cross-TU optimize at link
clang++ -flto=thin -O2 ...           # ThinLTO
```

Gotchas: **same compiler (and often version) for compile and link** (IR is not a stable
interchange format); pass `-flto` at compile *and* link; use an LTO-plugin-aware linker
(lld/gold/BFD+plugin). Mixing LTO + non-LTO objects works (non-LTO ones just don't
participate).

### Costs/limits

- Build time & memory at link (full-LTO badly; ThinLTO much less).
- Debuggability - cross-module inlining muddies stack traces/line info.
- IR is toolchain-version-locked (not a stable ABI),
- Diminishing returns if hot code is already in one TU, or across **prebuilt
  shared-11brary** boundaries LTO can't see (LTO works within what's linked, not across
  dynamic , `.so`).

_________________________________________________________________________

## Part 2 - Profile-Guided Optimization (PGO)

### What it is

Feed **actual runtime behavior** back so profitability uses measured data, not static
heuristics, Classic 3-phase **instrument -> train -> optimize**g

```bash
# 1. instrument
clang++ -fprofile-generate -O2 app.cpp -o app_instr
# 2. train on REPRESENTATIVE workloads
/app_instr < typical_input                       # emits .profraw/.gcda
llvm-profdata merge -o app. profdata * profraw
# 3. optimize with the profile
clang++ -fprofile-use=app.profdata -O2 app.cpp -o app
```

(GCC: `-fprofile-generate` -> run -> `-fprofile-use`.)

### What the profile improves

- **Inlining** - inline *hot* call sites, leave cold ones (beats size heuristics).
- **Basic-block / function layout** - hot paths contiguous (fall-through), cold code
  (error handling) pushed out -> better **I-cache + branch prediction**, Often the single
  biggest PGO win.
- **Branch hints** from measured frequencies.
- **Speculative devirtualization** - monomorphic hot indirect call -› guarded direct call
  \+ inline.
- **Register allocation / spill placement** prioritized to hot regions;
- **unroll/vectorize** where it pays.
- **Hot-cold function splitting** into separate sections.

### Instrumentation vs. sampling

- **Instrumented PGO:** precise counts, but slower instrumented build + a separate
  representative run.
- **Sampling/ AutoFDO** (and BOLT/Propeller-style): profile **production** with a
  sampling profiler (perf'), zero instrumentation overhead -> profile the real deployed
  workload continuously; less precise but far lower friction. Modern trend for large
  services.

### Critical caveat: profile representativeness

PO is only as good as its training profile. Unrepresentative inputs or **stale profiles**
(code changed since) can **mis-optimize and hurt** performance. Profiling from production
(AutoFDO) mitigates this - the profile *is* the real workload.

### Post-link optimizers (BOLT, Propeller)

Optimize the **already-linked binary** using profiles -re-lay-out code for I-cache/branch
locality *after* compiler+linker, capturing layout wins unavailable until final addresses
exist. Often applied on top of LTO+PGO for extra datacenter-scale gains.

**BOLT workflow** (LLVM, from Meta; not run on the study host - no `llvm-bolt` package and
`perf_eventparanoid=4`):

```bash
# 1. Link with relocations kept so BOLT can move functions safely
clang -O2 -g app.c-o app -wl,--emit-relocs          # (-fno-pie optional; BOLT handles PIE)
# 2. Profile with LBR (last-branch records) on Intel; AMD uses BRS/IBS; no LBR -> sampling-only, less accurate
perf record -e cycles:u -j any,u -o perf.data -- ./app workload
# 3. Convert the profile
perfbolt -p perf.data -o perf.fdata ./app
# 4. Rewrite the binary
llvm-bolt app -o app.bolt -data=perf.fdata \
  -reorder-blocks=ext-tsp -reorder-functions-cdsort \
  -split-functions -split-all-cold -split-eh -dyno-stats
```

- **what it does:**basic-blockreordering within functions (ext-TSP), function reordering
  across the binary (C³/cdsort - hot callers next to callees), hot/cold splitting into a
  separate `.text.cold`, plus peephole cleanups, ICF, and indirect-call promotion.
  `-dyno-stats` prints before after taken Branch and -ca che-relevant courtsation,
- **why post-link beats compile-time PGO for layout:** it sees the final binary, including
  code from libraries, inlined copies, and linker-placed functions, and profiles the exact
  machine code that runs (no source-to-binary mapping drift). Typical published gains:
  several % to ~20% on large front-end-bound services (HHVM, Clang itself, MySQL), on top
  of PGO+LTO.
- **Pitfalls;** needs `--emit-relocs` (or limited non-relocation mode);
  stripped/hand-written assembly or self-modifying code limits it; profile must come from
  the same binary build ID; BR availability (VMs often hide it); debug info is updated
  but large binaries need lots of RAM during rewrite.
- **Instrumentation mode** (`llvm-bolt -instrument`) when perf/BR isn't available, at the
  cost of a slow training run.
- **Propeller** (Google) does similar layout work but feeds it back into the **linker**
  (basic-block sections, `-fbasic-block-sections-list=`), which scales better for huge
  binaries and integrates with distributed builds, AutoFDO + ThinLTO + Propeller is
  Google's production stack; PGO + LTO + BOLT is common elsewhere (also used for bullding
  Clang, Python, and Rust compilers).

**Relevant flags summary:** Clang IR PGO `-fprofile-generate`/`-fprofile-use=`;
context-sensitive PGO `-fcs-profile-generate` (second instrumented round after inlining);
AutorDo `-fprofile-sample-use=prof.afdo` (from "`perf` via `create_llvm_prof`): GCC
`-fprofile-generate` / `-fprofile-use`, `-fauto-profile`. Don't confuse with coverage
(`--coverage`/`-fprofile-arcs`,`-fprofile-instr-generate -fcoverage-mapping`).

___________________________________________________________________

## Part 3 - Combining LTO + PGO

Complementary; the standard max-performance recipe (browsers, DBs, datacenter services):

- **LTO** gives whole-program **scope**; **GO** gives accurate **hotness**, Together the
  inliner makes *cross-module* inlining decisions driven by *measured* hot call sites -
  better than either alone.
- Pipeline: PGO-instrument -> train -> final build with **both `-fprofile-use` and
  `-flto`** (often ThinLTO) -> optionally **BOLT**.
- PGO also makes LTO *cheaper/better*: the profile tells LTO where to spend its expensive
  cross-module inlining budget.

________________________________________________________________________

## Staff / Principal-level framing

- **LTO fixes the *scope* gap (per-TU -> whole-program); PGO fixes the *accuracy* gap
  (heuristics -> measurement).** Orthogonal; combining is multiplicative.
- **Dominant wins are code layout (PGO) + cross-module inlining (LTO)** - both about
  **I-cache and branch-prediction efficiency**, where modern-CPU front-end stalls dominate
  large binaries (ties to the earlier cache/locality discussions: optimize code locality
  to match the hardware bottleneck).
- **ThinLTo is the engineering unlock** - summary-based, parallel, incremental makes
  whole-program optimization practical at million-line scale (same "summary + combined
  index" pattern as scalable interprocedural analysis).
- **PGO's Achilles* heel is representativeness/staleness** - needs a profile-collection
  pipeline, representative workloads, and a refresh story;
  AutoFDo/sampling-from-production is the mature answer.
- **Costs are build complexity/time, not runtime** - multi-stage builds, long/parallel
  links, toolchain-version coupling, debuggability impact. Adopt for release builds where
  the payoff (often 5-20%+ each, more combined, plus LTO binary-size reduction) justifies
  pipeline complexity - not for fast dev iteration.
- **Correctness vs, profitability still holds** - LTO/PGO change scope and profitability,
  never legality; a profile can't make an i11egal transformation legal, only guide which
  legal ones to apply.

## Interview Q&A

**Q: LTO and GO both address "incomplete information." What exactly does each fix?** A:
LTo fixes the *scope* gap - a classic compiler optimizes one translation unit at a time
and the linker just concatenates objects, so LTO emits IR into objects and defers
whole-program optimization to link time (cross-Tu inlining, interprocedural const-prop,
whole-program devirtualization and dead-code elimination). PGO fixes the *accuracy* gap -
static heuristics guess which paths are hot (loops hot, `else` cold) and are often wrong.
so PGO feeds measured runtime behavior back. They're orthogonal and compose
multiplicatively: LTO scope x PGO accuracy. *Follow-up:* Does either change what's legal?
No - both change scope and profitability, never legality; a profile can't make an illegal
transformation legal. *Red flag:* Describing them as alternatives, or claiming PGO enables
new (otherwise illegal) transformations.

**Q: Why doesn't monolithic LTO scale, and how does ThinLTO fix it?** A: Full LTO Loads
*all* IR into one process as a giant module - huge memory, a long serial link, and no
incrementality (change one file, re-optimize everything). ThinLTO has each TU emit IR plus
a compact summary (call graph, symbol defs/refs); a fast serial thin link reads only the
summaries, builds a combined call-graph index, and decides cross-module imports without
loading all IR; then per-module backends run in parallel, cacheable and incremental. You
get near-full-LTO benefit at near-regular-build scalability. *Follow-up:* What's GCC's
analogue? WHOPR - the same summary-plus-partitioning idea. *Red flag:* Treating ThinLTO as
"weaker LTO" rather than the scalability engineering that makes whole-program optimization
practical.

**Q: Give the three-phase PGO workflow with flags, and name the single biggest win.** A:
Instrument (`clang++ -fprofile-generate -O2`), train on representative workloads (produces
`.profraw`, merged with `llvm-profdata merge`), then optimize
(`clang++ -fprofile-use-app-profdata -O2`). The biggest win is usually basic-block and
function layout - hot paths made contiguous for fall-through, cold code (error handling)
pushed out - which improves I-cache utilization and branch prediction. Profile-driven
inlining, speculative devirtualization, and hot-focused register allocation follow.
*Follow-up:* GCC equivalents? `-fprofile-generate` → run + `-fprofile-use`. *Red flag:*
Naming inlining as the dominant win and forgetting code layout, where front-end stalls on
large binaries actually dominate.

**Q: What's PGO's main risk, and how does AutoFDo mitigate it?** A: Representativeness and
staleness - PO is only as good as its training profile, and unrepresentative inputs or a
profile taken before code changed can *mis-optimize and hurt* performance.
AutoFDo/sampling profiles the real production workload with a sampling profiler ( `perf`,
`-fprofile-sample-use=`), so the profile *is* the deployed workload and there's zero
instrumentation overhead; it's less precise but far lower friction, which is why it's the
modern trend for large services. *Follow-up:* Instrumented vs. sampling trade-off in one
line? Instrumented gives precise counts but needs a slower instrumented build and a
separate representative run; sampling is lower-precision but profiles production
continuously. *Red flag;* Assuming any profile helps - a stale or skewed profile can be
worse than static heuristics.

**Q: What does a post-link optimizer like BOLT do that compile-time PGO can't, and what
does it need?** A: BOLT rewrites the already-linked binary, re-laying-out basic blocks
(ext-TSP) and functions (cdsort) and splitting hot/cold into separate sections - it sees
the final machine code including 11brary code, inlined copies, and linker-placed
functions, with no source-to-binary mapping drift. It needs the binary linked with
`-Wl,--emit-relocs` so functions can be moved safely, and a profile from the same build
ID, ideally with LBR (`perf record -j any,u`); typical gains are several % to ~20% on
front-end-bound services on top of PGO+LTO. *Follow-up:* How does Propeller differ? It
feeds the layout back into the *linker* via basic-block sections
(`-fbasic-block-sections=`), scaling better for huge binaries and distributed builds. *Red
flag:* Expecting BOLT to work on a stripped binary with no relocations, or using a profile
from a different build.

**Q: A colleague turns on `-flto` at compile but links without it and sees no cross-TU
inlining. What went wrong, and what else bites people?** A: `-flto` must be passed at
*both* complle and link - comple emits IR into the objects, and the link step is where
whole-program optimization and final codegen happen; omitting it at link just
concatenates. Other gotchas: use the same compiler and often the same version for compile
and link (IR is not a stable interchange format), and use an LTO-plugin-aware linker
(lld/gold/BFD+plugin). Mixing LTO and non-LTO objects works - the non-LTo ones simply
don't participate. *Follow-up:* Why diminishing returns sometimes? If the hot code already
lives in one TU, or the win would be across prebuilt shared-11brary boundarles LTO can't
see. *Red flag:* Mixing compiler versions for compile vs. link and expecting the IR to be
portable.

**Q: Why is combining LTO + PGO more than the sum, and what's the pipeline?** A: LTO gives
whole-program scope and PGO gives measured hotness, so together the inliner makes
*cross-module* inlining decisions driven by *measured* hot call sites - better than either
alone - and PGO even makes LTO cheaper by telling it where to spend its expensive
cross-module inlining budget. Pipeline: PGO-instrument train final build with both
`-fprofile-use` and `-flto` (often ThinLTO) → optionally BOLT. The cost is build
complexity and time, not runtime, so it's for release builds, not dev iteration.
*Follow-up:* Where do these wins physically come from? Code locality for modern CPU
front-ends -I-cache and branch-prediction efficiency, where front-end stalls dominate
large binaries. *Red flag:* Enabling the full stack for fast dev-iteration builds where
the build-time cost isn't justified.

## Synthesis

**L**and **PGO** both address the compiler'sincomplete-information problem from
orthogonal angles. **LO** defers optimization/codegen to link time by emitting **IR**
into objects, so the optimizer sees the **whole program** and does cross-TU **inlining,
interprocedural const-prop, whole-program devirtualization, and dead-code elimination**;
monolithic LTO doesn't scale, so **ThinLTO** (summaries + combined call-graph index +
parallel incremental backend) makes it practical. **GO** feeds **measured runtime
behavior** (instrument-train-optimize, or lower-friction **sampling/AutoFDO** from
production) to drive **hot/cold code layout** (biggest win - I-cache/branch locality),
**profile-driven inlining**, **speculative devirtualization**, and hot-focused
allocation/unrolling - with the key caveat that an **unrepresentative/stale profile can
hurt**. They **combine** as the standard max-performance recipe (LTO scope x PGO accuracy,
often + **BOLT** post-link layout). Principal view: these optimize **code locality for
modern CPU front-ends**, ThinLTO is the scalability unlock, profile representativeness is
PO's main risk, the price is build complexity/time (not runtime), and both change
scope/profitability while preserving semantics.
