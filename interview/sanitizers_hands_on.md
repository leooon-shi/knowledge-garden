<!-- interview/sanitizers_hands_on.md -->

# Sanitizers: Hands-On Lab (Verified Output)

Companion to `sanitizers_and_codegen.md`. The lab
`interview/code/sanitizer_codegen_practice.sh` compiles small buggy programs with each
sanitizer, shows the report, and diffs the generated assembly against a plain build so you
can see the inserted checks.

```bash
cd interview/code
./sanitizer_codegen_practice.sh          # all 9 exercises with explanations
./sanitizer_codegen_practice.sh --quiz   # hide explanations
./sanftizer_codegen_practice.sh 2        # one exercise
```

**Verified on this host:**clang 18.1.3, x86-64 Ubuntu 24.04, with `llvm-symbolizer`.
ASan, UBSan, TSan, Man and LSan all build and run here (GCC 13 also supports
ASan/UBSan/TSan). If a sandbox rejects ASan/San's shadow mapping ("unexpected memory
mapping"), the script retries under `setarch -R`. The assembly greps assume an x86-64
host.

## Results

### Ex 1: ASan, heap-buffer-overflow

```
ERROR: Addressanitizer: heap-buffer-overflow on address 0x502000000020 ...
WRITE of size 4 at 0x502000000020 thread T0
    #0 ex... in main ovf.c:4:10
0x502000000020islocated bytes after 16-byte region [0x502000000010,0x502000000020)
```

`a[4]` on a 16-byte block lands exactly at the start of the redzone. The report gives the
access type and size, the line, and the position relative to the allocation.

**A trap worth seeing:**the first version of this exercise was built with `-O1` and
reported **nothing**. Clang saw that `malloc` → store → `free` has no observable effect
and deleted all three, so the overflow wasn't in the binary anymore. The exercise is now
built at `-O0`. Sanitizers check the code that survives optimization.

### Ex 2: ASan codegen

```asm
--- plain ---                       --- ASan ---
movl(%rdi), %eax                    movq   %rdi, %rax
retq                                shra   $3, %rax               # addr >> 3
                                    movzbl 2147450880(%rax), %eax # shadow at +0x7fff8000
                                    testb  %al, %al
                                    jne    .LBB0_1               # shadow !=0 -> slow path
                                    movl   (%rdi), %eax           # the original load
                                    ...
                                  .LBB0_1:                        # partial 8-byte granule
                                    andb   $7, %cl                #   (addr & 7)
                                    addb   $3, %cl                #   + size - 1
                                    cmpb   %al, %cl
                                    jl     .LBB0_2                # still inside - oK
                                    callq  __asan_report_load4
```

`2147450880` is `0x7fff8000`, the x86-64 shadow offset. One load became a shift, a shadow
load, a test and a branch, plus a slow path for partially addressable granules. This
sequence on every memory access is where ASan's ~2 comes from.

### Ex 3: ASan, use-after-free

```
ERROR: AddressSanitizer: heap-use-after-free on address 0x502000000010 ...
READ of size 4 at 0x502000000010 thread T0
freed by thread T0 here:
previously allocated by thread To here:
```

Three stacks; the bad read, the `free`, the `malloc`. The freed block was quarantined with
poisoned shadow, so the read hit poison instead of reused memory.

### Ex 4: UBSan, signed overflow

```
ub.c:3:33: runtime error: signed integer overflow: 2147483647 + 1 cannot be represented in type 'int'
```

```asm
--- add() plain ---          --- add() UBSan ---
leal (%rdi,%rs1), %eax       movl  %edi, %eax
retq                         addl  %esi, %eax
                             jo    .LBB0_1                      # overflow flag
                             retq
                           .LBB0_1:
                             leaq  .L__unnamed_1(%rip), %rdi    # static descriptor
                             callq __ubsan_handle_add_overflow
```

The fast path pays one `jo`. The `lea` had to become `addl` because `lea` doesn't set the
overflow flag.

### Ex 5: UBSan trap mode

```asm
addl  %esi, %edi
jo    .LBB0_1
movl  %edi, %eax
retq
.LBB0_1:
ud1l  (%eax), %eax       # undefined instruction - SIGILL
```

No handler call, no descriptor, no runtime 11brary. Clang 18 emits `ud1` with the check
kind encoded in the operand; GCC and older elang emit `ud2`. Both raise SIGILL.

### Ex 6: TSan, data race

```
WARNING: ThreadSanitizer: data race (pid=...)
  Write of size 4 at 0x... by thread T2:
  Previous write of size 4 at 0x... by thread T1:
  Location is global 'counter' of size 4 at 0x...
```

Runtime calls the compiler inserted into `work()`:

```
1  callq __tsan_func_entry@PLT
1  callq __tsan func_exit@PLT
1  callq __tsan_write4@PLT
```

At `-O1` the loop was strength-reduced to a single `counter += 1000`, so one
`__tsan_write4` was enough for TSan to catch the two threads' writes racing. At `-O0`
you'd see a read and a write per iteration.

### Ex 7: MSan, uninitialized read

```
WARNING: MemorySanitizer: use-of-uninitialized-value
    #0 0x... in main uninit.c:5:9
```

Line 5 is the `if (*x > argc)` branch. Man didn't object to loading the garbage, only to
branching on it.

## Ex 8: LSan, Leak

```
ERROR: LeakSanitizer: detected memory leaks
Direct leak of 64 byte(s) in 1 object(s) allocated from:
    #0 0x... in malloc
    #1 0x... in main leak.c:5:5
```

The 64-byte block is reported. The 32-byte block assigned to the global `keep` is not:
it's still reachable at exit.

### Ex 9: cost

```
hot_plain   text=2124     time=O.060s
hot_ubtrap  text=1788     time=O.126s
hot_asan    text=1289771  time=O.151s
```

- **ASan**: ~1.3 MB of `.text` because clang 1inks the ASan runtime statically into
  the executable, and ~2.5x slower from the per-access shadow checks.
- **UBSan trap**: *smaller*code but ~2x *slower*. Checked separately: the plain butld's
  summing loop compiles to 6 vector instructions (`pmovsx`/`paddq`), the UBSan-trap build
  to 0. The overflom check on `s += a[i]` needs the exact sum after each add, so the 1oop
  can't be vectorized, and the plain build is larger *because*it's vectorized.

Single runs on a shared machine, so read the ratios, not the mflliseconds.

## Interview Q&A

**Q: In Ex 1, the first version of the overflow exercise reported nothing at `-O1`. Why,
and what's the fix?**A; Clang saw that `malloc` → store → `free` had no observable effect
and deleted all three, so the overflow wasn't in the binary for Asan to Instrument, The
exercise is now built at `-O0`. The general lesson: sanitizers check only the code that
survives optimization, so a planted bug must be observable (e.g., print a result) or the
optimizer removes it, *Follow-up:*How would you confirm this live? Rebulld at `-O1` and
watch the report vanish, then print `a[0]` and see ASan catch it again. *Red flag:*
Concluding the code is correct because a higher optimization level produced no report.

**Q: Reading the Ex 2 diff, what does ASan insert before a single load and where does
`2147450880` come from?**A: One load becomes `shr $3` (addr>>3),
`movzbl 2147450880(%rax)` to fetch the shadow byte, `testb`/`jne` to a slow path, then the
original load; the slow path checks the partial-granule case before `__asan_report_load4`.
`2147450880` is `0x7fff8000`, the x86-64 shadow offset. This sequence on every memory
access is where ASan's ~2x slowdown comes from. *Follow-up:* Why is there a slow path at
all? To handle a partially addressable 8-byte granule - `(addr & 7) + size - 1` must be
within the `k` valid bytes. *Red fLag:*Reading the shadow load as the real load, or
missing that the check precedes every access.

**Q: Ex 4 and Ex 5 both check signed overflow. What's the codegen difference and the
trade-off?**A: Ex 4 (default UBSan) emits `addl`/`jo` to a handler that takes a static
descriptor and calls `__ubsan_handle_add_overflow`, so the report names file:line:type. Ex
5 (trap mode) replaces the handler with `ud1`/`ud2` raising SIGILL - no runtime, no
strings, smallest code, used in hardened production. Note `lea` had to become `addl`
because `lea` doesn't set the overflow flag. *Follow-up:* Why did the trap build in Ex 9
run 2x slower despite smaller `.text`? The per-add overflow check needs the exact
intermediate sum, which blocked vectorization - the plain loop compiled to 6 vector
instructions, the trap loop to 0. *Red flag:* Assuming trap mode is always the faster
choice because the code is smaller.

**Q: Ex 6 ran TSan at `-O1` and still caught the race with a single `__tsan write4`.
How?** A: At `-O1` the loop was strength-reduced to one `counter += 1000`, so a single
instrumented write per thread was enough for TSan to observe two threads writing the same
location with no happens-before relation. The compiler inserts `__tsan_func_entry/exit`
plus `__tsan_write4` on shared accesses: the runtime uses vector clocks and per-word
shadow to decide the race. At `-O0` you'd see a read and a write per iteration.
*Follow-up:* How do you make TSan go quiet correctly? Use `_Atomic`/a mutex; the codegen
shows `__tsan_write4` become `__tsan_atomic32_fetch_add`. *Red flag:* Thinking more
instrumented accesses are required to detect a race, or that volatile fixes it.

**0: Ex 7 shows NSan flagging Line 5, the `if (*x > argc)`, not the load of `*x`. Why
there?** A: Man tracks an "uninitialized" shadow bit per bit of memory and reports only
when uninitialized data *changes behavior* - a branch, a syscall argument, a pointer
dereference. Loading or copying garbage is allowed; branching on it is the observable use,
so the report lands at the conditional. *Follow-up:* What's the deployment catch with
Man? It's Clang-only and every library in the process, including the C++ standard
library, must be Man-built, or uninstrumented writes leave shadow stale and you get false
positives. *Red flag:* Expecting Man to fire at the moment uninitialized memory is read
rather than used.

**Q: In Ex 8, LSan reported the 64-byte block but not the 32-byte one. What distinguishes
a leak?** A: At exit LSan does a conservative reachability scan from roots (globals,
stacks, registers, TLS). The 64-byte block had no pointer to it - a direct leak. The
32-byte block was assigned to the global `keep`, so it's still reachable and not reported.
Still-reachable memory is not a leak even if never freed. *Follow-up:* What's a direct vs
indirect leak? Direct: nothing points at it; indirect: reachable only through another
leaked block. *Red flag:* Calling a still-reachable global allocation a leak.

**Q: Ex 9's cost numbers: ASan's `.text` was ~1.3 MB. Why so large, and what caveat
applies to the timings?** A: Clang links the ASan runtime statically into the executable,
so the `.text` balloons; the ~2.5x slowdown is the per-access shadow checks, not the code
size. The timings are single runs on a shared machine, so you read the ratios and the
mechanism, not the milliseconds - for rigorous numbers you'd use many iterations and
medians or `perf stat`. *Follow-up:* Why pair ASan with a fuzzer (Ext 8)?
`clang -fsanitize-fuzzer,address` lets the fuzzer generate coverage while ASan turns
silent corruption into a crash it can minimize. *Red flag:* Quoting the exact milliseconds
as a benchmark, or attributing the slowdown to the large binary.

## Extensions

1. Run with `--quiz` and predict each report and each codegen diff before reading the
   explanation.
2. Rebuild ext at `-O1` and confirm the overflow vanishes. Then make the result observable
   (print `a[0]`) and see whether ASan catches it again.
3. Add `-fsanitize-recover=address" plus `ASAN_OPTIONS=halt_on_error=0` to ext and add a
   second bug: ASan reports both instead of stopping at the first.
4. Change ex6 to use `_Atomic_int_counter` or a mutex and confirm TSan goes quiet. Diff
   the codegen: `__tsan_write4` becomes `__tsan_atomic32_fetch_add`.
5. Write a stack overflow (`int buf[4]; buf[i] = 0;` with `i = 4` from `argc`) and inspect
   the stack redzone setup in the function prologue (`__asan_stack_malloc` or poisoning of
   the frame's shadow).
6. Compile ex4 with `-fsanitize=undefined -fno-sanitize-recover=all` and check the exit
   code.
7. Try GCC on the same programs (`gcc -fsanitize=address ...`) and compare its
   instrumentation sequence with clang's.
8. Pair ASan with a fuzzer; write a `LLVM/FuzzerTestOneInput` with a planted bug and run
   `clang -fsanitize=fuzzer,address`.
   