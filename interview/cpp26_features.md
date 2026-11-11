<!-- interview/cpp26_features.md -->

# Popular C++26 Features, Benefits, and Compiler/Platform Support

See also: `cpp20_concepts_ranges_modules.md` (the C++20 foundations Ct26 builds on).

**Status note (late 2026):** C++26 was technically finalized in 2025 (feature freeze June
2025, ballot resolution at the Nov 2025 Kona meeting) and is going through ISO publication
as ISO/IEC 14882:2026. Reflection and contracts are in; **pattern matching
(inspect/match) did NOT make C++26**, and **trivial relocatability (P2786) was voted
in at Hagenberg (Feb 2025) and then removed again at Kona** - both are Ct29 candidates.
Distinguish "voted into the standard" from "usable in your toolchain today" (support lags
by years; gate on •_cpp_* feature-test macros). Exact per-version support moves fast -
verify against cpreference's compiler-support page and each vendor's status tracker.

## Part 1 - Features and benefits

### 1. Static Reflection (headline)

**What:** compile-time introspection + code generation - query
members/names/enumerators/attributes via the *^^* operator (-> `std::meta::info`),
**splice** back with `[: ... :]`, plus `std:meta` functions. **Benefits:** eliminates
serialization/ORM/enum\<->string/hashing boilerplate; replaces macros + external codegen +
heavy THP; type-safe, zero-runtime-cost. Most transformative feature; reshapes library
authoring.

### 2. Contracts

**what:** language `pre(...)`, `post(...)`, `contract_assert(...)` with conflgurable
enforcement (ignore/observe/enforce). **Benefits:** formalizes correctness in the language
(vs. ad-hoc `assert`), configurable defensive programming, foundation for static analysis.
(Landed late/contentious; still settling.)

### 3. `std::execution` (senders/receivers)

**What:** P2300 structured async/parallel model -
`schedule`/`then`/`when_all`/`sync_wait`, composable with `operator|`. **Benefits:**
standard composable concurrency (no per-library executors), structured
lifetimes+cancellation, targets CUs and accelerators.

### 4. `std::hazard_pointer` / `std::rcu`

**What:** standardized safe memory reclamation for lock-free structures. **Benefits:**
stdlib building blocks for scalable read-mostly concurrent containers without `shared_ptr`
refcount contention (the MVCC/COW-`shared ptr` techniques, standardized).

### 5. `std::simd`

**What:** portable explicit SID type across SSE/AV/NEON/SVE. **Benefits:** portable
vectorization without intrinsics; wins for numeric/DSP/ML/graphics loops.

### 6. Safety & quality-of-life

- **Erroneous behavior (EB):** uninitialized reads become diagnosable rather than UB -
  kills a class of security bugs.
- **More `constexpr` stdlib**, `std::print`/`std::format` polish, **pack indexing**
  (`pack...[N]`), generic-code ergonomics; legacy cruft deprecated.

### 7. Library additions

- **`std::inplace_vector`*** - fixed-capacity, stack-allocated (no heap) -
  bounded/allocation-free for embedded/hot paths.
- **`std::submdspan`**/`mdspan` growth; **debugging** (`std::breakpoint`,
  `std::is_debugger_present`), `std::text_encoding`.

## Part 2 - Compiler & platform support

### General picture

Support is **early, incremental, and per-feature** behind `-std-c++26` / `-std-c++20`
(`/std::c++latest` on MSVC). Full conformance historically lands ~3-5 years after
finalization, It's the "design toward it, gate on feature-test macros" phase, not "use
freely in production."

### The three majors

- **CCC (11bstdc++);** `-std=c++26` (on `-std=c++2c`), ~GCC 14+ with more each release
  (GCC 15/16 add large chunks, incl, reflection work in GCC 16), Strong/early on 
  core-language + 11brary additions, Tracks the GCC C++ Standards Support page.
- **clang(libc++):** `-std-c++26`, ~Clang17-18+ for early bits; Clang 20+ for most
  core-language Items, Often **fastest on experimental language features** (led reflection
  prototyping), Key nuance: the **front-end (language)** and **libc++ (library)** advance
  independently one may work while the other lags, Tracks exx-status + libc++ release
  notes.
- **MSVC (MS STL):** `/std:c++latest` (`/permissive-` for conformance); no stable
  `/std:c++26` unt11 nearer publication, Strong, actively-developed open-source STL with a
  public tracker; conformance-focused; historically slightly behind Clang on the most
  experimental Language features.

### Feature-by-feature reality (the important nuance)

"Does compller X support C++26?" is the wrong question; "does X support *this feature*
yet?" is right.

- **Reflection:** most complex, landed late - **usable last**; first as
  experimental/partial (EDG, Clang prototypes). Don't assume production reflection soon.
- **Contracts:** late/contentious -> experimental/partial first, behind flags, semantics
  settling.
- **`std: :execution`:** available *today* via reference impl **stdexec** (NVIDIA) as a
  library, ahead of stdlib inclusion.
- **`std::simd` / `inplace_vector` / `hazard_pointer` / `rcu`:** arrive as each STL
  implements them; some via `std::experimental`/TS precursors already.
- **EB, pack indexing, smaller papers:** land earliest and most uniformly. Consequence:
  you'll often have a feature in the *compiler* but not the *library* (or vice versa) ->
  gate on **feature-test macros** (`__cpp__*`, `__cpp_lib_*`), not compiler version.

### Platforms

- **Linux** - GCC + Clang, all major arches (x86-64, AArch64...); primary proving ground.
- **Windows** - MSVC (primary) + Clang/clang-cl; `/std:c++latest`.
- **macOS/Apple** - Apple Clang **lags upstream LLVM** a release or two; use
  upstream/Homebrew LLVM for newer features sooner.
- **Embedded/GPU** - via GCC/Clang backends; `std::simd`/`std::execution` on accelerators
  is active but bleeding-edge.

## Staff / Principal-level framing

- **"Feature-complete standard" != "usable toolchain."** Treat C++26 as *dtrectional* for
the next few years; adopt features individually as your compilertSTLsupport them.
- **Gate on feature-test macros, never `__plusplus`/compiler version** - 1anguage and
  library halves advance independently and vendors differ;
  `#ifdef __cpp_lib_inplace_vector`-style guards are the only reliable mechanism.
- **pin the toolchain and test the matrix** - pin exact GCC/Clang/MSVC versions in CI
  and test the actual feature set used; portability across vendors must be verified, not
  assumed.
- **Reflection and contracts are the long poles** - plan for them last,
  experimental-first; don't architect around them until support is real. Execution (via
  stdexec), simd, inplace_vector, EB are reachable sooner.
- **Get benefits today via libraries** - stdexec (senders/receivers),
  `std::experimental::simd`, third-party reflection/hazard-pointer 1ibs - decoupling
  adoption from stdlib rollout.

## Interview Q&A

**Q: What's the headline feature of C++26, and why does it matter?** A: Static reflection
- compile-time introspection plus code generation via the `^^` operator (yielding
`std::meta::info`), splicing back with `[: ... :]`, and `std::meta` functions. It matters
because it eliminates serialization/ORM/enum-to-string/hashing boilerplate and replaces
macros plus external codegen plus heavy template metaprogramming with type-safe,
zero-runtime-cost code. It reshapes how libraries are authored. *Follow-up:* When can I
use it in production? - Not soon; it's the most complex feature, landed late, and will be
usable last (experimental/partial first), so plan for it last. *Red flag:* Claiming
reflection is production-ready across major compilers today, or confusing it with runtime
RTTI.

**Q: A candidate says "C++26 has pattern matching and trivial relocatability." Correct
them.** A: Both claims are wrong as of late 2026. Pattern matching (`inspect`/`match`) did
NOT make C++26 - it's a C++29 candidate. Trivial relocatability (P2786) was voted into the
draft at Hagenberg in Feb 2025 and then removed again at Kona in Nov 2025, so it's also
not in C++26. What did land includes reflection, contracts, `std::execution`, `std::simd`,
`hazard_pointer`/`rcu`, `inplacevector`, erroneous behavior, and pack indexing.
*Follow-up:* What's the right mental model for "voted in"? - "Voted into the standard" is
not "usable in your toolchain"; support lags by years, and features can even be reverted
between meetings. *Red flag:* Treating any conference-meeting vote as final, or not
knowing trivial relocatability was pulled.

**Q: Why should you gate feature use on `__cpp_*` macros rather than compiler version or
`__cplusplus`?** A: Because C++26 support is early, incremental, and per-feature, and the
Language front-end and the standard library advance independently. You can have a feature
in the compiler but not the library (or vice versa), and vendors differ. Only feature-tes
 macros like `_ _cpp_lib_inplace_vector` reliably tell you whether the exact feature you
need is actually present. *Follow-up:* Give a concrete split example. - Clang's front-end
may support a language feature while libc++ hasn't shipped the corresponding library piece
yet, *Red flag:* "Just check `__cplusplus >- 202600L`" - that tells you nothing about
which features are actually implemented.

**Q: You want senders/receivers async composition today, before your STL ships
`std::execution`, What do you do?** A: Use the reference implementation stdexec (NVIDIA)
as a library - it's available today, ahead of stdlib inclusion. This is the general
principle for C++26; pull benefits forward via reference or third-party libraries where
the standard 11brary lags, e.g., stdexec for execution, `std::experimental::simd`, and
third-party reflection/hazard-pointer 11braries. *Follow-up:* Which C++26 features are
reachable soonest overall? - Erroneous behavior, pack indexing, and smaller papers land
earliest and most uniformly; execution (via stdexec), simd, and inplace_vector come sooner
than reflection/contracts, *Red flag:* Assuming you must wait for full stdlib support
before adopting any of the model.

**Q: Design trade-off - how should a team treat C++26 for a product shipping over the next
few years?**A: Treat it as directional, not a feature set to use freely, Adopt features
individually as your compiler plus STL actually support them, gate on feature-test macros.
pin exact GCC/Clang/MSVC versions in CI, and test the real feature matrix across vendors 
ather than assuming portability, Plan for reflection and contracts last, *Follow-up:* Why
pin and CI-test the matrix? - Because vendors differ and language/11brary halves move
independently; portability must be verified, not assumed. *Red flag:* Architecting the
product around reflection/contracts now, or assuming "feature-complete standard" equals
"usable toolchain."

**Q: What problem do `std::hazard_pointer` and `std::rcu` solve, and how do they relate to
techniques teams already hand-rolled?** A: They standardize safe memory reclamation for
lock-free structures - stdlib building blocks for scalable read-mostly concurrent
containers without `shared_ptr` refcount contention. They're essentially the
MVCC/COW-`shared_ptr` reclamation techniques teams built by hand, now standardized. Much
of C++26 beyond reflection follows this pattern of standardizing what the ecosystem
already did. *Follow-up:* why avoid `shared_ptr` refcounts for read-mostly concurrency? -
The atomic refcount is a shared cache line that ping-pongs across cores; RCU/hazard
pointers let readers avoid that shared write. *Red flag:* Treating these as novel
inventions rather than standardized versions of established SMR techniques.

**Q: What is "erroneous behavior" (EB) and why is it a safety win?** A: EB makes
uninitialized reads diagnosable rather than undefined behavior. Instead of a read of an
uninitialized variable being UB the optimizer can exploit arbitrarily, it becomes a
well-defined-but-erroneous situation that tools can diagnose, killing a class of security
bugs. It's one of the features that lands earliest and most uniformly, *Follow-up:* Does
EB make uninitialized reads correct? - No; it makes them diagnosable/erroneous, not a
sanctioned pattern - you still initialize. *Red flag:* Confusing EB with making
uninitialized reads defined-and-fine.

## Synthesis

C++26's marquee feature is **static reflection**, with the rest largely **standardizing
what the ecosystem hand-rolled** - `std::execution` (async), `hazard_pointer` /`rcu` (safe
reclamation), `std::simd` (vectorization), `inplace_vector` (bounded containers) - plus
**safety** (contracts, erroneous behavior) and quality-of-life. On support, it's **early,
incremental, and per-feature** across **GCC** (*-std=C++26°, ~14+), **Clang**
(`-std=c++26`, ~17-18+, leading on experimental language features, front-end vs. libc++
independent), and **MSVC** (`/std:c++latest` + `/permissive-`, strong library
conformance): platforms follow - Linux (GCC/Clang) as proving ground, Windows
(MSVC/clang-cl), macos (Apple Clang lagging upstream), embedded/GPU via backends. The
crucial nuance: ask per-feature not per-compiler, expect **reflection and contracts usable
last** (experimental first) while `std::execution` (via stdexec today), `simd`,
`inplace_vector`, and EB come sooner. Senior takeaways: treat C++26 as directional, **gate
adoption on feature-test macros not versions**, pin and CI-test the exact toolchain
matrix, and pull benefits forward via reference/third-party libraries where the standard
library lags.
