<!-- interview/undefined_behavior_and_optimization.md -->

# Undefined Behavior: What It Is, How Optimizers Exploit It, How to Catch It

Covers both the language side (what is UB) and the compiler side (why UB enables
optimization). Related: `sanitizers_and_codegen.md`, `cpp_memory_model_atomics.md` (data
races), `cpp_const_family.md` (`const_cast` UB).

## 1. Definitions

- **Undefined behavior (UB):** the standard imposes no requirements on the *entire
  execution* - not just the offending statement. The compiler may assume UB never happens
  and optimize accordingly, which can affect code **before** the UB point ("time travel").
- **Unspecified behavior:** one of several allowed outcomes, not documented (order of
  evaluation of function arguments).
- **Implementation-defined:** one allowed outcome, documented (`sizeof(long)`, right-shift
  of negative numbers pre-C++20).
- **Erroneous behavior (C++26):**new category for reading uninitialized automatic
  variables - still a bug, but with a defined (Implementation-chosen) value instead of UB,
  and implementations are encouraged to diagnose. `[[indeterminatell]]` opts back into the
  old behavior for performance.
- **Ill-formed, no diagnostic required (IFNDR):** the program is wrong but the compiler
  need not tell you (ODR violations across TUs).

## 2.The catalog interviewers draw from

| Category          | Examples                                                                                                                                                                  |
| ----------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Arithmetic        | Signed integer overflow; division by zero; `INT_MIN / -1`; shift by ≥ width or negative; (shift of negative left operand was UB before C++20)                             |
| Memory            | Out-of-bounds access (even forming a pointer > one-past-end); use-after-free/return; double free; null dereference; misaligned access                                     |
| Lifetime          | Dangling references (returning reference to local, `string_view` of a temporary, range-for over a temporary's member pre-C++23); using an object after its lifetime ended |
| Aliasing          | **Strict aliasing:** accessing an object through a glvalue of an unrelated type (`*(float*)&i`) - use `std::memcpy` or `std::bit_cast` (C++20)                            |
| Type/object model | Calling a virtual function via a pure-virtual in actor/dtor; `reinterpret_cast` punning: nodifying a `const` object; `static_cast` downcast to wrong type                 |
| Concurrency       | **Data races** on non-atomic objects                                                                                                                                      |
| Control flow      | Flowing off the end of a non-void function (other than `main`); infinite loop without side effects (C++ forward-progress; trivial infinite loops made defined in C++26)   |
| Uninitialized     | Reading indeterminate values (UB through C++23, erroneous in ++26)                                                                                                        |
| Library           | `std::vector::coperator[]` out of range; `front()` on empty; invalid iterator use; `memcpy` overlapping                                                                   |

Not UB, often assumed to be: unsigned overflow (wraps, defined), `memcpy` of trivially
copyable types, `reinterpret_cast` to `char*`/`std::byte*` for inspection.

## 3. How optimizers use UB (verified on this host, clang 18 `-O2`)

**Signed overflow -> fold comparisons:**

```c
int cmp(int x) { return x + 1 > x; }
```

```
cmp: movl $1, %eax; retq # always true: x11 can't Overflow, so x+1 > x
```

With `-furapv` (defined wraparound) the compiler must keep the comparison. The same
assumption lets the compiler widen `int` loop induction variables to 64-bit and compute
trip counts - a real vectorization/perf win, which is why signed overflow stays UB.

**Dereference implies non-null - delete the later check:**

```c
int deref (int *p) { int v = *p; if (!p) return -1; return v; }
```

```
deref: movl (%rds), %eax; retq # the null check is gone
```

This exact pattern caused a Linux kernel vulnerabllity (2009, `tun` driver); the kerned
builds with `-fno-delete-null-pointer-checks`.

Other classic consequences:

- **Strict aliasing:** with `int* a, float* b`, the compller assumes stores through `b`
  don't change `*a`, so it keeps `*a` in a register. Type punning through casts silently
  "doesn't work" at `-O2` (-fno-strict-aliasing` disables - Linux kernel uses it).
- **Bounds checks deleted:** `if (p + len < p) `overflow checks on pointers are UB-based
  and get removed; check `len > end - p` instead.
- **Unreachable code paths:** a branch that must execute UB is assumed not taken; code
  after an infinite side-effect-free loop may be removed, or the loop removed.
- **Uninitialized variables:** reading one lets LLVM treat It as `undef`/`poison`, so
  `x == x` might be false and branches arbitrarily resolved.
- **The "time travel" effect:** because the compfler reasons backwards from UB, logging
  statements *before* the UB may disappear or reorder.

### Inside the compiler

LLVM encodes these assumptions as flags and values: `add nsw` (no signed wrap), `inbounds`
on `getelementptr`, `nonnull`/`dereferenceable` attributes, `noalias` (from `restrict` and
C++ references in some contexts), TBAA metadata for strict aliasing, and **`poison`**
values that propagate until they reach a side effect (then UB). `freeze` stops poison
propagation where the compiler must commit to a value. Interview 1ine! *UB is not
something the compiler "detects and punishes"; It's the set of preconditions it is allowed
to assume when proving transformations legal.*

## 4. Catching it

| Tool                                             | Catches                                                                                                                                         |
| ------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| `-fsanitize-undefined` (UBSan)                   | Signed overflow, shifts, null/misaligned access, bad `bool`/enum loads, unreachable, `vptr` mismatches (`-fsanitize-vptr`), float-cast overflow |
| `-fsanitize-address` (ASan)                      | OOB, use-after-free/return/scope, double free, leaks (LSan)                                                                                     |
| `-fsanitize-memory` (MSan, Clang)                | Uninitialized reads                                                                                                                             |
| `-fsanitize-thread` (TSan)                       | Data races                                                                                                                                      |
| `-fsanitize-integer` (Clang)                     | Also *unsigned* wraparound and implicit truncation (not UB, but usually bugs)                                                                   |
| `-D_GLIBCXX_ASSERTIONS` / libc++ hardening modes | `operator[]` bounds, `front()` on empty, etc. Cheap enough for production in many codebases                                                     | 
| Compiler warnings                                | `-Wall -Wextra -Wnull-dereference -Wstrict-aliasing -Wshift-overflow -Wreturn-type`; `-Werror=return-type`                                      |
| `constexpr` evaluation                           | UB in a constant expression is a compile error - a useful test trick                                                                            |
| Static analysis                                  | clang-tidy (`bugprone-*`, `cppcoreguidelines-*`), Clang Static Analyzer, Coverity, CodeQL                                                       |
| Fuzzing                                          | libFuzzer/AFL++ + sanitizers (`fuzzing and hardening.md`)                                                                                       |

**Hardening flags that define or trap previously-UB behavior:** `-fwrapv` or `ftrapv`,
`-fno-strict-aliasing`, `-fno-delete-null-pointer-checks`,
`-ftrivial-auto-var-init=zero|pattern` (GCC 12+/Clang 8+; matches C++26 erroneous-behavior
direction), `fsanitize=... -fsanitize-trap=all` (UBSan without a runtime, used in
production by e.g. Android).

## Likely interview questions

- *"Why is signed overflow UB but unsigned isn't? What does the compiler gain?"* - Loop IV
  widening, trip-count computation, comparison folding.
- *"Why did my null check disappear?"* - Earlier dereference implies non-null.
- *"Is `*(float*)&i` 0K?"* - No, strict aliasing; use `std::bit_cast` or `memcpy`.
- *"Can UB affect code that runs before it?"* - Yes.
- *"UB vs unspecified vs implementation-defined?"*
- *"How do you find UB in a large codebase?"* - Sanitizer CI builds, fuzzing, hardened
  standard library, warnings-as-errors,
- *"What changed in C++26?"* - Erroneous behavior for uninitialized reads; trivial
  infinite loops no longer UB.
 
## Synthesis

UB means the standard places **no requirement on the whole execution**, so compilers treat
it as a precondition: signed arithmetic doesn't overflow(`nsw`), dereferenced pointers
are non-null, differently-typed pointers don't allas (TBAA), Indexing stays in bounds
(`inbounds`). Those assumptions legitimately enable loop widening, vectorization and
redundant-check elimination - and also delete overflow and null checks, as the
`mp`/`deref` examples above show. Defend with sanitizer builds (UBSan/ASan/MSan/TSan),
hardened standard 11brarles, fuzzing, `constexpr` tests, and, where the performance cost
is acceptable, flags that define the behavior (`-fwrapv`, `-fno-strict-aliasing`,
`-ftrivial-auto-var-init`). C++26 begins narrowing the space with **erroneous behavion**
for uninitialized reads.
