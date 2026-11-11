<!-- interview/sanitizers_and_codegen.md -->

# Sanitizers and Their Codegen

See also: `.fuzzing_and_hardening.md` (libFuzzer/SanitizerCoverage, kernel sanitizers, CFI and hardening flags) and .undefined_behavior_and_optimization.md°.

## What a sanitizer is

A sanitizer is **compiler-inserted instrumentation plus a runtime library** that turns a
silent bug into an immediate, reported failure. The compiler rewrites your code (extra
checks around loads, stores, arithmetic, function entry); the runtime owns the bookkeeping
(shadow memory, allocator, thread clocks) and prints the report.

That split explains almost everything about sanitizers:

- **They are dynamic.** They only catch bugs on paths you actually execute. No test
  reaching the bug means no report. Coverage is the limit, not the tool.
- **They work on the code that survives optimization.** If the optimizer deletes the buggy
  code, there's nothing left to check (the lab shows this happen).
- **The cost is in the codegen.** Every check is real instructions on the hot path. How
  expensive a sanitizer is depends on how many operations it has to instrument and what
  each check does.

## The family

| Sanitizer                      | Flag                                                           | Catches                                                                                                             | Mechanism                                                         | Typical cost               |
| ------------------------------ | -------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------- | -------------------------- |
| **ASan** (Address)             | `-fsanitize=address`                                           | heap/stack/global buffer overflow, use-after-free, use-after-return, double free                                    | shadow memory + redzones + quarantining allocator                 | ~2x time, ~2-3x memory     |
| **UBSan** (Undefined Behavior) | `-fsanitize=undefined`                                         | signed overflow, shift out of range, misaligned/null deref, invalid enum/bool, bad casts, out-of-bounds array index | inline checks at each risky operation                                 | small, check-dependent     |
| **TSan** (Thread)              | `-fsanitize=thread`                                            | data races, some lock-order inversions                                                                              | every shared access instrumented; vector clocks + per-word shadow | ~5-15x time, ~5-10x memory |
| **MSan** (Memory)              | `-fsanitize=memory` (clang only)                               | use of uninitialized memory                                                                                        | bit-precise shadow tracking "is this initialized"                  | ~3x time |
| **LSan** (Leak)                | on by default with ASan on Linux; `-fsanitize=leak` standalone | memory leaks | conservative reachability scan at exit | near zero until exit |
| **HMASan**                     | `-fsanitize=hwaddress` (AArch64)                               | like ASan | pointer tagging (top-byte-ignore) instead of redzones | lower memory than ASan


**Compatibility:** ASan, TSan and MSan each take over the allocator and shadow memory, so
they are **mutually exclusive** - one per build. USan combines with any of them
(`-fsanitize=address,undefined` is the standard pairing).

## How each one works (and what it emits)

### ASan: shadow memory

ASan reserves a **shadow region** where **1 shadow byte describes 8 application bytes**:

- `0` → all 8 bytes addressable
- `k` (1-7) → only the first `k` bytes addressable
- negative values → poisoned (redzone, freed, stack-after-return, and so on)

The mapping is a shift and an add: `shadow = (addr >> 3) + offset`. On x86-64 Linux the
offset is `0x7fff8000`. Before **every** load and store the compiler inserts:

```asm
movq   %rdi, %rax
shrg   $3, %rax                  # addr > 3
movzbl 2147450880(%rax), %eax    # load shadow byte at +0x7fff8000
testb  %al, %al
jne    slowpath                  # non-zero: maybe bad
mov1   (%rdi), %eax              # the original load
...
slow path:                       # partial granule: is (addr & 7) + size - 1 < k ?
callq  __asan_report_load4       # no: report and abort
```

The allocator places **redzones** (poisoned shadow) around every heap block; the compiler
does the same for stack frames and globals. `free()` poisons the block and holds it in a
**quarantine** instead of handing it out again, which is why use-after-free is caught
rather than silently reading reused memory.

### UBSan: inline checks at the risky operation

For signed `a + b`, plain codegen is one `lea`. UBSan uses the CPU's overflow flag:

```asm
addl %ess, %eax
jo   .Lhandler     # overflow flag set?
retq
.Lhandler:
leaq Ldescriptor(rip), %rdi # static (flle, line, column, type)
callq __ubsan_handle_add_overflow
```

The fast path costs one branch. The descriptor is static data, so the report can name the
file, line and types without any runtime cost on the fast path.

**Trap mode** (`-fsanitize-trap=...`) replaces the handler call with an undefined
instruction (`ud2`, or `ud1` in recent clang) that raises SIGILL: no runtime library, no
strings, smallest code. This is how UBSan checks ship in hardened production builds and
kernels.

**The hidden cost:** a check can block optimizations. In the lab, an overflow-checked
`s += a[i]` loop can no longer be **vectorized**, because the check needs the exact
intermediate sum after every add. The trap build had smaller* code and sti11 ran ~2x
slower.

### TSan: instrument every shared access

The compiler turns each load/store of potentially shared memory into a runtime call
(`__tsan_read4`, `__tsan_write4`) and adds `__tsan_func_entry/exit` for stack traces. The
runtine keeps **vector clock per threads** and **shadow cells per 8 bytes** recording
recent accesses. Two accesses race if they touch the same location, at least one writes,
and neither **happens-before** the other according to the clocks. Mutexes, atomics and
thread create/join update the clocks.

Function calls on every access are why TSan is the most expensive sanitizer.

### MSan: shadow bits for "initialized"

Every bit of memory has a shadow bit meaning "uninitialized". The compiler propagates
shadow through loads, stores, copies and arithmetic, and **reports only when uninitialized
data changes behavior**: a conditional branch, a syscall argument, a pointer dereference.
Copying garbage around is allowed. The catch: **every** library in the process, including
the C++ standard library, must be built with Man. Otherwise writes by uninstrumented code
don't update shadow and you get false positives.

### LSan: a GC mark phase at exit

At exit LSan stops the process and does a **conservative reachability scan** from roots
(globals, thread stacks, registers, TS), treating anything that looks like a pointer as
one. Allocations nobody points at are reported as **direct leaks**, and blocks reachable
only through leaked blocks as **indirect leaks**. Still-reachable memory, such as a global
holding a buffer, is not a leak.

## Using them well

**Build flags:**

```bash
clang -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined app.c -o app
```

- `-g` and `-fno-omit-frame-pointer` give readable, accurate stack traces.
- `-O1` is the usual compromise: fast enough for tests while keeping most bugs in the
  binary. Higher levels can delete dead buggy code; `-O0` is slowest but most faithful.
- Have `llvm-symbolizer` in PATH so reports show file:line instead of raw addresses.

**Runtime options** (environment variables):

```bash
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1 ./app
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 ./app
TSAN_OPTIONS=second_deadlock_stack=1 ./app
```

By default UBSan prints and keeps going. CT usually wants `halt_on error=1`, or
`-fno-sanitize-recover-all` at compile time, so the first UB fails the test.

**Suppressions** for known third-party issues: `ASAN_OPTIONS=suppressions-asan.supp`,
`TSAN_OPTIONS=suppressions=tsan.supp`;`__attribute__((no_sanitize("address")))` on a
single function.

**Where they belong:**

- **Unit and integration tests in CI**: ASan+UBSan on every commit; TSan on a separate job
  for concurrent code.
- **Fuzzing**: libFuzzer/AFL plus ASan is the main memory-bug finder in industry. The
  fuzzer generates inputs, the sanitizer turns silent corruption into a crash the fuzzer
  can minimize.
- **Production**: only cheap modes, UBSan trap mode, hardened allocators, sampling tools
  like GWP-ASan, HWASan on AArcha64, Full ASan on TSan in production is rare.

## Sanitizers vs, other tools

- **Valgrind (memcheck)** needs no recompilation (dynamic binary translation) but is
  ~20-50x slower and can't see stack/global overflows the way Asan's compile-time redzones
  can, ASan is faster and finds more; Valgrind works on binarles you can't rebulld.
- **Static analysis** (clang-tidy, the clang static analyzer) can reason about paths no
  test executes, but trades that coverage for false positives. Dynamic sanitizers have
  essentially no false positives (Man's uninstrumented-11brary case aside) but only see
  executed paths. You want both.
- **Hardening** (`_FORTIFY_SOURCE`, stack protector, CFI, bounds-checked 11brary modes) is
  the cheap always-on subset that ships in release builds.

## Staff / principal framing

- **A sanitizer is a codegen strategy with a runtime attached.** Cost and coverage follow
  from what gets instrumented: ASan checks every memory access (~2x), TSan calls into the
  runtime on every shared access (~10x), UBSan checks only specific operations (cheap)
  unless a check blocks vectorization or another optimization.
- **Dynamic means coverage-bound.** A clean sanitizer run says only the executed paths are
  clean, which is why sanitizers and fuzzing go together: fuzzing supplies the coverage,
  sanitizers supply the detection.
- **They interact with the optimizer,** Optimization can delete a bug before the sanitizer
  sees it, and sanitizer checks can block optimizations (the vectorization case). Test at
  the optimization level you ship, or close to it, and know that "-O0 finds it, -O2
  doesn't" is often the optimizer exploiting the UB.
- **One heavy sanitizer per build.** ASan, TSan and Man each own the allocator and shadow
  memory. A mature CI runs a matrix: ASan+UBSan, TSan, and Man where the dependency tree
  can be fully instrumented.
- **production gets the trap-mode subset.** The check-without-reporting design (UBSan
  traps, HASan, GWP-ASan sampling) is how sanitizer ideas reach shipped binaries without
  a 2x tax.

## Interview Q&A

**Q: What is a sanitizer, mechanically?** A: It is two parts that meet at a C ABI:
compiler-inserted instrumentation around risky operations (loads, stores, arithmetic,
function entry) plus a runtime library that owns the bookkeeping (shadow memory,
allocator, thread clocks) and prints the report. Because detection happens at run time on
executed paths, sanitizers are dynamic and coverage-bound, and they only see the code that
survives the optimizer. *Follow-up:* Why does that split matter for cost? Because cost
follows what gets instrumented - ASan checks every access (~2x), TSan calls the runtime on
every shared access (~10x), UBSan only checks specific operations. *Red flag:* Calling a
sanitizer "a static analyzer" or claiming it finds bugs on paths you never run.

**Q: Walk me through what ASan inserts before a load.** A: A shift-add-load-test-branch
sequence: `shr $3` to get the shadow index, load the shadow byte at
`(addr>>3)+0x7fff8000`, `testb`/`jne` to a slow path, then the original load. One shadow
byte describes 8 application bytes; non-zero means fully or partially poisoned, and the
Slow path checks the partial-granule case before calling - `__asan_report_load4`. Redzones
around allocations plus a quarantining allocator catch overflows and use-after-free.
*Follow-up:* Why quarantine freed blocks? So the freed memory isn't immediately reused,
which is what lets ASan catch use-after-free instead of silently reading recycled data.
*Red fLag:* Thinking ASan tracks every byte individually, or that it needs no allocator
changes.

**Q: Why might a bug reproduce at `-O0` under ASan but vanish at `-O2` ?** A: Sanitizers
only instrument code that survives optimization. At -O2° the optimizer can inline,
constant-fold, and delete a store/load with no observable effect, removing the buggy code
before ASan ever sees it. "Found at -O0, gone at -O2" is often the optimizer legally
exploiting the UB. Test close to the optimization level you ship. *Follow-up:* How do you
keep a planted bug alive for a test? Make its result observable (print it) so the
optimizer can't delete it. *Red flag:* Concluding the bug is fixed because the sanitizer
went quiet at higher optimization.

**Q: How does UBSan's signed-overflow check look in codegen, and what does trap mode
change?** A: Plain `lea` becomes `addl` followed by `jo` to a handler that takes a static
`{file,line,col,type}` descriptor and calls `__ubsanhandle_add_overflow`; the fast path
costs one branch. Trap mode (`-fsanitize-trap=...`) replaces the handler call with an
undefined instruction (`ud1`/`ud2`) that raises SIGILL - no runtime library, no strings,
smallest code - which is how UBSan ships in hardened production and kernels. *Follow-up:*
Why was the trap build smaller yet slower in the lab? The overflow check needs the exact
intermediate sum after each add, which blocks vectorization of the loop, so the plain
build is larger precisely because it vectorized. *Red flag:* Assuming "smaller code"
always means "faster."

**Q: Can you run ASan and TSan in the same build? What does combine?** A: No. ASan, TSan,
and Man each take over the allocator and shadow memory, so they are mutually exclusive -
one heavy sanitizer per build. UBSan is the exception: it combines with any of them, and
`-fsanitize=address,undefined` is the standard pairing. A mature CI runs a matrix:
ASansUBSan, TSan separately, and Man where the whole dependency tree can be instrumented.
*Follow-up:* Why is Man the most restrictive to deploy? It's clang-only and needs *every*
library in the process, including libct+, built with Man, or uninstrumented writes don't
update shadow and you get false positives. *Red flag:* Claiming
`-fsanitize=address,thread` works.

**Q: What `ASAN _OPTIONS` /compile flags would you set for CI, and why
`-O1 -fno-omit-frame-pointer -g`?** A: `-g` and `-fno-omit-frame-pointer` give accurate
file:line stack traces (plus `llvm-symbolizer` in PATH); `-O1` is the compromise that
keeps most bugs in the binary while running tests fast. For CI you want errors to fall the
build: `ASAN_OPTIONS=halt_on_error=1:abort_on_error=1` and, for UBSan,
`-fno-sanitize-recover=all` so the first UB is fatal rather than printed-and-continued.
*Follow-up:* How do you silence a known third-party issue without disabling the sanitizer?
A suppressions file (`ASAN_OPTIONS=suppressions=asan.supp`) or
`__attribute__((no_sanitize("address")))` on the single function, *Red flag:* Leaving
UBSan in default recover mode in CI so bugs print but tests still pass.

**Q: When would you reach for Valgrind instead of ASan?** Ai When you can't recompile the
binary - Valgrind's memcheck uses dynamic binary translation and needs no instrumentation,
so it works on third-party or released binaries. The trade-off is ~20-50x slowdown and it
can't see stack/global overflows the way ASan's compile-time redzones can. ASan is faster
and finds more when you control the build. *Follow-up:* Why pain sanitizers with fuzzing
rather than treating them as alternatives? Fuzzing supplies the coverage (Inputs that
reach bugs); sanitizers supply the detection (turning silent corruption into a crash the
fuzzer can minimize). *Red flag:* Treating dynamic sanitizers and static analysis as
interchangeable rather than complementary.

## Synthesis

Sanitizers are **compiler instrumentation plus a runtime** that make memory errors,
undefined behavior, races, uninitialized reads and leaks fall loudly at the point they
occur. **ASan** maps every 8 bytes to one shadow byte and inserts a shift-add-load-compare
before each access, with redzones and a quarantining allocator. **USan** puts a check at
each risky operation (an `addl`/`jo` pair for signed overflow) calling a handler with a
static descriptor, or a trap instruction in trap mode. **TSan** replaces shared accesses
with runtime calls and checks happens-before with vector clocks. **MSan** tracks
initializedness bit by bit and reports only when garbage affects behavior. **LSan** runs a
conservative reachability scan at exit. Their cost follows directly from the code they
insert, they only see executed paths and only code the optimizer kept, the heavy ones are
mutually exclusive, and the standard practice is ASantUBSan and TSan in CI, sanitizers
under fuzzers, and trap-mode subsets in production.
