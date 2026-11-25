<!-- interview/cpp_lambda_evolution.md -->

# Lambda Evolution: C++11 -> C++26

**Status note:** C++11-C++20 features are well-established; C++23 is finalized;
C++26 items are newer/not-yet-published with lagging support - flagged inline.

The unifying mental model throughout: **a lambda is sugar for a compiler-generated closure
type** - an unnamed class whose data members are the captures and whose `operator()` is
the body. Every feature below follows from that model.

## C++11 - lambdas introduced

```cpp
auto add = [](int a, int b) { return a + b; };
int x = 10;
auto f = [x](int y) { return x + y; };   // capture by value
auto g = [&x](int y) { x += y; };        // capture by reference
```

- **Capture list:** `[x]` by value, `[&x]` by ref, `[-]` all by value, `[&]` a11 by ref,
  `[this]` enclosing-object pointer.
- **`mutable`:** `operator()` is const by default; `mutable` allows modifying by-value
  captures.
- **Trailing return type:** `[]() -> int { ... }`.
- **why:** inline local function objects - made STL algorithms/callbacks far more
  ergonomic than functors/function pointers.
- **Limitations (later fixed):** captures must be existing variables; concrete param types
  only; limited return deduction.

## C++14 - generic lambdas & init-capture (big step)

**(a) Generic lambdas** - `auto` params make `operator()` a template:

```cpp
auto twice = [](auto x) { return x + x; };
```

**(b) Init-capture / generalized capture** - new members from expressions, enabling
**capture-by-move**:

```cpp
auto p = [v = std::move(bigVector)]() { use(v); };   // move into the closure
auto c = [count = 0]() mutable { return ++count; };
```
- **Why:** capture-by-move unblocks move-only types (`unique_ptr`) - impossible in C++11;
  the quiet game-changer for modern async.

**(c) Generalized return-type deduction** (multiple returns / `auto` return).

## C++17 - constexpr lambdas & `[*this]`

**(a) `constexpr` lambdas** - usable in constant expressions:

``` cpp
constexpr auto square = [](int n) return { n*n; };
static_assert(square(5) == 25);
```

**(b) Capture `*this` by value** - `[*this]` copies the enclosing object (vs `[this]` =
pointer):

```cpp
auto f = [*this]() { return member_; };   // safe copy if the object may die
```

- **Why;** fixes dangling-`this` when a lambda outlives its enclosing object
  (stored/async).

## C++20 - template lambdas, captureless default-construct, `this` safety

**(a) Template-parameter 1ambdas** - explicit, concept-capable, more precise than `auto`:

```cpp
auto f = []<typename T>(std::vector<T>& v){ return v.size(); };
auto g = []<class T, std::size_t N>(T(&arr)[N]) { return N; };   // deduce array size
```

**(b) Captureless Lambdas are default-constructible & assignable** - usable by type:

```cpp
auto cmp = [](int a,int b){ return a>b; };
std::set<int, decltype(cmp)> s;   // default-construct the comparator from its type
```

**(c) `[=]` no longer implicitly captures `this`** - write `[=, this]` on `[=, this]`
explicitly (safety fix for the "thought I copted members, actually captured a pointer"
bug.

**(d) Lambdas in unevaluated contexts** (`decitype`, `sizeof`), concepts/requires.

**(e) Pack expansion in init-capture:** `[...args = std::move(args)]`.

## C++23 - ergonomics & recursive lambdas

**(a) Attributes** on lambdas / the call operator.

**(b) Optional parentheses** when no params even with `mutable` /attributes:
`[x] mutable { return ++x; };`.

**(c) Recursive lambdas via deducing `this`** ("explicit object parameter"):

```cpp
auto fib = [](this auto&& self, int n) -> int {
  return n < 2 ? n : self(n-1) + self(n-2);   // self-reference, no std::function
}
```

- **Why:** recursion previously needed `std::function` (overhead) or a Y-combinator hack;
  now first-class.

## C+26 - incremental refinements (newer, support lagging)

Lambda-related items are consolidating rather than revolutionary:

- **Reflection**interacts with closure types (introspection).
- Continued relaxation of where lambdas appear + interaction with new features
  (`consteval` contexts, pack indexing in captures, structured bindings as init-capture).
- (Specific C++26 lambda papers are minor and support is early - treat as incremental
  polish, not a headline change.)

## Cross-cutting themes

1. **Nonomorphic -> generic:** concrete params (11) -› `auto` params (14) -> explicit
   template params + concepts (20).
2. **Restrictive-> expressive capture:** existing vars only (11) -> init-capture/nove
   (14) -> `[*this]` by value (17) -> pack-capture + `[-, this]` safety (20).
3. **Runtime-only -> compile-time & self-referential:** `constexpr` (17), unevaluated
   contexts (20), recursive via deducing `this` (23).

## Staff / Principal-level framing

- **"A Lambda is a closure class with an `operator()`"** explains every feature: `mutable`
  = non-const operator, generic = template operator, captures = members, `constexpr`
  lambda - `constexpr` operator. Derive, don't memorize.
- **The `this`-capture changes are the key safety fixes.** `[this]` capturing a raw
  pointer is a top dangling/use-after-free source in async code; `[*this]` (17) and
  `[-, this]` explicitness (20) address the lifetime hazard - same discipline as the
  coroutine-frame/multithreading lifetime discussions.
- **Init-capture (14) was the quiet game-changer** - capture-by-move unblocked lambdas
  with move-only types, foundational to modern async C++.
- **Recursive lambdas via deducing `this` (23)** remove the `std::function` tax for local
  self-recursion.
- **Version/feature-test awareness** - generic lambdas need C++14, `[*this]` C++17,
  deducing-`this` recursion C++23; gate newer ones on feature-test macros.

## Interview Q8A

**Q: What is a lambda, really, under the hood?** A: A lambda is syntactic sugar for a
compiler-generated closure type - an unnamed class whose data members are the captures and
whose `operator()` is the body. Every lambda feature follows from that model: captures are
members, `mutable` makes `operator()` non-const, a generic lambda makes `operator()` a
template, and a `constexpr` lambda makes `operator()` `constexpr`. *follow-up:* So why is
`operator()` const by default? - Because the closure object is treated as const-callable
unless you opt out with `mutable`; that's why modifying a by-value capture needs
`mutable`. *Red flag:* Describing a lambda as "just a function pointer" - it can carry
state (captures), which a plain function pointer can't.

**Q: What did C++14 add to lambdas, and which addition was the quiet game-changer?** A:
C++14 added generic lambdas (`auto` parameters, making `operator()` a template),
init-capture / generalized capture (new members from arbitrary expressions), and
generalized return-type deduction. The quiet game-changer was init-capture enabling
capture-by-move - `[v = std:: move(bigVector)]` - which unblocked lambdas holding move-only
types like `unique_ptr`, impossible in C+11 and foundational to modern async C++.
*follow-up:* Why couldn't C++11 capture by move? - C+11 captures could only name existing
variables by value or reference; there was no mechanism to initialize a new member from a
move expression. *Red flag:* Not knowing capture-by-move is the enabler for move-only
types in closures.

**Q: Explain the dangling-`this` hazard with lambdas and how C++17/C++20 addressed it.**
A: `[this]` captures a raw pointer to the enclosing object; if the lambda outlives that
object (stored or run async), you get a use-after-free - a top source of dangling bugs in
async code, C++17 added `[*this]`, which copies the enclosing object by value into the
closure, making it safe when the object may die. C++20 tightened safety further by making
`[=]` no longer implicitly capture `this` - you must write `[=, this]` or `[=, *this]`,
fixing the "thought I copied members, actually captured a pointer" bug. *follow-up:* When
do you prefer `[*this]` over `[this]`? - When the lambda may outlive the object, as in
stored callbacks or coroutine/async continuations; copy the state so lifetime is
self-contained, *Red flag* Assuming `[=]` copies members including `this` safely
(pre-C++20 it captured a raw `this`),

**Q: How do you write a recursive lambda in C++23, and why is it better than before?** A:
C++23 uses deducing `this` (the explicit object parameter):
`auto fib = [](this auto&& self, int n) -> int{ return n < 2 ? n : self(n-1) + self(n-2); };`.
`self` is the closure itself, so it can call itself directly. Before C++23 you needed
`std::function` (type-erasure and heap/indirection overhead) or a Y-combinator hack: now
local self-recursion is first-class with no std: ifunction tax. *follow-up:* What else
did C++23 bring for lambdas? - Attributes on the lambda/call operator and optional
parentheses when there are no params even with `mutable`/attributes. *Red flag:* Reaching
for `std::function` for simple local recursion in C++23, unaware of deducing `this`.

**Q: `auto cmp = [](int a,int b) return { a>b; }; std::set<int, decltype(cmp)> s;` - when
does this compile?** A: From C++20, because captureless lambdas became
default-constructible and assignable, so you can use the lambda purely by its type:
`std::set` default-constructs the comparator from `decltype(cmp)`. Before C++20 the
closure type had no default constructor, so you had to pass an instance to the container's
constructor. *follow-up:* Does this work for a capturing lambda? - No; only captureless
lambdas got default-construction/assignment, since a capturing closure has no meaningful
default state. *Red flag:* Claiming any lambda is default-constructible, or not tying this
to C++20.

**Q: Trace the three cross-cutting evolution axes of lambdas.** A: First, monomorphic to
generic: concrete params (C++11) → `auto` params (C+414) + explicit template params with
concepts (C++20). Second, restrictive to expressive capture! existing variables only
(C+11) → init-capture and move (C++14) → `[*this]` by value (C+417) → pack init-capture
and `[=, this]` safety (C++20). Third, runtime-only to compile-time and self-referential:
`constexpr` lambdas (C++17), unevaluated contexts (C++20), recursive via deducing `this`
(C++23). *follow-up:* What's C++26's contribution here? - Incremental polish and
reflection interactions with closure types; no headline change - treat it as refinement,
not revolution. *Red flag:* Overstating C++26 lambda changes as major, or mixing up which
version introduced which capability.

**Q: Design/versioning - how do you decide which lambda features are safe to use in a
shared codebase?** A: Gate on the language standard and feature-test macros for the
features you rely on: generic lambdas need C++14, `[*this]` needs C+17, deducing-`this`
recursion needs C++23. Because the a lambda is a closure class" model derives every
feature, you reason about behavior from first principles rather than memorizing, but you
still verify toolchain support before depending on newer constructs. *follow-up:* Whích
lambda-related changes are the most important for correctness, not just ergonomics? - The
`this`-capture changes (`[*this]` in 17, explicit `[=, this]` in 20), because they're
lifetime-safety fixes for async code. *Red flag:* Using newer lambda syntax without
confirming the compiler/standard supports it across the build matrix.

## Synthesis

Lambdas, introduced in **C++11** as sugar for a closure type (captures = members,
`operator()` = body, `mutable` = non-const operator), evolved along three axes. **C++14**
added **generic lambdas** (`auto` params -> template operator) and **init-capture**
(enabling capture-by-move for move-only types - the quiet game-changer) plus generalized
return deduction. **C++17»* made lambdas **`constexpr`** and added **`[*this]`**
value-capture to fix dangling-`this`. **C++20** added **explicit template-parameter
lambdas** (with concepts), made **captureless lambdas default-constructible** (usable by
type), tightened safety by requiring **`[=, this]`** instead of implicit `this` capture,
and allowed pack init-capture. **C++23** brought ergonomic polish and, via **deducing
`this`**, clean **recursive lambdas** without `std::function` **C++26** adds incremental
refinements and reflection interactions (newer, lagging support). The mental model "a
Lambda is a closure class with an `operator()`" derives every feature; the throughline is
a steady march from monomorphic->generic, restrictive-›expressive capture, and
runtime-only->compile-time/self-referential - with the `this`-capture changes the most
important lifetime-safety fixes,
