<!-- Interview/cpp_mutable_keyword.md -->

# The `mutable` Keyword in C++

## What it is

`mutable` is a storage-class specifier for **non-static, non-const, non-reference data
members** of a class. It declares the member **can be modified even when the containing
object is `const` ** (through a const member function or a const reference/pointer). It is
the designed **escape hatch** from physical const-ness, used to implement *logical*
const-ness.

There is also a **second, unrelated use**: `mutable` on a **lambda** (C++11+), letting it
modify its by-value captures. Same keyword, different purpose - both covered below.

## Use 1 - "mutable' data members (logical vs. physical const)

The language enforces **physical (bitwise) const** - a const object can't have any member
modified. What you usually want is **logical const** - the object's *observable* state
doesn't change, even if internal bookkeeping does. `mutable` carves out the members that
are not part of observable state."

```cpp
class Widget {
    mutable std::mutex m_;              // locking isn't a logical mutation
    int value_;
public:
    int get() const {
        std::lock_guard lk(m_);         // constmethod...
        return value_;                  // ...yet locks the mutable mutex
    }
};
```

### Canonical use cases

**(a) Mutexes / synchronization.** A const getter still needs to lock; the mutex is
`mutable`. The most common, least controversial use - locking isn't a logical state
change.

**(b) Memoization / caching.** A const method lazily computes and caches; the cache is
`mutable` because filling it doesn't change what the object represents.

```cpp
class Matrix {
    mutable std::optional<double> cached_det_;
public:
    double determinant() const {
        if (!cached_det_) cached_det_ = compute_determinant(); // modify in const method
        return *cached_det_;
    }
};
```

**(c) Non-observable bookkeeping** - a `mutable` hit/access counter, last-access timestamp
(LRU), dirty flag.

**How:** prefix the member with `mutable`; it can then be written from const member
functions and through const references/pointers.

## Use 2 - `mutable` lambdas

A lambda's `operator()` is **const by default**, so by-value captures are read-only
inside. `mutable` makes `operator()` non-const, allowing modification of the *captured
copies* (not the originals):

```cpp
int x = 0;
auto f = [x]() mutable {return ++x; };   // modifles the CAPTURED COPY
f();  // 1
f();  // 2 (lambda's own x persists between calls)
// outer x is still 0
```

- Without `mutable`, `++x` wouldn't compile (can't modify a by-value capture in a const
  call operator).
- It mutates the lambda's **internal copy**, persistent across calls ( stateful function
  object); the outer vardable is unaffected.
- **Not needed** for by-reference captures (`[&x]`) - those modify the original and don't
  Involve the lambda's const-ness.

## When to use (and not)

**Use when:**

- A member is genulnely **not part of logical/observable state** and must be modiflable
  from const contexts: mutexes, caches, lazy values, debug/stat counters.
- You want a stateful lambda mutating its own by-value state.

**Don't use when:**

- Working around a design flaw - if a "const" method changes observable state via
  `mutable`, that's lying about constness (code smel1), `mutable` should *preserve* the
  const contract, not break it.
- A simple non-const method would be more honest. Reach for `mutable` only when the method
  *should* be const to callers.

## Traps & subtleties

- **Can't combine with `const`/`static`/references.** `mutable const int x;` is
  ill-formed; no `mutable` reference members. Non-const, non-reference, non-static data
  members only.
- **silently breaks `const`'s thread-safety guarantee if misused.** The standard library
  assumes const member functions are safe for concurrent calls (const = read-only = safe
  to share). A mutable member modified without synchronization in a const method
  **violates that** - data race when the const object is shared across threads. Hence
  `mutable` caches must be protected (a `mutable std::mutex`); a lockless `mutable` cache
  in a const getter is a classic concurrency bug. (Ties to the multithreading-hazards doc:
  `const` is a thread-safety contract, `mutable` can quietly break it.)
- **Defeats some optimizations** - the compiler can't assume a const object is fully
  immutable, so it may not fold/cache reads as aggressively. Minor but real.
- **Overuse erodes const-correctness** - each mutable member is a hole in the const
  guarantee.
- **`mutable` vs. `const_cast`:** `mutable` is the *correct, defined* way to allow
  modifying specific members in const contexts; `const_cast`-ing away const to modify a
  member is UB If the object is truly const. Prefer `mutable`.

## Staff / Principal-level framing

- **`mutable` is the implementation tool for the logical-vs-physical-const distinction.**
  Design the *interface* for logical const (what callers observe); use `mutable` to let
  the *implementation* keep internal state physical-const would forbid. Getting this split
  right makes const-correct, cache-friendly APIs.
- **The thread-safety contract is the hidden landmine.** Because `const` is assumed "safe
  to call concurrently," any `mutable` member touched in a const method must be
  synchronized or provably race-free. `mutable std::mutex` + `mutable` cache is the
  canonical correct pattern; a lone unsynchronized mutable cache is the canonical bug.
- **A precision tool, not a convenience.** Legitimate uses are narrow (synchronization,
  caching, non-observable bookkeeping, stateful lambdas). Reaching for it to silence the
  compiler usually means you're breaking the const contract - reconsider whether the
  method should be const at all.

## Interview Q&A

**Q: What does the `mutable` keyword do, and what can it be applied to?** A: On a data
member, `mutable` declares that the member can be modified even when the containing object
is `const`- through a const member function or a const reference/pointer. It applies only
to non-static, non-const, non-reference data members. It's the designed escape hatch from
physical const-ness, used to implement logical const-ness. There's a second, unrelated use
on lambdas. *Follow-up:* Can you write `mutable const int x;` or a `mutable` reference
member? - No; both are ill-formed. `mutable` is incompatible with `const`, `static`, and
reference members. *Red flag:* Thinking `mutable` applies to local variables or function
parameters, or that it works on references.

**Q: Distinguish logical const from physical const, and give the canonical `mutable` use
cases.** A: The language enforces physical (bitwise) const - a const object can't have any
member modified. Logical const is what you usually want: the object's observable state
doesn't change even if internal bookkeeping does. `mutable` marks the members that aren't
part of observable state. Canonical cases: mutexes/synchronization (locking isn't a
logical mutation), memoization/caches (lazily computing and caching doesn't change what
the object represents), and non-observable bookkeeping (hit counters, last-access
timestamps, dirty flags). *Follow-up:* Which use is the least controversial? - A
`mutable std::mutex` locked by a const getter; locking is clearly not a logical state
change. *Red flag:* Using mutable to change state that callers can actually observe -
that's lying about constness.

**Q: What does "mutable' do on a lambda, and when is it unnecessary?** A: A lambda's
`operator()` is const by default, so by-value captures are read-only inside. `mutable`
makes `operator()` non-const, letting the lambda modify its captured copies (not the
originals) - giving a stateful function object whose state persists across calls. It's
unnecessary for by-reference captures (`[&x]`), which modify the original and don't
involve the closure's const-ness. *Follow-up:*
`int x=0; auto=[x]() mutable { return ++x; }; f(); f();` - what's returned and what is
outer `x`? - Returns 1 then 2 (the lambda's internal copy persists); outer `x` is still 0.
*Red flag:* Believing a `mutable` lambda modifies the outer variable, or adding `mutable`
to a reference-capturing lambda thinking it's required.

**Q: Debugging scenario - a const getter caches a result in a `mutable` member without a
lock, and the program crashes intermittently under load. What's wrong?** A: `const` is an
implicit thread-safety contract: the standard library assumes const member functions are
safe for concurrent calls because const means read-only means safe to share, An
unsynchronized mutable" cache written inside a const method violates that assumption -
when the const object is shared across threads, concurrent const calls race on the cache,
which is a data race (UB), The fix is the canonical pattern: pair the `mutable` cache with
a `mutable std::mutex` and lock in the const method. *Follow-up:* Why is a lone
unsynchronized `mutable` cache called the canonical concurrency bug here? - Because it
silently breaks const's "safe to call concurrently" guarantee that callers and the stdlib
rely on. *Red flag:* Not recognizing that const implies a concurrent-read safety contract
that `mutable` can break.

**Q: Why prefer `mutable` over `const cast` for modifying a member in a const context?*
A: "mutable is the correct, well-defined mechanism to allow modifying specific members in
const contexts. `const_cast` -ing away const to modify a member is undefined behavior if
the underlying object is truly const (It may be in read-only memory or constant-folded).
`mutable` expresses the intent at the declaration and is always defined; `const_cast` is a
code smell signaling an upstream const-correctness flaw. *Follow-up:* Is there any
legitimate use of `const_cast` then? - Only removing const that was layered on a non-const
object (eg., legacy non-const-correct CAPIs), never to mutate a genuinely const object.
*Red flag:* Treating `mutable` and `const_cast`-to-modify-const as interchangeable.

**Q: When should you NOT reach for `mutable`?** A: When It would let a "const" method
change observable state - that's a lie about constness and a code smell. If a simple
non-const method would be more honest, use that. Reach for `mutable` only when the method
genuinely should be const to callers but needs to touch members that aren't part of
logical state. Overuse erodes const-correctness because each `mutable` member is a hole in
the const guarantee. *Follow-up:* What's the design principle? - Design the interface for
logical const (what callers observe) and use `mutable` so the implementation can keep
internal state physical const would forbid. *Red flag:* Sprinkling `mutable` to silence
compiler errors instead of fixing the const design.

**Q: Design trade-off - besides thread safety, what's the cost of using `mutable`?** A: It
defeats some optimizations: the compiler can no longer assume a const object is fully
immutable, so it may not fold or cache reads as aggressively - minor but real. More
importantly, each `mutable` member weakens the const contract and the reasoning that rests
on it (optimization, thread-safety, reader confidence). It's a precision tool for narrow
cases (synchronization, caching, non-observable bookkeeping, stateful lambdas), not a
general convenience. *Follow-up:* So what's the one-line principal-level framing? -
`mutable` is the correct, defined implementation tool for the logical-vs-physical-const
split, but a narrow one - misuse silently breaks const's concurrency guarantee. *Red
flag:* Ignoring that `mutable` has real optimization and reasoning costs, treating it as
free.

## Synthesis

`mutable` has two uses under one keyword. On a **data member**, it permits modification
even when the containing object is `const` - the designed escape hatch implementing
**logical const** (observable state unchanged) atop the language's **physical const**
(bitwise immutability); canonical cases are **mutexes, memoization/caches, and
non-observable bookkeeping** in const methods. On a **lambda**, it makes the call operator
non-const so the lambda can modify its **by-value captures** (its own persistent copies,
not the originals), Use it when a member is genuinely outside the object's logical state
(or for a stateful lambda); avoid it when it would let a "const" method change observable
state (a lie about constness), Sharpest trap: because `const` is an implicit
**thread-safety contract**, an unsynchronized `mutable` member modified in a const method
is a **data race** - pair `mutable` caches with a `mutable std::mutex`, Principal view:
`mutable` is the precise, defined implementation tool for the logical-vs-physical-const
split (unlike the UB-prone `const_cast`), but a narrow one - overuse erodes
const-correctness and misuse silently breaks const's concurrency guarantee.
