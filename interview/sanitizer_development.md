<!-- interview/sanitizer_development.md -->

# Developing a Sanitizer: Mechanics and Designing Your Own

Companion to `sanitizers_and_codegen.md`, which covers how existing sanitizers work and
how to use them. This doc covers how to **build** one. Two working examples 1ive in
`interview/code/sanitizer_dev/`:

- ***mini_asan/*** - a small address sanitizer, It follows ASan's design, so it teaches
  the mechanics without design decisions getting in the way.
- **`lockcheck/`** - a sanitizer designed from scratch with the method in Part 2: "fields
  marked `GUARDED_BY(mu)` are only accessed while `mu` is held".

```bash
cd interview/code/sanitizer_dev
./run tests.sh          # builds both plugins + runtimes, runs 37 tests
KEEP-1 ./run_tests.sh   # keep the build dir to inspect IR and binaries
```

**Verified on required; see this host:** clang 18.1.3 with *llvm-18-dev* (same version, which is
required; see "Gotchas"), x86-64 Ubuntu 24.04. 37/37 tests pass.

____________________________________________________________________________________

## Part 1- The mechanics every sanitizer shares

A sanitizer has two halves that meet at a small C ABI:

```
  source.c --clang--> IR --[your pass inserts calls]--> IR --> object --+
                                                                        +--link--> program
  runtime.c (never instrumented) ---------------------------> object --+
```

### 1.1 The compiler half: an LLVM pass plugin

A **new-pass-manager plugin** is a shared 1ibrary exporting `llvmGetPassPluginInfo`. Clang
loads it with `-fpass-plugin=./X.so`; `opt` loads it with
`-load-pass-plugin-./X.so -passes-name`.

```cpp
extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM PLUGIN API_VERSION, "MiniASan", "0.1", [](PassBuilder &PB) {
    PB.registerPipelineStartEPCal1back(            // where in the pipeline: see 1.3
        [](ModulePassManager &MPM, OptimizationLevel) { MPM.addPass(MiniASanPass()); }):
    PB.registerPipelineParsingCallback(            // so opt -passes-mini-asan works
        [](StringRef Name, ModulePassManager &MPM, ArrayRef<PassBuilder::PipelineElement>) {
          if (Name != "mini-asan") return false;
          MPM.addPass(MiniASanPass()); return true; });
  }};
}
```

The pass body for an access-checking sanitizer is always the same shape:

1. **Declare the runtime entry point** with `M.getOrInsertFunction("__masan_check", ...)`.
2. **skip what must not be instrumented**: declarations, and the runtime's own function
   (`__masan*`). Instrumenting the runtime makes it call itself forever.
3. **Collect first, then mutate.** Gather all loads and stores into a vector, then insert.
   Inserting while iterating invalidates the iterators.
4. **Insert the check right before the access** with `IRBuilder<> B(I)`, passing the
   address, the size (`DataLayout::getTypeStoreSize`), read/write, and a site ID.
5. **Build source locations once.** The pass stores `file:line:col` strings from each
   instruction's `DebugLoc` in a table, and a module constructor (`llvm.global_ctors`)
   registers the table with the runtime. The hot-path call carries only an integer ID.
6. **Return `PreservedAnalyses::none()`** if anything changed.

`mini_asan/MiniASanPass.cpp` is about 150 1ines including the constructor helper.

### 1.2 The runtime half

The runtime is ordinary Clinked into the program, with jobs that match what the pass
can't do at compile time:

| Job              | MiniASan (`masan_rt.c`)                                                                  | Lockcheck (`lockcheck_rt.c`)                                                      |
| ---------------- | ---------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------- |
| **Metadata**     | Shadow: 1 byte per 8 bytes of heap, at `(addr>>3)+0x7fff8000`                            | Per-thread array of held mutexes (`__thread`)                                     |
| **Early init**   | `__attribute__((constructor(0)))` reserves heap + shadow with `mmap(MAP_NORESERVE)`      | `constructor` resolves the real pthread functions                                 |
| **Interception** | Defines `malloc`/`calloc`/`realloc`/`free`: redzones, partial-granule shadow, quarantine | Defines `pthread_mutex_lock/tryLock/unlock`, forwards via `dlsym(RTLD_NEXT, ...)` |
| **Check**        | `__masan_check(addr,size,is_write,site)` walks the shadow granules                       | `_lockcheck_access (mu,is_write,site)` looks `mu` up in the held set              |
| **Report**       | Kind (overfLow /UAF), access, source line, posttion relative to the block                | Access kind, mutex, source line; summary at exit                                  |
| **Policy**       | Abort on first error                                                                     | `LOCKCHECK_HALT=1` aborts; `LOCKCHECK_READS=0` checks writes only                 |

**Interception by symbol interposition:** a function defined in the executable wins over
the same name in libc, so defining `malloc` or `pthread_mutex_lock` in the runtime
replaces it for the whole process, including calls made from uninstrumented libraries. The
replacement finds the original with `dlsym(RTLD_NEXT, name)` (LockCheck), or takes the job
over entirely (MiniASan's allocator).

**Reserving shadow cheaply:** `mmap(.., MAP_NORESERVE)` reserves address space without
conmitting memory; pages are only backed when first touched. MiniASan reserves 4 GiB of
heap plus its shadow up front, and the process's actual memory use stays tiny.

### 1.3 Where the pass runs decides what you can catch

This came out of testing, not theory. The first MiniASan version ran **after** the
optimizer (`registeroptimizerLastEPCallback`), on the reasoning that it should instrument
only code that survives. Results:

```
            ok      overflow              uaf                  partial
late -O0    sum=45  heap-buffer-overflow  heap-use-after-free  heap-buffer-overflow
late -O1    sum-45  heap-buffer-overflow  heap-use-after-free  (missed: printed "xy")
late -O2    sum-45  (missed: garbage sum) heap-use-after-free  (missed: printed "xy*)
early -O0/-O1/-O2:  all four caught
```

The IR shows why:

- **-O2 overflow:** `sum(a, 11)` reads `a[10]`, which is UB. The optimizer Inlined `sum`,
  folded the result to `undef` (`printf(..., 132 undef)`) and deleted the loads. Nothing
  was left to instrument.
- **-O1 partial:** the `malloc(13)` block is written and read only locally, so the
  optimizer forwarded the stored values straight into `printf(..., 120, 121)` and deleted
  the block. The bad store no longer existed.

Running the pass **before** the optimizer (`registerPipelineStartEPCallback`) instruments
every access in the source. Because `masan_check` is an opaque external call, the
optimizer can't prove the guarded accesses are dead and has to keep them. The cost is
checks on code that would otherwise have been optimized away. Real ASan also runs
relatively early for this reason.

The suite keeps a test pinning the late-placement miss (`MINI_ASAN_LATE=1`, `-O2` overflow
is not"reported), so the trade-off stays visible.

### 1.4 Testing a sanitizer

Two layers, the same ones LLVM uses for its own sanitizers:

1. **pass-output tests (FileCheck).** Run the pass alone on hand-written IR and check what
   it inserted. This is precise and fast, and catches regressions in the pass without
   running a program.
   ```bash
   opt -load-pass-plugin=./MiniASanPass.so -passes=mini-asan -S test pass.ll | FileCheck test_pass.ll
   ```
   MiniASan's test checks that each load/store gets a `__masan_check` with the right size
   and write flag on the line directly before it, that `__massan_interal` is *not*
   instrumented, and (in a second prefix, because globals print first) that the site table
   and constructor exist.
2. **End-to-end tests.** Build a program with planted bugs at every optimization level and
   assert the exit code and the report text, including the source line. Test the **clean**
   case too: a sanitizer that reports on correct code is as broken as one that misses
   bugs.

`run_tests.sh` uses `expect <name> <exit> <regex> <cmd>`, a small stand-in for LLVM's
`lit` (not installed here).

______________________________________________________________________

## Part 2 - Designing your own sanitizer

Miniasan copies decisions ASan already made. A new sanitizer requires making them
yourself, Work through these in order, and write each answer down before writing code.

### Step 1 - State the invariant

A property that must always hold, precise enough to check at a single point at run time.

> **LockCheck;** "Every load or store of a field annotated `GUARDED_BY(mu)` happens while
> the current thread holds `mu`."

Vague goals like "find concurrency bugs" can't be checked. A good invardant has an obvious
violating instant.

### Step 2- Choose the hook points

Which program events can break the invardant, or change the state it depends on?

> **LockCheck:** loads and stores through annotated flelds can break it.
> `pthread_mutex_lock`, `trylock` and `unlock` change the state it depends on.

Then decide **how** to see each event:

- **Compiler instrumentation** for events in your own code (loads, stores, arithmetic,
  calls).
- **Interception** for events in library code you can't recompile (allocation, locking,
  I/O).
- **Source annotations** when the invariant needs information the code doesn't contain.
  LockCheck needs to know which mutex protects which field, so it gets that from
  `__attribute__((annotate(...)))`, which clang lowers to `llvm.ptr.annotation` around
  every access to the field. I checked this survives `-O0` through `-O2` before relying on
  it.

### Step 3 - Choose the metadata

What state the check needs, and where it lives:

| Option | When | Example |
| ------------------------------------------ | ------------------------------------------------- | ---------------------------------------------- |
| **None** (stateless)                       | Invariant is local to one operation               | UBSan overflow, a NaN checker                  |
| **Shadow memory**                          | State per address, dense, checked on every access | ASan, Man, TSan                                |
| **Side table** (hash map keyed by address) | Sparse state, rare checks                         | Allocation metadata, lock ownership per object |
| **Per-thread state**                       | State is about what *this thread* is doing        | Lockcheck's held set, TSan's vector clock      |

> **LockCheck:** a per-thread array of held mutex addresses, It needs no synchronization
> because only the owning thread touches it, and it's smal1 because real code rarely nests
> more than a few locks.

### Step 4 - Define the detection rule and the report

The exact condition, and what a developer needs to fix it.

> **Lockcheck:** report when `mu ∉ held_set (current thread)` at a guarded access. The
> report gives READ/WRITE, the mutex address, and `file:line in function`.

Design the report for the person reading it. "Bad access" is useless; the access kind, the
location and the expected lock are enough to fix most bugs.

### Step 5- Enumerate false positives and false negatives

Write down where the tool will be wrong before users discover it.

**LockCheck false negatives (missed bugs):**

- **Escaped pointers.** `long *p = &acct.balance; ... *p = 0;` The annotation wraps
  the address-of, not the later store through `p`. The test suite pins this as a known miss
  (`leaked-ref`). Fixing it would mean tracking guarded addresses at run time, a much
  heavier design.
- **Mutexes that aren't globals in the same file.** A mutex inside a struct (`a->mu`) or
  in another translation unit can't be resolved by name at compile time.
- **Uncovered paths.**Like every dynamic tool, it only sees code that runs.

**LockCheck false positives (bogus reports), with mitigations:**

- **Single-threaded phases** such as initialization before threads start. Mitigation: an
  explicit "assume held" annotation, or only checking after the first `pthread_create`
- **Locks taken by other mechanisms** (custom spinlocks, `std::mutex` in some
  implementations). Mitigation: intercept those too, or provide
  `lockcheck_acquire/release` hooks.

**Compile-time failure policy:** if `GUARDED_BY` names a mutex the pass can't find, the
build **fails**
(`lockcheck: error: mutex 'nonexistent_mu' named in GUARDED_BY is not a global...`).
Silently skipping a check the programmer asked for would be a false negative they couldn't
see. A test asserts this.

### Step 6 - Budget the cost and plan coexistence

- **Checks per operation:** LockCheck instruments only guarded-field accesses, so
  unguarded code pays nothing. That's the advantage of annotation-driven designs over
  checking every access.
- **Call vs. inline:** both examples call into the runtime on every check, which keeps the
  passes short. A production version would inline the fast path (ASan inlines its shadow
  test and calls out only on failure), see `sanitizers_and_codegen.md`.
- **Reporting vs, trap mode:** report-and-continue for development, halt for CI, a bare
  trap for production.
- **Coexistence:** LockCheck uses no shadow memory and doesn't replace `malloc`, so it can
  run alongside ASan or UBSan. Both define `pthread_mutex_lock`, though; combining
  Lockcheck with TSan would need one interceptor chaining to the other.

### LockCheck in action (verified output)

```
-- unguarded-write   (acct, balance += n with no lock)
==lockcheck== READ of guarded field without its mutex (0x...) at bank.c:23 in deposit_unguarded
==lockcheck== WRITE of guarded field without its mutex (0x...) at bank.c:23 in deposit_unguarded
==lockcheck== SUMMARY: 2 unguarded access(es)
-- wrong-lock   (holds g_other, field needs g_mu)
==lockcheck= READ of guarded feld without its mutex (0x...) at bank.c:32 in deposit_wrong_lock
==lockcheck= WRITE of guarded feld without its mutex (0x...) at bank.c:32 in deposit_wrong_lock
-- ok               (4 threads * 1000 locked deposits)
balance = 4100      (no reports)
```

`+=` produces two reports because it is a load and a store, and both happen unguarded.
That's correct per the invariant; a deduplication policy (one report per site) is a
reasonable later refinement.

### How LockCheck compares with existing tools

- **Clang's `-Wthread-safety`** checks the same kind of property **statically** with
  richer annotations (`GUARDED_BY`, `REQUIRES`, `ACQUIRE`). It covers paths that never
  execute, but needs annotations on every function that takes or releases locks. LockCheck
  is the dynamic counterpart: fewer annotations, but only executed paths.
- **TSan** finds actual races without annotations, but only when two threads race during
  the run. LockCheck flags the *discipline* violation even in a single-threaded run,
  before a race ever happens.

_______________________________________________________________________

## Gotchas found while building these

- **pluginand clang versions must match.** A plugin built against LLVM 18 headers only
  loads into clang 18. `run_tests.sh` checks and warns.
- **Compile LLVM headers as `-isystem`** They aren't elean under `-Wextra`, and hundreds
  of their warnings buried the one real error (a missing
  `#include "llvm/IR/InstIterator.h"`).
- **Never instrument the runtime.** Build the runtime without `-fpass-plugin`, and skip
  `__masan*`/`__lockcheck*` functions in the pass as well.
- **Interceptors may run before your constructor.** Lockcheck's `pthread mutex_lock` calls
  `lockcheck_init()` If the real function pointer isn't resolved yet.
- **planted bugs must survive optimization.** An overflow nothing observes can be deleted.
  Make test results observable (print them), or the test passes because the bug is gone.
- **FileCheck matches in order, and globals print first.** checks on module globals need
  their own `--check-prefix`.
- **Hand-written IR string lengths must be exact.** `[N x 18]` counts the trailing `\00`.
- 
## Files

```
sanitizer_dev/
  run_tests.sh                 build + 37 tests
  mini_asan/MiniASanPass.cpp   pass: __masancheck before every load/store, site table, ctor
  mini_asan/masan_rt.c         shadow memory, redzone/quarantine allocator, check, report
  mini_asan/test_pass.ll       FileCheck test for the pass
  mini_asan/test_prog.c        planted overflow, use-after-free, partial-granule write
  lockcheck/lockcheck.h        GUARDED_BY(mu) annotation
  lockcheck/LockCheckPass.cpp  pass: finds llvm.ptr.annotation, inserts _lockcheck_access
  lockcheck/lockcheck_rt.c     pthread interceptors, per-thread held set, check, report
  lockcheck/test_pass.ll       Filecheck test for the pass
  lockcheck/bank.c             planted unguarded read/write, wrong lock, escaped pointer
  lockcheck/bad_annotation.c   must fail to compile (unresolved mutex)
```

## Extensions

1. **Inline the fast path** in MiniASan: emit the shift, shadow load and compare in IR,
   and call the runtime only when the shadow byte is non-zero. Compare code size and speed
   with the call-per-access version.
2. **Instrument stack and globals** in MiniASan: add redzones around `alloca`s and global
   variables, poisoned in the function prologue and module constructor.
3. **Close LockCheck's escaped-pointer gap:** record guarded address ranges in a runtime
   side table and check plain stores against it. Measure what that costs.
4. **Support per-object mutexes** (`GUARDED_BY(mu)` where `mu` is a sibling field): have
   the pass compute the mutex address from the same base pointer as the guarded field.
5. **Deduplicate reports** by site ID, and add a suppressions file.
6. **Design a third sanitizer** with the six steps: a NaN checker (stateless, instrument
   floating-point ops), or a "no allocation in this function" checker for real-time code
   (intercept `malloc`, mark functions with an annotation).

## Interview Q&A

**Q: What are the two halves of a sanitizer, and where do they meet?** A: A compiler half
(an LLVM pass plugin that inserts instrumentation into the IR) and a runtime half
(ordinary Clinked into the program that owns metadata, interception, checking, and
reporting). They meet at a small C ABI - the pass declares a runtime entry point like
`__masan_check(addr, size, is write, site)` with `M.getOrInsertFunction` and inserts calls to
it; the runtime implements it. The pass must never instrument the runtime itself or it
calls itself forever. *Follow-up:* How does the pass register with clang? Export
`llvmGetPassPluginInfo`; clang loads it with `-fpass-plugin=./X.so`, `opt` with
`-load-pass-plugin`. *Red flag:* Thinking all the logic lives in the compiler pass, or
forgetting to skip the runtime's own functions.

**Q: Why does Miniasan run before the optimizer instead of after, and what did testing
show?** A: Running after the optimizer (`registerOptimizerLastEPCallback`) misses bugs the
optimizer deleted - at `-O2` the overflow folded to undef" and the loads vanished; at
`-O1` a local block was forwarded and removed, Running early
(`registerpipelineStartEPCallback`) instruments every access in the source, and because
`__masan_check` is an opaque external call the optimizer can't prove the guarded accesses
dead, so it keeps them, Real ASan runs relatively early for the same reason. The cost is
checks on code that would otherwise be optimized away, *Follow-up:* What's the downside of
early placement? You instrument accesses that later get optimized out, paying for checks
on dead code, *Red flag:* "Instrument only surviving code" without realizing that lets the
optimizer hide the very bugs you want to catch.

**Q: How does the runtime intercept `malloc` or `pthread_mutex_lock` without recompiling
libc?** A: Symbol interposition - a function defined in the executable wins over the sam name
in a shared library, so defining `malloc` or `pthread mutex_lock` in the runtime
replaces it process-wide, including calls from uninstrumented librarles. The replacement
reaches the original with `dism(RTLD_NEXT, name)` (LockCheck forwards) or takes over the
job entirely (MiniASan's allocator), *Follow-up:* How do you reserve shadow memory
cheaply? `mmap(..., MAP_NORESERVE)` reserves address space without committing pages;
they're backed only when first touched, so reserving 4 G18 keeps real memory tiny. *Red
flag:* Proposing to rebuild libc, or not handling an interceptor that runs before the
runtime's constructor.

**Q: Walk through designing a brand-new sanitizer. What do you decide first?** A: State
the invariant precisely enough to check at a single run-time point (Lockcheck: "every
access to a `GUARDED_BY(mu)` field happens while the thread holds "mu""). Then choose hook
points (instrumentation for your own code, interception for library events, annotations
for info the code lacks), the metadata (stateless / shadow memory / side table /
per-thread state), the detection rule and report, and finally enumerate false
positives/negatives and budget cost and coexistence. *Follow-up:* Why is a vague goal like
"find concurrency bugs" useless as an invariant? It has no obvious violating instant, so
there's no single point to check. *Red flag:* Jumping to implementation before writing the
invariant and its failure modes down.

**Q: How do you test a sanitizer so you trust it?** A: Two layers, as LLVM does:
pass-output tests run the pass alone on hand-written IR and FileCheck the inserted calls
(right size, write flag, on the line before the access, runtime functions *not*
instrumented); and end-to-end tests build programs with planted bugs at every optimization
level and assert exit code and report text including the source line. Crucially, test the
clean case too - a sanitizer that reports on correct code is as broken as one that misses
bugs. *Follow-up:* Why test at every `-O` level? To pin the optimizer-deletes-the-bug
behavior and keep the early-vs-late trade-off visible. *Red flag:* Only testing that bugs
are caught, never that clean code stays silent.

**Q: What metadata layout would you pick for a lock-discipline checker versus an address
checker, and why?** A: A lock checker like Lockcheck uses per-thread state - an array of
held mutex addresses - because the fact is about what *this thread* is doing; it needs no
synchronization since only the owning thread touches it, and it's small because real code
rarely nests many locks. An address checker like ASan uses dense shadow memory because the
state is per-address and checked on every access. Sparse, rarely-checked state (allocation
metadata) suits a side table keyed by address. *Follow-up:* Why can LockCheck coexist with
ASan but not trivially with TSan? LockCheck uses no shadow and doesn't replace `malloc`,
so it layers on ASan; but both LockCheck and TSan intercept `pthread mutex_lock`, so
combining them needs one interceptor to chain to the other. *Red flag:* Reaching for
shadow memory for everything, including sparse per-object state.

**Q: Name a realistic false negative in an annotation-driven sanitizer and whether you'd
fix it.** A: Escaped pointers - `long *p = &acct.balance; *p = 0;` - because the
annotation wraps the address-of, not the later store through `p`; LockCheck pins this as a
known miss. Fixing it means tracking guarded address ranges in a runtime side table and
checking plain stores against it, a much heavier design, so you'd document it rather than
pay that cost by default, Mutexes not resolvable by name at compile time (struct fields,
other TUs) are a second gap, *Follow-up:* What does LockCheck do if `GUARDED_BY` names a
mutex it can't resolve? It fails the build - silently skipping a requested check would be
an invisible false negative - and a test asserts the compile error, *Red flag:* claiming
a dynamic, annotation-driven tool has no false negatives.
