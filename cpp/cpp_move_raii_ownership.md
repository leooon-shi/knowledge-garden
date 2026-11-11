-- interview/cpp_move_raii_ownership.md -->
#C++ Value Semantics: Move, Perfect Forwarding, RAII, Exception Safety, and Smart-Pointer Ownership These topics are one subject seen from different angles: **who owns a resource, how ownership is transferred, and what happens when something throws mid-transfer. ** Related:
*std_string_register_abi.md" (why owning types are memory-passed),
•cow_strings_multicore.md" (refcount cost), "cpp_const_family.md".
# 1. Value categories (the foundation)
- **Ivalue** - has identity, can't be implicitly moved from (**, *p', "afil").
**prvalue** - pure temporary, no identity ('42, f() returning by value,
"TO").
**value** - has identity but is "expiring" (std: :move(x), static_cast<T&&>(x)").
- glvalue - Ivalue U avalue; rvalue • prvalue U value.
*  T&& binds to rvalues. **A named rvalue reference is itself an 1value** -
*  void f(T&& t) (g(t); ) passes an ivaue; you need std::move(t) to forward the rvalue-ness.
# 2. Move semantics
*std: :move
doesn't move.* It is static_cast<T&&›- a cast that makes overload
resolution pick the move constructor/assignment. The *move constructor* does the work! steal the source's resources and leave the source in a *valid but unspecified state** (destructible and assignable; for std types typically empty, but don't rely on it beyond what the type documents).
cpp
class Buffer {
std: :size_t n_ - e; char* P_ - nul1ptr;
public:
explicit Buffer(std: :size_t n) : n_(n), p_(new char[n]) 0)
~Buffer() { delete[] p_; }
Buffer(const Buffer& o) : n_(on_), P_(new char[on_]) { std:: mempy(P_, O.P_, n_); }
Buffer (Buffer&& o) noexcept
: n_(std::exchange(on_, e)), p_(std::exchange(o.P, nullptr)) (
Buffer& operator-(Buffer o) noexcept { swap(o); return *this; } // copy-and-swap, handles both void swap(Buffer& o) noexcept { std: :swap(n_, on_); std: :swap(P_, o-p_): }
### Rule of zero / three/five
-   **Rule of zero (preferred):** compose from RAII members (std: :vector, std::unique_ptr) and declare *no* special members.
-  **Rule of five:** if you write any of destructor / copy ctor / copy assign / move ctor / move assign, consider all five.
**Implicit generation trap:** declaring a destructor or copy operation **suppresses the implicit move operations** - moves silently become copies. Declaring a move operation deletes the implicit copies.
## "noexcept on moves is load-bearing
"std::vector reallocation uses std: move_if noexcept: if T's move constructor may throw (and "T is copyable), vector **copies** instead, to keep the strong exception guarantee. A missing noexcept turns every reallocation into deep copies. Check with
*static_assert(std::is_nothrow move_constructible_v<T>).
### Copy elision, RVO, NRVO
-  **C++17 guaranteed elision** for pralues: *T x - T(; return T); - no copy/move even exists (works for non-movable types).
-   **NRVO** (*T x; ..•; return x;°) is permitted, not guaranteed; if not elided,
*   return x; is treated as an rvalue first (implicit move; widened in CH20/23).
*  *Pessimization:** return std: :movex); disables NRVO. Don't. (Clang warns with
• -Wpessimizing-move,)
return of a function parameter can't be NRVO'd but is Implicitly moved.
### Moved-from pitfalls
- Using a moved-from object beyond assign/destroy (clang-tidy
•bugprone-use-after-move").
*   const T%; std; move (x) ylelds const T&&, which binds to the **copy** constructor - silently copies,
-   Moving out of a std: :initializer_11st is Impossible (elements are const) -
*   vector<unique ptrsT>›(} doesn't complle
-  Self-move-assignment must leave the object valid (copy-and-swap handles it),
#* 3. Perfect forwarding
•*•cpp
template <class... Args>
std::unique_ptr<T> make(Args&8... args) {
return std:: unique_ptrsT>(new T(std: iforward<Args>(args)...))
1
2
15
-**Forwarding (universal) reference;** •T&&" where T' is a *deduced template parameter* of that function (or "auto&&").
•std: vector<T›&&* or "T&&" in a class template's
non-template member is a plain rvalue reference.
- **Reference collapsing:** •& &,
*888
Ivalue deduces •T - U8*, so •T&& collapses to •ua.
*  **std:: forward<T>(x) ** casts back to rvalue only if the original argument was an rvalue. Forward each argument **at most once**.
*  *Failure cases: ** braced-init-lists (f({1,2}) can't deduce), 0/ NULL' as null pointer, overloaded function names, bit-fields, declaration-only
• static constexpr
members
(pre-C++17 ODR-use).

**Greedy-overload trap:** a forwarding constructor •template‹class U› XU&&) beats the copy constructor for non-const values "X". Constrain it: requires (Istd: sis_same_vestd: sremove_curef_t<u>, x›).
## 4. RAII
**Resource Acquisition Is Initialization:** bind a resource's lifetime to an object's lifetime - acquire in the constructor, release in the destructor. Because destructors run during **stack unwinding**, RAII makes cleanup automatic on every exit path (return, break, exception).
-   Applies to anything: memory (unique ptr"), locks ("std: :scoped_lock"), flles/sockets (custom wrapper), transactions (commit-or-rollback guard), coroutine handles.
-   **Scope guards:** *std: :experimental: iscope_exit* / "scope_fail" / "scope_success(Library Fundamentals TS v3), or a 10-1ine hand-rolled guard.
-   **Destructors are implicitly "noexcept,** Throwing out of one during unwinding calls
*  std: terminate. If release can fail (e.g. close() flushing), expose an explicit
*  close() that reports errors and make the destructor best-effort.
- Constructor failure: if a constructor throws, the destructor for *that* object doesn't run, but already-constructed **members and bases are destroyed**, So hold each resource in its own RAII member, never two raw resources in one class.
#* 5. Exception-safety guarantees (Abrahams)
| Guarantee
Example
I Promise
*  *No-throw** | Never throws
*  std::vector::pop_back l
I **Strong*»
| Commit-or-rollback: on throw, state unchanged
copyable) | | **Basic*s operations
1 (none)
I On throw, invariants hold, no leaks state may change most
1 Anything can happen
avoid
I destructors, "swap", move ops,
I vector: push_back" (if move is noexcept or T
-   **Copy-and-swap** gives the strong guarantee for assignment: do all throwing work on a copy, then commit with a no-throw "swap.
-   General recipe: *do everything that can throw first, off to the side; then commit using only no-throw operations.*
-   noexcept is part of the type system (C++17) and is a contract: violating it calls
*  std::terminate, without guaranteed unwinding.
-   Cost model: table-based zero-cost exceptions - no cost on the happy path, expensive throw (unwinder, RTTI matching). This is why
•std::expected (C+23) is used for
*  expected* failures, exceptions for exceptional ones. With "-fno-exceptions, much of this changes (no unwinding - RAII still works for normal exits).
*  * 6. Smart pointers and ownership
I Type Notes
I Ownership
I Size
1 std::unique_ptr<T, D› | Exclusive, move-only | 1 pointer (stateless deleter, via EBO) | Default choice; zero-overhead vs raw "new/delete (except it's memory-passed in the Itanium ABI - see "std_string_register_abi.md") | I std: :shared_ptr<T>*
I Shared, refcounted
12 pointers (object + control block)
thread-safe "control block", not thread-safe object*
[Atomic refcount ops;
"std: weak ptr<T›"
I Non-owning observer | 2 pointers
empty; breaks cycles
I lock)" → "shared_ptr or
1 T** / T&
| Non-owning
guaranteed by caller™
1-
I Fine for borrowed, Lifetime
#** The control block
Holds strong count, weak count, deleter, allocator (and with "make_shared", the object itself).
- **make_shared***; one allocation (object + block), better locality, Downside: object memory isn't freed until the last "weak_ptr" dies (block and object share an allocation),
*  ** shared ptreT>(new T) **; two allocations; the object is freed when strong count hits e1,
*  **make _unique /make_shared*** also avold the pre-C++17 Leak in changes, but st111 an interview favorite).
- The refcount release pattern inside:
if (strong.fetch_sub(1, memory_order_aca_rel) == 1) destroy(): - release so pric rates are visible to the destroying thread, acquire so the destroyer sees them. Se
* cpp_memory_model_atomics. md".
#*# Pitfalls
-   **Cycles leak: ** parent -> child both shared_ptr'. Make the back-edge "weak_ptr.
-   **Two control blocks for one object:** shared_ptr<T> a(p); shared_ptr<T> b(p); → double delete. Also shared_ptr<T>(this) Inside a member - use oundig shared are exists yet a. In the constructor).
* shared_from_this() (throws bad weak_ptr' if no
owning shared ptr exists yet, e•g•
- **Aliasing constructor:** • shared_ptr<Member> (owner, &owner->m)* shares ownership of
* owner while pointing at a member - useful, but surprising when the pointee outlives your expectation.
-   **Pass by what you mean:** *f(T&) / f(T*) 1f *f only uses the object;
*  f (unique_ptr<T>) to take ownership;
•f(shared _ptr<T>) by value to share ownership:

*  const shared_ptreT>& only if *f*might* copy it. Passing shared_ptr by value on hot paths pays two atomic RMWs per call.
-   *std: :atomic<std:: shared_ptr<T>>* (C++20) for atomically swapping the pointer itself: usually implemented with a lock or split refcount - not free.
-   Custom deleters: "unique_ptr<FILE, decitype(&fclose)> (or a stateless functor to keep it pointer-sized); "shared_ptr type-erases the deleter (no type change).
## Likely interview questions
-   *"What does "std: move" do?"* - A cast to value; the move constructor does the moving.
-   *"Why should move constructors be "noexcept p"* - "vector" reallocation copies otherwise (strong guarantee) -
-   *Is "return std: move(Local);" good?"* - No; it blocks NRVO.
-   *"Rvalue reference vs forwarding reference?"* - Forwarding requires deduction of "T in"T&&" (or "auto&&").
-   *"ImpLement a "unique_ptr" / write copy-and-swap / write a scope guard. "*
-   **make_shared vs "shared_ptr(new T)*?"* - One allocation vs two; weak_ptr keeps memory alive.
*  "Is "shared ptr" thread-safe?"* - Control block yes; the pointee and the same
*  shared_ptr" instance written concurrently, no.
*  "What happens if a destructor throws?"* - "noexcept by default -> "std::terminate"; during unwinding, always terminate.
- *State the three exception-safety guarantees and give an example of each."*
## Staff / Principal-level framing
- **Ownership is an API design decision, expressed in types.** "unique ptr° parameters,
*  & parameters, and shared_ptr" by value each say something different. Reviews should reject ambiguous ownership (raw owning pointers) outright.
*  **shared_ptr is a design smell when it's the default.** It hides lifetime and adds atomic traffic; most graphs have a clear owner. Reach for it only for genuinely shared, non-hierarchical lifetimes.
-***noexcept and rule-of-zero are performance features**, not just style: they decide whether containers move or copy.
**Exception safety is about invariants**, not try/catch. The "compute aside, conmit with no-throw ops" discipline also applies to transactional systems, config reloads, and lock-free publication,
## Synthesis
Move semantics transfers ownership cheaply: std: move" is a cast, the move constructor steals and leaves a valid-but-unspecified source, and **noexcept moves** are what let containers move rather than copy, *Perfect forwarding** (forwarding references + reference collapsing "std; forward) preserves value category through generie code.
**RAII** ties resource lifetime to scope so stack unwinding cleans up on every path, and the **no-throw / strong / basic guarantees** describe what state survives an exception - copy-and-swap and "commit with no-throw ops" are the standard tools, **Smart pointers* encode ownership in types: "unique_ptn" by default, shared_ptn" for truly shared lifetimes (control block, atomic refcount, "make_shared trade-offs), "weak_ptn' to break cycles and observe, enable_shared_from_this" to avoid duplicate control blocks.