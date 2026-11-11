<!-- interview/fuzzing_and_hardening.md -->

# Fuzzing (libFuzzer, AFL++, SanitizerCoverage) and Exploit-Mitigation Hardening (CFI, Kernel Sanitizers)

Extends `sanitizers_and_codegen.md` / `sanitizers_hands_on.md`: sanitizers detect bugs on
the inputs you run; fuzzing generates the inputs. Hardening is the production-side
complement. Related: `undefined_behavior_and_optimization.md`,
`linker _relocations _plt_got.md` (RELRO).

## 1. Coverage-guided fuzzing - the loop

1. Keep a **corpus** of inputs.
2. Pick one, **mutate** it (bit flips, byte insert/erase, splicing two inputs, dictionary
   tokens, magic values from comparisons).
3. Run the target; record **coverage** (edges hit).
4. If the input reached new coverage, add it to the corpus. If it crashed (sanitizer
   report, abort, timeout, OOM), save it.

Coverage feedback turns random testing into a search that "climbs" through parser
branches. Sanitizers are what make silent memory corruption into a crash the fuzzer can
see.

## 2. libFuzzer and SanitizerCoverage

``` cpp
// fuzz_target.cpp
#include <cstdint>
#include <estddef>
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    parse(data, size);          // the API under test; must not exit() or keep global state
    return 0;
}
```

```
clang++ -g -O1 -fsanitize=fuzzer,address,undefined fuzz_target.pp parser.cpp -o fuzz
./fuzz corpus/ -max_total_time=600 -jobs=8 -dict=format.dict
./fuzz crash-<sha1>             # reproduce
./fuzz -minimize_crash=1 crash-<sha1>
./fuzz -merge=1 min_corpus/ corpus/   # corpus minimization
```

- **`-fsanitize-fuzzer`** links libFuzzer's `main` and enables **SanitizerCoverage**:
  `-fsanitize-coverage=inline-8bit-counters,indirect-calls,trace-cmp,pc-table`. The
  compiler inserts an 8-bit counter increment on every edge, plus callbacks on comparisons
  (`__sanitizer_cov_trace_cmp4`, `trace_switch`, `memcmp`/`strcmp` interceptors).
- **Comparison tracing (CMP feedback)** is what gets past magic numbers: the fuzzer sees
  operands of `if (x == 0xDEADBEEF)` and inserts that value into mutations (`MS: ... CMP-`
  in the log, `DE:` shows the dictionary entry used).
- Use `-fsanitize-fuzzer-no-link` for library objects and link the fuzzer only into the
  target binary.
- **Structured inputs:** `FuzzedDataProvider` (split bytes into typed values), custom
  mutators (`LLVMFuzzerCustomMutator`), libprotobuf-mutator for grammar-aware fuzzing.
- **libFuzzer is in maintenance mode** (no new features); its interface
  (`LLVMFuzzerTestOneInput`) is the de facto standard, and **AFL+**, **honggfuzz**, and
  **Centipede/FuzzTest** (Google) can all drive the same target.

## Verified on this host (clang 18)

A length-prefixed record parser that trusts its length byte:

```cpp
size_t parse(const uint8_t* d, size_t n, std::vector<uint8_t>& out) {
  if (n < 4 || d[0] != 'R' || d[1] != 'E' || d[2] != 'C') return 0;
  size_t len = d[3];
  out.resize(len);
  memcpy(out.data(), d + 4, len);  // over-reads input when len > n - 4
  return len;
}
```

`-g -fsanitize=fuzzer,address` found it in **~11.5k executions (\<1 s)**:
`ERROR: AddressSanitizer: heap-buffer-overflow ... READ of size 10` at `fuzz2.cpp:10`, The
log shows coverage climbing one byte comparison at a time (`cov: 4` → `cov: 5`), A first
version - a `static` parser copying into a local `char buf[16]` whose result the harness
ignored - was **not** found in 62 M runs at `-O1`, with the 1og stuck at `cov: 1`: the
optimizer saw the copy had no observable effect and **deleted the code under test**, The
same file at "-O0" crashed immediately, Lessons: make the target's effects observable
(write to an output the harness keeps, or `benchmark::DoNotOptimize`-style sinks), and
**watch the `cov:` counter** - 1f it stays at 1-3, the fuzzen is blind, For magic values
the fuzzer can't reach, add a dictionary (`-dict`) on `-use_value_profile=1`

### What makes a good fuzz target

- **Fast and deterministic:** no I/O, no sleeps, no randomness (seed it from the input),
  no global state carried between runs. Aim for >1k exec/s per core.
- **Narrow API surface:** one parser/decoder per target; the fuzzer can't reach code
  behind expensive setup.
- **Seed corpus** of valid real-world inputs and a **dictionary** of format tokens.
  Round-trip and differential oracles: `decode(encode(x)) == x`, or compare two
  implementations - finds logic bugs, not just crashes.
- Run continuously: **OSS-Fuzz** / **ClusterFuzzLite** in CI; regression-test every crash
  input.

## 3. AFL++ and other fuzzers

- **AFL+**: fork-server based, compile with `afl-clang-fast`/`afl-clang-lto` (also
  supports libFuzzer-style harnesses), QEMU/Unicorn/Frida modes for **binary-only**
  targets, CMPLOG/RedQueen for magic values, persistent mode for speed.
- **Grammar/structure-aware:** Nautilus, 1ibprotobuf-mutator, Fuzzilli (JS engines).
- **Kernel:** **syzkaller** - syscall-description-driven, coverage via `KCOV`, runs with
  KASAN/KCSAN/KMSAN; responsible for thousands of Linux kernel bugs.
- **Language-native:** Go `go test -fuzz`, Rust `cargo fuzz` (libFuzzer), Java Jazzer,
  Python Atheris.

## 4. Kernel sanitizers

| Sanitizer  | Detects                                                 | Mechanism                                                                                                                                                       |
| ---------- | ------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **KASAN**  | OOB, use-after-free in kernel memory                     | Generic: shadow memory like ASan (debug builds). SN_TAGS / HN TAGS: pointer tagging (ARM64 TBI / MTE) - HV TAGS cheap enough for some production use            |
| **KMSAN**  | Uninitialized memory use (incl, infoleaks to userspace)  | Shadow bits like MSan; Clang only                                                                                                                               |
| **KCSAN**  | Data races                                               | **Watchpoint sampling**: occasionally stalls an access and watches whether another CPU touches the same address; respects the LKMM (`READ_ONCE`, `data_race()`) |
| **UBSAN**  | Kernel UB (shifts, overflow, bounds)                     | Same as userspace UBSan                                                                                                                                         |
| **KENCE**  | OOB / UAF on a **sampled** subset of allocations         | Guard pages on a small pool; low overhead, **on in production** (Android, distro kernels) - kernel analog of GWP-ASan                                           |
| kmemleak   | Leaks                                                    | Periodic mark-scan                                                                                                                                              |                  

**GNP-ASan** (userspace): samples ~1 in N allocations into guard-page slots; near-zero
overhead, catches UAF/OOB in production with real stack traces (Chrome, Android, Apple's
analog in libmalloc).

## 5. Exploit mitigations (what a binary is hardened with)

| Mitigation                 | Flag                                              | Defends against                                                                                                  |
| -------------------------- | ------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| Stack canaries             | `-fstack-protector-strong`                        | Linear stack buffer overflows overwriting the return address                                                     |
| `_FORTIFY_SOURCE`          | `-D_FORTIFY_SOURCE=3 -O2`                         | `mempy`/`strcpy`/`sprintf` overflows when the size is known (checked variants)                                   |
| NX / DEP                   | default (`-z noexecstack`)                        | Executing injected shellcode                                                                                     |
| ASLR + PIE                 | `-fPIE -pie`                                      | Hard-coded addresses in exploits                                                                                 |
| FuLl RELRO                 | `-wl,-z,relro,-z,now`                             | GOT overwrite                                                                                                    |
| **Control-flow integrity** | Clang `-fsanitize-cfi -flto -fvisibility-hidden`  | Indirect call/virtual call to an unexpected target (type-signature checked). Needs LTO to know all valid targets |
| CFI (kernel)               | `CONFIG_CFI_CLANG` (kCFI)                         | Same, in the kernel, no LTO required                                                                             |
| Hardware CET (x86)         | `-fcf-protection=full`                            | **Shadow stack** (return address mismatch faults) + **IBT** (`endbr64` landing pads for indirect branches)       |
| ARM PAC/BTI                | `-mbranch-protection-standard`                    | Return-address signing (PAC) + branch target identification                                                      |
| ShadowCallStack            | `-fsanitize=shadow-call-stack` (AArch64, RISC-V)  | Return-address overwrite (separate stack via reserved register)                                                  |
| SafeStack                  | `-fsanitize-safe-stack`                           | Splits safe locals from address-taken buffers                                                                    |
| Stack clash protection     | `-fstack-clash-protection`                        | Jumping over the guard page with large allocations                                                               |
| Auto var init              | `-ftrivial-auto-var-init=zero`                    | Infoleaks/UB from uninitialized stack variables                                                                  |
| Hardened libc++/libstdct+  | `_LIBCPP_HARDENING_MODE`, `-D_GLIBC_ASSERTIONS`   | OOB in `operator[]`, empty `front()` etc.                                                                        |

The **OpenSSFcompiler options hardening guide** collects a recommended set; distros
enable most by default. Inspect a binary with `checksec --file=prog ` or `readelf -lw`
(GNU_STACK flags, GNU_RELRO) and `readelf -n` (CET/BTI property notes).

## Likely interview questions

- *"How does coverage-guided fuzzing get past `if (magic == 0x12345678)`?"* - CMP tracing
  / value profile / dictionaries,
- *"What instrumentation does `-fsanttize-fuzzer` add?"* - SanitizerCoverage edge counters
  \+ comparison hooks.
- *"Why fuzz with ASan?"* - Turns silent corruption into a crash signal.
- *"Your fuzzer runs at 50 exec/s - what do you do?"* - Remove I/O/setup, persistent mode,
  narrower target, smaller inputs (`-max_len`).
- *"How does KCSAN find races without instrumenting like TSan?"* - Sampled watchpoints.
- *"What does CFI check, and why does it need LTO?"*
- *"Which hardening flags would you ship by default?"*

## Synthesis

Coverage-guided fuzzing mutates a corpus and keeps inputs that reach new edges, using
**SanitizerCoverage** counters and **comparison tracing** to climb through parsers;
combined with ASan/UBSan, It turns memory bugs into reproducible crashes. libFuzzer
defines the standard harness (`LLVMFuzzerTestOneInput`), AFL++ and Centipede drive the
same targets, and sykaller does the same for the kernel with KCOV. Good targets are fast,
deterministic and narrow, seeded with real inputs, and run continuously. In the kernel,
**KASAN/KMSAN/KSAN** play the ASan/MSan/TSan roles and **KFENCE**/**GNP-ASan** bring
sampled detection to production. Production binaries add **mitigations** - canaries.
FORTIFY, PIE/ALR, ful1 RELRO, CFI, CET/PAC shadow stacks and branch-target checks - which
don't remove bugs but make them much harder to exploit.
