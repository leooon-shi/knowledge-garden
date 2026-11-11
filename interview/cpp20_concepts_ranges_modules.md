<!-- interview/ cpp20_concepts_ranges_modules.md -->

# C++20 Concepts, Ranges, and Modules

The three large C++20 features that change how generic code and builds are written.
Related: `template_name_lookup_ast_adl.md` (ADL and customization points),
`cpp_lambda_evolution.md`, `cpp26_features.md`.

## 1. Concepts and constraints

## What they replace

Pre-C++20 constraints used **SFINAE** (`std::enable_if_t<...>`, `decitype(expr, vold())`,
detection idiom). Problems: unreadable signatures, errors deep inside instantiation, no
ordering between overloads beyond hacks (tag dispatch).

A **concept** is a named compile-time predicate on template arguments; a
**requires-clause** attaches it to a template.

```cpp
template <class T>
concept Hashable = requires(const T& a) {
  { std::hashe<T>{}(a) } -> std::convertible_to<std::size_t>;   // compound requirement
};

template <class T>
concept Container = requires(T c) {
  typename T::value_type;           // type requirement
  c.begin(); c.end();               // simple requirements
  requires std::is_default_constructible_v<T>;   // nested requirement
}

// Four equivalent spellings
template <Hashable T> void f(T);
template <class T> requires Hashable<T> void f(T);
template <class T> void f(T) requires Hashable<T>;
void f(Hashable auto x);                // abbreviated function template
```

### Subsumption - overload ordering by constraints

When two overloads both match, the one whose constraints **subsume** the other's
(logically imply them, by normalized conjunction/disjunction of *the same named concepts*)
is more specialized:

```cpp
template <std::input_iterator I>         void adv(I&, int);   // general
template <std::random_access_iterator I> void adv(I&, int);   // chosen for vector iterators
```

- Subsumption works only through **named concepts*. Two identical-looking raw `requires`
  expressions (or `std::is_integral_v<T> && X`) aren't compared structurally - you get
  ambiguity. Wrap predicates in concepts if you need ordering.
- Replaces tag dispatch (`iterator_category`) for most uses.

### Standard concepts worth knowing

`std::same_as`, `derived_from`, `convertible_to`, `integral`, `floating_point`, `regular`,
`totally_ordered`, `invocable`, `predicate`; iterator concepts `input_iterator`
`contiguous_iterator`; range concepts `range`, `sized_range`, `view`, `viewable_range`.

## Pitfalls

- **Syntactic, not semantic.** A concept checks that expressions compile, not that they
  mean what you expect (`std:regular` can't verify that `==` is an equivalence relation).
  Semantic requirements are documented, not checked.
- **Satisfaction is cached per (concept, args)** - a concept that depends on something
  completed later in the TU can give inconsistent results (111-formed, no diagnostic
  required).
- `requires requires (T x) { x + x; }` - the first `requires` starts a clause, the second
  an expression, Legal, usually a sign you should name a concept.
  Constrain the **forwarding constructor** trap (see `cpp_move_rali_owmership.md`):
  `requires (!std::same_as<std::remove_cvref_t<U>, x>)`.
- Error messages are better but not magic; Clang/CC show the unsatisfied atomic
  constraint, `static_assert(Concept<T>)` near the type definition gives an early, local
  error.
- **Concepts vs, virtual interfaces:** concepts = static polymorphism (zero runtime cost,
  code bloat, no heterogeneous containers); virtual = runtime polymorphism. Type erasure
  (`std::function`, `std:any`, `std::move_only_function`) bridges them.

## 2, Ranges

### The model

A **range** is anything with `ranges::begin`/`ranges::end` (the end may be a **sentinel**
of a different type - e.g. a null terminator or a count), **Views** are lightweight ranges
with O(1) copy/move (typically non-owning or owning a cheap thing) and **lazy**
evaluation.

```cpp
#include <ranges>
auto evens_squared = v
    | std::views::filter([](int x) { return x % 2 == 0; })
    | std::views::transform([](int x) { return x * x; })
    | std::views::take(10);
for (int x: evens_squared) { /* computed on demand */ }

std::ranges::sort(v);                        // no begin/end pair
std::ranges::sort(people, {}, &Person::age); // projection
auto it = std::ranges::find(v, 42);
```

- **Range algorithms** (`std::ranges::sort`, `find`, ...) take ranges, accept
  **projections**, are constrained with concepts, and return richer results
  (`in_out_result`, the end iterator).
- **Adaptors:** `filter`, `transform`, `take`, `drop`, `take_while`, `reverse`,
  `keys`/`values`/`elements`, `split`, `join`, `iota`; C++23 adds `zip`, `enumerate`,
  `chunk`, `slide`, `stride`, `cartesian_product`, `as_rvalue`, and
  **`std::anges::to<Container>()`** to materialize.
- **Niebloids / CPOs:** `std::ranges::begin`, `swap`, `size` are function objects. That
  disables ADL on the call itself and lets them do a controlled customization lookup
  (member, then ADL with "poison pill" overloads) - see `template_name_lookup_ast_adl.md`.

### Pitfalls interviewers like

- **Dangling:** `auto it = std::ranges::find(get_vector(), 1);` returns
  `std::ranges::dangling` (a compile error when used) because the argument was an rvalue
  non-borrowed range. `std::string_view`/`std::span` are **borrowed ranges** and don't
  dangle this way.
- **`views::filter` caches `begin()`** for amortized O(1) - so it's not `const`-iterable,
  and mutating elements so they no longer satisfy the predicate while iterating is UB.
- **Laziness Surprises:** `transform` re-runs the function each time an element is
  dereferenced; `filter | transform` can call the lambda more than expected. Side effects
  in view lambdas are a bug magnet.
- **Views hold references**: returning `v | views::filter(...)` from a function where `v`
  is local dangles. Use `views::owning`-style (pass the container by value into
  the pipeline, C++23 `owning_view`) or materialize with `ranges::to`.
- **Performance:** pipelines usually optimize to the hand-written loop at `-O2`, but deep
  stacks of adaptors can defeat vectorization and blow up compile times and debug builds.
- `std::views::iota(0, n)` where `n` is `size_t` and `e` is `int` - mismatched types fail
  to compile; use `0uz` (C++23) or `size_t{0}`.

## 3. Modules

### Why

Headers are textual inclusion: each TU re-parses `<vector>` etc. (often 160k+ lines),
macros leak, include order matters, and ODR violations hide easily. Modules compile an
interface **once** into a **BMI** (binary module interface - `.pcm` for Clang, `.gcm` for
GCC, `.ifc` for MSVC) that importers load.

```cpp
// math.cppm - primary module interface unit
module;                       // global module fragment: legacy #includes go here
#include <cmath>
export module math;           // module declaration
export double hypot2(double a, double b) { return std::sqrt(a*a + b*b); }
double helper();              // not exported: invisible to importers (module linkage)

// main.cpp
import math;
import std;                   // C++23 standard library module
int main() { std::println("{}", hypot2(3, 4)); }
```

- **Units:** primary interface (`export module M;`), **partitions**
  (`export module M:part;`, imported as `import :part;`), implementation units
  (`module M;`), and **header units** (`import <vector>;` - compiled headers, partial
  compatibility path).
- **Macros don't cross `import`** (except via header units). Non-exported names have
  **module linkage** - not visible, no ODR clash.
- **Reachability vs visibility:** a non-exported type may still be *reachable* (usable via
  `auto`) if returned from an exported function, but can't be named.

### The build-system problem (the real interview topic)

- Module dependencies must be discovered **before** compilation (you can't compile
  `main.cpp` until `math`'s BMI exists), Compilers emit **P1689** dependency JSON
  (`clang-scan-deps`, `-fdeps-format-p1689r5` in GCC); **CMake ≥ 3,28** with Ninja
  supports `FILE_SET CXX_MODULES`, and `import std;` support arrived around Make 3.30
  (experimental flag),
- **BMIs are not portable**; tied to the exact compiler, version, and often flags -
  they're build artifacts, not something you ship, Libraries ship module *interface
  sources*,
- Parallelism changes: a module DAG sertalizes some compilation that headers allowed in
  parallel; the win is avoiding repeated parsing. Typical reported gains: large for
  `import std;` vs including many std headers, more modest for project code.
- **Adoption status (late 2026):** usable with recent Clang/MSVC and GCC 15+
  (`import std;` in libstdc++ from GCC 15), CMake + Ninja. Still rough with tooling
  (clangd, some static analyzers), mixed header/module codebases, and packaging. Conmmon
  advice: modularize leaf libraries or use `import std;` first; don't big-bang convert.

### Modules vs PCH vs unity builds

|                  | Precompiled headers   | Unity/jumbo builds                         | Modules                        |
| ---------------- | --------------------- | ------------------------------------------ | ------------------------------ |
| Semantics change | None                  | Yes (shared TU: static/anon-ns spllisions) | Yes (isolation, no macro leak) |
| Granularity      | One prefix per target | Batches of TUs                             | Per module                     |
| Standard         | No                    | No                                         | Yes                            |

## Likely interview questions

- *"Rewrite this `enable_if` overload set with concepts."*
- *"How does the compiler choose between two constrained overloads?"* - Subsumption over
  normalized named-concept constraints.
- *"Are concepts checked semantically?"* - No.
- *"why does `ranges::find` on a temporary vector not compile when used?"* - `dangling`.
- *"why are views Lazy, and what can go wrong?"* - Re-evaluation, cached `begin`, dangling
  references.
- *"Why are `std::ranges::begin` etc. objects?"* - Disable ADL on the call; controlled
  customization.
- *"What problem do modules solve, and why has adoption been slow?"* -
  Parse-once/isolation; dependency scanning, BMI non-portability, tooling.
  
## Synthesis

**Concepts** turn template requirements into named, checkable predicates, giving readable
constraints, earlier errors, and overload ordering by **subsumption** (only through named
concepts) - but they are syntactic checks, not semantic proofs. **Ranges** add
range-based, projection-aware algorithms and **lazy, composable views**，with
sentinel-based ends and niebloid customization points; the pitfalls are dangling
(temporaries and returned views), filter's cached `begin`, and repeated evaluation in lazy
pipelines. **Modules** replace textual inclusion with compiled interfaces (BMIS),
isolating macros and non-exported names; the language part is straightforward, and the
hard part is the build: dependency scanning (P1689), compiler-specific BMIs, and tooling
maturity - so adoption is incremental, typically starting with `import std;`.
