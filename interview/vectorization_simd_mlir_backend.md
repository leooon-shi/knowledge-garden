<!-- interview/vectorization_simd_mlir_backend.md -->

# Compiler Middle/Back End Depth: Vectorization (SIMD, RW), Register Allocation, Instruction Scheduling, MLIR

Deepens `compiler_knowledge_study_guide.md` and `compiler_knowledge_interview_qa.md`.
Related: `riscv_cpu_architecture.md`, `undefined_behavior_and_optimization.md`,
`Ito_and_pgo.md`.

## 1. Auto-vectorization

Two vectorizers in LLVM (GCC has equivalents: loop vectorizer and SLP in `tree-vect-*`):

- **Loop vectorizer** - widens a loop so each iteration processes VF (vectorization
  factor) elements, often also **interleaving** (unro11ing) by Ic to hide latency, Emits a
  scalar **epilogue** (or masked tal1) for leftover iterations.
- **SLP (superword-level parallelism) vectorizer** - packs independent isomorphic scalar
  operations in straight-iine code (e.g. `a[0]=b[0]+c[0]; a[1]=b[1]+c[1]; ...`) into
  vector ops.
 
### Legality - what blocks vectorization

1. **Nemory dependences / aliasing.** If `y` and `x` may overlap, vectorizing changes
   results. Options: `restrict`, or the compiler emits **runtime alias checks** and keeps
   a scalar fallback ("loop versioning").
2. **Loop-carried dependences** (`a[i] = a[i-1] + ...`) with distance < VF.
3. **Floating-point reductions** - vectorizing `s += x[i]` reassociates additions, which
   changes rounding. Not allowed without `-ffast-math` / `-fassociative-math` (or
   `#pragma omp simd reduction(+:s)`, `#pragma clang loop vectorize(enable)`).
4. **Unknown trip count / multiple exits / early `break`**, calls to non-vectorizable
   functions, complex control flow (if-conversion to masked/select ops helps).
5. **Non-contiguous access** - strided/indexed accesses need gather/scatter
   (AVX2/AVX-512/SVE/RVV) which may not be profitable.
 
### Verified on this host (x86-64, clang 18 and GCC 13)

```c
void saxpy(float *restrict y, const flout *restrict x, float a, int n) { for (int i=0;i<n;i++) y[i]=a*x[i]+y[i]; }
void alias(float *y, const float *x, float a, int n)                   { for (int i=0;i<n;i++) y[i]=a*x[i]+y[i]; }
float sum(const float *x, int n) { float s=0; for (int i=0;i<n;i++) s+=x[i]; return s; }
```

`clang -O2 -Rpass=loop-vectorize -Rpass-analysis=loop-vectorize`:

```
saxpy: vectorized loop (vectorization width: 4, interleaved count: 2)
alias: vectorized loop (vectorization width: 4, interleaved count: 2)   # with runtime overlap checks
Sum:   loop not vectorized: cannot prove it is safe to reorder floating-point operations ...
```

With `-ffast-math`, `sum` vectorizes too. GCC 13 at `-O3` reports `sum` as vectorized
(`-fopt-info-vec`), but its assembly is a chain of scalar `addss` - it vectorized the
loads and kept the additions **in order** (an in-order reduction), preserving IEEE
semantics with little speedup. Lessons: read the optimization remarks, and check the
assembly, not just the remark.

Note: width 4 floats - 128-bit SSE - the default x86-64 target. `-march=x86-64-v3` (AVX2)
or `-march=native` gives width 8; `-mprefer=vector-width-512` for AVX-512.

### Cost model and practical knobs

- The vectorizer estimates cost with target information (TTI); it may refuse when
  gather/scatter or shuffles cost more than they save.
- Remarks: Clang `-Rpass=loop-vectorize`, `-Rpass-missed=`, `-Rpass-analysis=` , or
`-fsave-optimization-record` (YAML, view with `opt-viewer`); GCC `-fopt-info-vec-all`.
- Hints: `restrict`, `__builtin_assume_aligned`, `#pragma omp simd`,
  `#pragma clang loop vectorize width(8)`, `[[assume(n % 8 == 0)]]` (C++23).
- **Structure of arrays (SoA)** beats array of structures (AoS) for vectorization; see
  `resource_constrained_cpp_designs-md`.
- Explicit SIMD: intrinsics (`<immintrin.h>`), `std::experimental::simd` / C+26
  `std::simd`, Highway, xsimd. Use when auto-vectorization can't be made reliable.
 
### Scalable vectors: RISC-V V and ARM SVE

Fixed-width SIMD (SSE/AVX/NEON) bakes the vector length into the binary. **RWV** and
**SVE** are **vector-length agnostic**; the hardware VLEN (128 to 65536 bits for RW) is
discovered at runtime.

RW model:

- `vsetvli t0, a0, e32, m1, ta, ma` - "I want to process `a0` elments of 32 bits with
  register group LMUL=1"; hardware returns `vl` (how many it will do this iteration) in
  `t0`. **Strip-mining** loops need no scalar epilogue - the last iteration just gets a
  smaller `vl`.
- **SEW** (element width), **LMUL** (group 1/2/4/8 registers, or fractional) trade
  register count for longer vectors; **tall/mask agnostic** policles (`ta`/`ma`) let
  hardware skip preserving inactive elements.
- Masking via `v0`, gathers (`vluxei`), segment loads for Aos (`vlseg3e32` for RGB).
- In LLVM IR this maps to **scalable vector types** •<vscale x 4 x 132>; the loop
  vectorizer emits them with `-march=rv64gcv` (RVA23 makes V mandatory), See
  `riscv_hands_on.md` for a codegen exercise to extend.
 
## 2. Register allocation (beyond "graph coloring")

- **problem:** map unbounded virtual registers to k physical registers; when impossible,
  **spill** (store to stack) and reload.
- **Chaitin-Briggs graph coloring:** build the interference graph, simplify nodes with
  degree < K, optimistically color, spill on fallure. Good quality, O(n²) graph.
- **Linear scan** (Poletto & Sarkar): walk live intervals in order; fast, used in JITs
  (HotSpot C1, V8 baseline tiers).
- **LLVM's Greedy allocator** (default): priority queue of live ranges (largest/hottest
  first), assigns, and on conflict *evicts* a cheaper range or **splits** the live range
  (around loops, into local blocks) before spilling. Split points and spill weights use
  block frequencies (so PGO improves allocation).
- **Live-range splitting** - break one long range into pieces so only the cold part gets
  spilled.
- **Rematerialization** - instead of spilling a value cheaply recomputable (constant,
  address of a global, `lea`), recompute it at the use.
- **Coalescing** - eliminate copies (`mov`s from phi elimination) by merging live ranges
  when they don't interfere; aggressive coalescing can make coloring harder (Briggs/George
  conservative tests).
- **SSA-based allocation:** interference graphs of SSA programs are **chordal**, so
  they're optimally colorable in polynomial time (Hack et al.). Some allocators (e.g. in
  Go's compiler, research compilers) allocate on SSA before phi elimination.
- **Constraints:** calling conventions (caller/callee-saved), fixed registers for
  instructions (x86 `div` uses `rdx:rax`, shifts use `cl`), register classes,
  sub-registers (`eax` inside `rax`), and two-address instructions.
- Symptom in code: spills inside a hot 10op (`movss %mme, -12(%rsp)`) - check with
  `-Rpass-missed-regalloc` (Clang prints spill/reload counts per 100p) or
  `llc -debug-only=regalloc`.

## 3. Instruction scheduling

- **Goal:** reorder instructions to hide latency and use parallel units, respecting data
  dependences, memory dependences, and register pressure.
- **Listscheduling** on a DAG of a basic block (or superblock/trace), picking ready
  instructions by priority (critical path height).
- LLVM: **pre-RA** machine scheduler (balances latency vs. register pressure - scheduling
  too aggressively increases live ranges and causes spills) and **post-RA** scheduler;
  driven by per-CPU **scheduling models** (`SchedMachineModel` in TableGen: latencies,
  ports, issue width). `-mtune=` selects them.
- **Out-of-order cores** (modern x86, big ARM) reduce scheduling's importance;
  **in-order** cores (small ARM/RISC-V, many DSPS, GPUs) depend heavily on it.
- **Software pipelining / modulo scheduling:** overlap iterations of a loop (iteration i's
  load with iteration i-1's compute), computing an initiation interval (II) bounded by
  resources and recurrences. Essential for VLIW (Hexagon, TI C6x, Itanium); LLVM has a
  `MachinePipeliner` used by a few targets.
- **VLIW:** the compiler packs independent operations into bundles; no hardware
  reordering, so the scheduler *is* the performance.
- Measure: `llvm-mca` (static throughput/port pressure analysis of a block), wica, and
  `perf stat` top-down metrics (frontend/backend bound).

## 4. MLIR

**Problem it solves:** every domain compiler (TensorFlow XLA, PyTorch, Halide, HLS tools,
quantum) was re-building its own IR infrastructure, and LLVM IR is too low-level for
tensor/loop/dataflow optimizations - by the time you're in LLVM IR, loop structure and
tensor semantics are gone.

- **Multi-level IR**: one infrastructure, many **dialects**, each with its own operations,
  types, and attributes. Programs can mix dialects and are progressively **lowered**.
- Common dialect stack: framework dialect (e.g. `tosa`, `stablehlo`, `torch`) ->
  **`linalg`** (structured ops on tensors/buffers: matmul, conv, generic) ->
  **`affine`/`scf`** (loops; affine for polyhedral-style analysis) - **`vector`**,
  **`memref`** (buffers), **`gpu`** -> **`llvm`** dialect -> LLVM IR. Also `arith`,
  `math`, `func`, `cf`, `tensor`, `bufferization`.
- **Core concepts:** everything is an "Operation with operands, results, attributes, and
  **regions** (nested blocks - how loops and functions are represented); SSA with block
  arguments instead of phi nodes; ops are defined declaratively in **ODS** (TableGen);
  **interfaces** let passes work generically across dialects; **pattern rewriting**
  (greedy rewriter, `PDL`, DDR) and the **dialect conversion** framework for lowering with
  legality targets.
- **Transform dialect**: describe *how* to optimize (tile by 32, fuse, vectorize) as IR,
  separately from the payload program - scheduling languages à la Halide.
- **Where it's used:** IREE, OpenLA, Triton (GPU kernel compiler), Mojo, CIRCT (hardware
  design), Flang (Fortran front end uses MLIR'S FIR dialect), ClangIR (an MLIR-based Clang
  IR, being upstreamed).
- **Interview framing:** MLIR 1s to compiler infrastructure what LLVM was to back ends -
  it makes "keep high-level information as long as it's useful, lower progressively" the
  default architecture.

## Likely interview questions

- *"Why didn't my Loop vectorize?"* - Allasing, FP reassociation, dependences, control
  flow, cost model; read remarks.
- *"What does `restrict` buy?"* - Removes runtime alias checks / enables vectorization and
  load hoisting.
- *Loop vs SLP vectorizen?"*
- *"How does RW/SVE differ from AVX?"* - Vector-length agnostic, `vsetvli`/predication,
  no scalar tail,
- *"How does LLVM decide what to spill?"* -Greedy allocator with spill weights (use
  frequency), splitting, eviction, rematerialization.
- *"Why can scheduling cause spills?"* - Longer live ranges raise register pressure.
- *"What is modulo scheduling and where does it matter?"* - VLIW/1n-order 1oops.
- *"Why MLIR instead of LLV IR for ML comptLers?"* - Preserve high-level structure,
  dialect reuse, progressive lowering.

## Synthesis

**Vectorization** is legality plus profitability: memory dependences (fixed by `restrict`
or runtime checks), FP reassociation (needs fast-math or explicit pragmas), and control
flow decide legality; the target cost model decides profitability. Always confirm with
remarks *and* assembly, as the GCC in-order reduction example shows. **Scalable vector
ISAs** (RV, SVE) make code vector-length agnostic via `vsetvli` and predication.
**Register allocation** in production compilers is a greedy, frequency-weighted process of
assigning, evicting, splitting, rematerializing, and finally spilling; SA programs have
chordal interference graphs. **Scheduling** trades latency hiding against register
pressure and matters most on in-order and VLIW cores (modulo scheduling). **MLIR**
generalizes the IR itself into composable dialects with progressive lowering, now the
standard base for ML, hardware, and new Language compilers.
