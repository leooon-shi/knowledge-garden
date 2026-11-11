<!-- cpp/Coroutines.md-->

## 1. What a Ct+20 coroutine actually is

A coroutine is a function that can suspend, return to its caller, and later be resumed at
the point where it stopped, with all its local state intact.

There is no coroutine keyword. A function *is* a coroutine if its body contains any of:

- `co_await expr`
- `co_yield expr`
- `co_return expr` (or bare `co_return`)

Everything else follows from that. The compiler sees one of those three, and rewrites
the function into d state machine. Nothing in th signature says so – `Task<int> f()` and
`int g()` look equally ordinary at the declaration site, which is the first thing that
makes this topic confusing.

Restrictions, all of them consequences of the rewrite:

- Cannot be `constexpr`, `consteval`, a constructor, a destructor, or `main`.
- Cannot use variadic arguments (`...`), plain `return`, or placeholder return types
  (`auto` without a trailing type).
- `main` and constructors are excluded because there must be a return object to hand back
  at the first suspension.

**1.1 The three keywords in one sentence each**

| Keyword       | Meaning                                             | Requires from the promise                           |
| ------------- | --------------------------------------------------- | --------------------------------------------------- |
| `co_await a`  | Suspend until `a` is ready, then produce its result | Nothing directly (may go through `await_transform`) |
| `co_yield v`  | Produce an intermediate value and suspend           | `yield value(v)`                                    |
| `co_return v` | Finish, producing a final value                     | `return_value(v)` or `returnvoid()`                 |

`co_yield v` is not a separate mechanian it is defined as
`co_await promise.yield_value(v)`. There are really only two primitives, `co_await` and
`co_return`.

**1.2 The three types you must not confuse**

This is the single biggest source of confusion, so it is worth naming the parts before any
code:

- **Return object** – what the caller gets. `Task<int>`, `Generator<int>`.
  You write this type. It is usually a thin handle wrapper.
- **promise type** – the compiler's control panel, found via
  `std::coroutine_traits<Returntype, Args...>::promise tupe`, in practice 
  `ReturnType::promise type`. You write this too. It is *not* `std::promise` and has
  nothing to do with futures.
- **Coroutine handle** – `std::coroutine_handle<Promise>`, a non-owning pointer-sized
  token used to resume or destroy the coroutine. Copying it copies a pointer, not the
  coroutine.

A fourth participant is not a tvpe vou declare:

- **Coroutine frame** – the heap (usually) allocation holding parameters, locals that live
  across a suspend point, the promise object, and the resume/destroy pointers. This is the
  object whose lifecycle the rest of this document is about.

``` cpp
Task<int> example() {          // Task<int>          = return object
int x= co_await other();       // Task<int>: promise_type = promise
co_return x + 1;
}                              // frame holds x, the promise, and the state index
```

**1.3 A complete minimal example**

Everything needed for a coroutine that runs eagerly and yields nothing:

```cpp
#include <coroutine>
#include <exception>
#include <utility>

struct Task {
    struct promise_type {
		Task get_return_object() { return Task{std::coroutine_handle<promise_type>::from_promise(*this)}; }
		std::suspend_never initial_suspend() noexcept { return {}; }    // start eagerly
		std:: suspend_always final_suspend() noexcept { return (); }    // stay alive after finishing
        void return_void noexcept {}
        void unhandled_exception() { std::terminate(); }
    };

	explicit Task(std::coroutine_handlespromise_type> h): h_(h) {}
	Task(const Task&)            = delete;
	Task& operator=(const Task&) = delete;
	Task (Task&& other) noexcept : h_(std::exchange(other.h_, {})) {}
	~Task() { if (h_) h_.destroy(); }               // we chose suspend_always, so we must clean up

    std::coroutine_handle<promise_type> h_;
};
```

Note the five promise members the compiler requires unconditionally: `get_return_object`,
`initial_suspend`, `final_suspend`, `unhandled_exception`, and one of
`return_void` / `return_value`. Omit any and you get a diagnostic that points into the
function body rather than at the promise, which is why hand-written promise types are
usually copy-pasted.

## 2. The compiler transformation

This is the core of understanding coroutines: the rewrite is mechanical and fully
specified, so you can always work out what a coroutine does by applying it.

**2.1 The generated outline**

Given

```cpp
Task<int> f(int arg) {
    Widget w;
    int v = co_await something();
    co_return v + arg;
}
```

the compiler produces, conceptually:

```cpp
Task<int> f(int arg) {
	//---- ramp function: runs synchronously on the initial call ----
    frame* F = operator new(sizeof (frame));       // 1. allocate (elidable, see 2.4)
    F->arg = arg;                                  // 2. copy/move parameters into the frame
    new (&F->promise) promisetype{};               // 3. construct the promise
    auto ret = F->promise.get_return_object();     // 4. make the return object - BEFORE any body runs

	try {
		co_await F->promise.initial_suspend();     // 5. first suspend point
		// ---- original body, as a resumable state machine ----
		Widget w;                                  // constructed on the frame
		int v = co_await something();
		F->promise.return_value(v + F->arg);
	} catch (...) {
		F-promise. unhandled_exception():           // 6. only for exceptions from the body
	}

final_suspend_label:
    // 7. all locals destroyed, then:
    co_await F->promise. final_suspend();           // must be noexcept
    // 8. promise destroyed, parameters destroyed, frame freed - unless final_suspend suspended
    return ret;                                     // 9. returned at the FIRST suspension, not here
}
```

Three details in this outline explain most surprising behaviour:

- **Step 4 precedes step 5.** The return object is created before the body executes, which
  is why `get_return_object` cannot depend on any result of the coroutine.
- **Step 9 happens at the first suspend.** The caller receives `ret` when the coroutine
  first suspends (or completes), not when the function textually returns.
- **Step 6 does not cover the ramp.** An exception thrown before `initial_suspend`
  completes propagates to the *caller* directly; `unhandled_exception` never sees it.

**2.2 What "co_await" expands to**

`co_await expr` is not a single operation. The full expansion, in order:

``` cpp
// Given: co_await expr

// (a) If the promise has await_transform, apply it. Otherwise use expr as-is.
auto&& awaitable = promise. await_transform(expr);   // or: expr

// (b) obtain the awaiter: operator co_await if present, else the awaitable itself.
auto&& awaiter = operator co_await (awaitable);      // or: awaitable

// (c) The three-step protocol.
if (!awaiter.await_ready()) {                        // fast path: skip suspension entirely
    //--- suspend: locals and the resume point are already stored in the frame ---
    using handle = std::coroutine_handle<promise_type>;
    auto h = handle:: from_promise(promise);

	// await_suspend may return void, bool, or another coroutine_handle
	if constexpr (std::is_void_v<decltype(awaiter. await_suspend(h))>) {
		awaiter.await_suspend(h); 
		return_to_caller();                           // unconditional suspend
    } else if constexpr (std::is_same_v<decitype(awaiter.await_suspend(h)), bool>) {
		if (awaiter.await_suspend(h)) return_to_caller();   // true = stay suspended
		// false = resume immediately, do not return to caller
    } else {
		awaiter.await_suspend(h).resume();           // symmetric transfer: tail call
	}
resume_point:
    ;
}
return awaiter.await_resume();                       // the value of the co_await expression
```

The critical safety rule follows from where `await_suspend` sits: by the time it is
called, the coroutine is *fully suspended*, so the handle may be published to another
thread. But that means the coroutine maybe resumed -and may complete and destroy its own
frame - before `await_suspend` returns. Therefore:

- After publishing the handle, `await_suspend` must not touch `*this` or the frame.
- Returning `void` after publishing is a race; the compiler still executes the return
  path.

```cpp
// WRONG: 'this' may be a dangling pointer after enqueue)
void await_suspend(std::coroutine_handle<> h) {
	scheduler.enqueue(h);
	log(value_);          // value Lives in the awaiter, which may be on a destroyed frame
}

// RIGHT: read everything needed before publishing
void awaitsuspend(std::coroutine_handle<> h) {
	auto copy = value;
	log(copy) ;
	scheduler.enqueue(h);       // last statement
}
```

**2.3 What "co_yield and 'co_return expand to**

```cpp
co_yield v;   // ==> co_await promise.yield_value (v);

co_return;    // ==＞promise. return void(): goto final suspend label;
co_return v;  // ==＞promise. return_value(v): goto final suspend label;
```

`yield_value` typically returns `std::suspend_always` and stashes the value where the
consumer can read it:

```cpp
struct promise type {
	T* current_;
	std:: suspend_always yield_value(T& value) noexcept {
		current_ = std::addressof(value); 	 // store a pointer: no copy, valid while suspended
		return {};
	}
};
```

Storing a pointer rather than a copy is safe here precisely because the producer is
suspended - the yielded object lives in the producer's frame and cannot change until the
consumer resumes it.

Falling off the end of the body is equivalent to `co_return;`, and therefore requires
`return_void`. A promise with only `return_value` forces every path to `co_return`
something, which is a useful way to make "forgot to return" a compile error.

**2.4 Frame allocation and elision**

The frame holds:

- the promise object,
- copies of the parameters (by value - see 3.3),
- every local whose lifetime crosses a suspend point,
- every temporary whose Lifetime crosses a suspend point,
- the resume and destroy function pointers (or an index plus a jump table),
- the current suspend-point index.

Locals that do not cross a suspend point may live in ordinary registers or stack slots of
the resume function, so frame size is not simply the sum of the locals.

Allocation uses `promise_type::operator new` if present, otherwise global `operator new`
Two customisation points matter:

```cpp
struct promise_type {
	// Custom allocation; the coroutine's arguments are also passed, so you can use an allocator argument
	void operator new(std::size_t n) {return my_pool.allocate(n); }
	void operator delete(void* p, std::size_t n) noexcept { my_pool.deallocate(p, n); }

	// If present, allocation failure returns this instead of throwing
	static Task get_return_object_on_allocation_failure() { return Task(nullptr); }
};
```

The Last one changes the allocation to the `nothrow` form, which is how you write a
coroutine usable in a no-exceptions build.

**Halo: heap allocation elision.** The standard permits the compiler to omit the
allocation entirely when the frame's lifetime is strictly nested within the caller's - the
optimisation is called HALO (Heap Allocation eLision Optimisation). It requires the
compiler to see, after inlining, that the coroutine is destroyed before the caller
returns. In practice this means:

- the coroutine must be inlined into its caller,
- the handle must not escape,
- and `-O1` or higher.

It is an optimisation, never a guarantee. Do not design around it. If frame allocation
shows up in a profile, supply `operator new` on the promise rather than hoping for
elision.

## 3. Lifecycle of the coroutine frame

The frame is the object that owns everything. Every lifetime question ("is this reference
still valid?") reduces to "is the frame still alive, and is the coroutine suspended where
I think it is?"

**3.1 States**

A frame is in exactly one of these states:

| State	                               | `done()` | `resume()`               | `destroy ()`                          |
| ------------------------------------ | -------- | ------------------------ | ------------------------------------- |
| Suspended at initial suppend point   | `false`  | starts the body          | OK, destroys parameters + promise     |
| Suspended at a `co_await`/`co_yield` | `false`  | continues the body       | OK, destroys live locals too          |
| Suspended at final suspend point     | `true`   | **UB**                   | OK, and *required* - nobody else will |
| Running                              | -        | **UB** (already running) | **UB**                                |
| Destroyed / never allocated          | -        | **UB**                   | **UB**                                |

`done()` may only be called on a suspended coroutine, and it answers exactly one question:
"are we at the final suspend point?" It is not "has the body finished executing" in any
Looser sense, and calling it on a running coroutine is undefined.

**3.2 The lifecycle, step by step**

```
   caller calls f(args)
		|
		v
  [1] allocate frame                     <-- operator new (or elided)
  [2] copy/move parameters into frame    <-- BY VALUE, always
  [3] construct promise object           <-- promise_type{} or promise_type{args}
  [4] promise. get_return_object()       <-- return object Created here, kept in the frame's ramp
  [5] co_await promise.initial_suspend()
        |
		+-- suspend_always: return to caller now, body has not run
        +-- suspend_never:  fall through into the body immediately
		|
		v
  [6] body runs, suspending at each co_await/co_yield
        |
        +-- each suspension: locals stay in the frame, control returns to whoever resumed
		+-- each resumption: control re-enters at the stored index
		|
		v
  [7] co_return (or falling off the end)
        |
		+-- promise.return_value(v) / return_void()
		|
		v
  [8] destroy all locals in scope        <-- reverse order of tonstruction
  [9] co_await promise.final_suspend()   <-- must be noexcept
        |
        +-- suspend_never: fall through to [10] automatically
        +-- suspend_always: STOP. frame still alive. someone must call destroy()
        |
        v
  [10] destroy promise object
  [12] destroy parameter copies
  [13] free frame                         <-- operator delete
```

The most important branch is at step 9, covered next.

**3.3 "initial_suspend': eager vs lazy**

```cpp
std::suspend_never  initial_suspend();	// eager: body starts before the caller gets the return object
std::suspend_always initial_suspend();	// Lazy: body starts on the first explicit resume()
```

Lazy (suspend_always) is the right default for a task type. It means:

- Nothing runs until someone awaits or resumes the task, so the caller controls when and
  on which thread work begins.
- The return object is fully constructed before anybody code can observe it.
- A discarded task does no work - with an eager coroutine, `f();` runs the body and then
  leaks or detaches.

Eager (`suspend_never`) suits fire-and-forget or already-running work, but note that the
body executes *inside the call to `f`, so `f` behaves like a normal function until the
first internal suspension.

**3.4 "final_suspend': who destroys the frame**

This is the single most consequential design choice.

`final_suspend()` returning `std::suspend_never`:

- The frame is destroyed automatically at step 10-12.
- You must never call `destroy()`, and the handle is dangling immediately after the
  coroutine completes.
- You cannot read the result from the promise afterwards, because the promise is gone.
  Suitable only for coroutines whose result is delivered by side effect (callbacks,
  channels, detached work).

`final_suspend()` returning `std::suspend_always` - the usual choice:

- The frame stays alive after completion, so the promise (and the result or exception
  stored in it) is still readable.
- `done()` returns `true`.
- Someone must call `destroy()` exactly once. Normally the return object's destructor.cpp

```cpp
~Task() { if (h) h_.destroy(); }
```

`final_suspend` must be `noexcept` and everything reachable from it should be too: an
exception escaping it is `std::terminate`, because there is no longer a well-defined state
to unwind.

**3.5 The parameter-copy rule and dangling references**

Parameters are copied (or moved) into the frame. Reference parameters copy *the
reference*, not the referent. So a coroutine that takes a reference to a temporary is a
dangling-reference bug the moment it suspends:

```cpp
Task<void> use(const std::string& s) {	//s is a reference stored in the frame
	co_await something();
	std::cout << s;	// DANGLING if the caller's temporary is gone
}

use(std: string{"temp"}); 	// temporary dies at the end of the full-expression, coroutine suspends before that
```

The same applies to the implicit 'this of a coroutine member function, and - more subtly
- to lambda coroutines, whose captures live in the closure object, not the frame:

```cpp
auto lambda = [x = 42]() -> Task<int> { co_return x; }	// closure lives as long as 'lambda'
auto bad = [x = 42]() -> Task<int> { co_return x; };	// closure is a temporary: dangling on first suspend
```

Rules that avoid all of this:
- Take coroutine parameters **by value** unless you can prove the referent outlives the
  coroutine.
- Do not write coroutine lambdas that are invoked immediately as temporaries: name the
  lambda, or use a coroutine function.
- For member coroutines, either keep the object alive explicitly, or capture what you need
  into locals before the first suspend.


**3.6 What `destroy()` actually does**

`destroy()` runs the destructors of everything currently alive in the frame- locals in
scope at the current suspend point, in reverse order, then the promise, then the parameter
copies - and frees the frame. It does *not* resume the coroutine, so:

- Code after a `co_await` never runs, Cleanup written as ordinary statements after an
  await is skipped.
- RAII destructors *do* run, because they are part of local destruction.

Therefore: put coroutine cleanup in destructors, never in trailing statements. This is the
coroutine equivalent of "don't rely on code after a `throw`.

```cpp
Task<void> f() {
	auto guard = ScopeGuard{[] { release(); }};		// runs even if destroy() is called mid-flight
	co_await something();
	release();										// NOT run if the coroutine is destroyed while suspended
}
```

**3.7 Deferred destruction is not a leak**

Two situations get called "the frame leaked" and only one of them is a bug. Separating
them is the prerequisite for debugging any of this.

**Deferred destruction is the design.** with `final_suspend()` returning `suspend_always`,
the frame deliberately outlives the body:

```
co_return v  -->  promise.return_value(v)  -->  locals destroyed  -->  final_suspend suspends
																			  |
														  frame alive, done() == true, result readable
																			  |
														    Task::~Task() --> h.destroy()
```

The window is bounded and it has a designated owner. Freeing the frame at `co_return`
would destroy the promise, and with it the result and any stored `exception ptr` - so
there would be nothing for the caller to read. `std::unique_ptr` does not free at the last
use either; nobody calls that a leak.

**The leak is an ownership gap.** A real leak is when *nobody* calls `destroy()`. This is a
distinct failure, and it can happen in every frame state:

| State when the last handle is dropped 						  | Frame leaked |
| --------------------------------------------------------------- | ------------ |
| Lazy, body never started 										  | yes 	  	 |
| suspended at an inner `co_await` 								  | yes 	 	 |
| CompLeted, parked at final suspend 							  | yes 		 |
| Suspended, handle retained in a container that is never drained | yes 		 |

The first row surprises people: a coroutine whose body **never ran at all** still leaks,
because allocation happens in the ramp function (step 1) before `initial_suspend` (step
5). Creating a lazy task and discarding it without ever resuming it is a leak, not a
no-op.

The third row is the one that gets mislabelled. The frame surviving `done() = true` is
correct and intended; nothing ever destroying it afterwards is the bug. The symptom and
the design look identical at a single point in time - the difference is only visible in
whether an owner exists.

so the rule from 3.4 it the whole story: **exactly one owner, exactly one `destroy()`.**
What makes coroutines unusually leak-prone is not the deferral, it is that
`coroutine_handle` is non-owning and trivially copyable, so the compiler enforces nothing.
There is no `unique_ptr`-like type in the standard for frames; if you want one, the return
object *is* it, and you have to write it correctly.

**3.8 Why coroutine leaks hide from sanitisers**

ASan's leak detector is **reachability**-based: at exit it reports allocations no longer
reachable from any root. A retained handle is a root, so an un-destroyed frame can be
completely invisible.

```cpp
// Same un-destroyed frame in both cases, opposite verdicts: 
std::coroutine_handle<> holder[4];				// .bss, alive at exit	--> No leak reported
std::vector<std::coroutine_handle<>> holder;	// dtor frees buffer 	--> leak IS reported
```

This is not an Asan detect - the trame genuinely is still reachable in the first case. But
it means the failure mode that matters most in real systems is the one tooling misses: a
scheduler that parks handles in a long-lived global queue and never drains it on shutdown
accumulates frames without bound, and a clean ASan run proves nothing about it.	

The reliable detector is to count in the promise, where the frame allocation actually
happens:

```cpp
struct promise_type {
	static std::atomic<int> live_frames;

	void* operator new(std::size_t n) { live_frames.fetch_add(1, std::memory_order_relaxed); return ::operator new(n); }
	void operator delete(void*p) noexcept { live_frames.fetch_sub(1, std::memory_order_relaxed); ::operator delete(p); }
	// ... rest of the promise
};

// At shutdown:
assert(promise_type::live_frames.load() == 0);
```

This counts what you care about - frames created versus frames destroyed - independently
of reachability, and it works in a build with no sanitiser at all. Section 10 turns it
into a procedure.

## 4. Lifecycle of thel promise object

The promise lives *inside* the frame, so its lifetime is a sub-interval of the frame"s:
constructed at step 3, destroyed at step 10. Everything the compiler needs from a
coroutine, it asks the promise.

**4.1 Required and optional members**

Required – omitting any is a compile error:

```cpp
struct promise_type {
	T get_return_object();
	Awaitable initial_suspend():
	Awaitable final_suspend() noexcept;
	void unhandled exception():
	void return void();	// and/or return_value(T), see 2.3
};
```

Optional, each enabling a feature:

| Member									  | Enables 								  |
| ------------------------------------------- | ----------------------------------------- |
| `return_value(T)`							  | `co_return v`							  |
| `yield_value(T)`							  | `co_yield v`							  |
| `await_transform(T)`						  | Intercepting every `co_await` in the body |
| `operator new` / `operator delete`		  | Custom frame allocation					  |
| `get_return_object_on allocation_failure()` | Non-throwing allocation					  |

**4.2 Construction: the promise can see the arguments**

If `promise_type` has a constructor accepting the coroutine's argument list, it is used;
otherwise the default constructor is. For member coroutines the implicit object argument
is passed first.

```cpp
struct promise_type {
	Allocator* alloc_;
	promise_type(Allocator& a, int /*other args*/) : alloc_(&a) {}	// sees the coroutine's args
	// ...
};

Task<int>work(Allocator& a, int n);	// promise gets (a, n)
```

Overload resolution here is a common source of confusion: if the constructor is not viable
for the full argument list, the compiler silently falls back to the default constructor
rather than reporting the near-miss. This is also the mechanism behind allocator-aware
coroutine types, which pair it with `operator new(std::size_t, Allocator&, Args...)`.

**4.3 get_return_object and the ordering trap**

`get return_object()` is called before `initial_suspend()`, so it cannot observe anything
the body does. It typically does one of two things:

```cpp
//	1 (a) Build the handle from the promise - the common case
Task get_return_object() {
	return Task{std::coroutine_handle<promise_type>::from_promise(*this)}:
}

// 11 (b) Return something convertible, deferring construction
struct promise_type { std::suspend_always get_return_object() { return {}; } }; 	// rare
```

`from_promise(*this)` is the inverse of `handle.promise()`: given the promise's address,
it recovers the frame address. Both are constant-time pointer arithmetic with no lookup,
and both require that the promise really is the one embedded in a frame – calling
`from_promise` on a standalone `promise_type` object is undefined.

One ordering subtlety worth internalising: the object returned by `get_return_object` is
converted to the coroutine's declared return type at the point of the `return` in the ramp
function - that is, at the *first suspension*, not at step 4. If the return type differs
from `get_return_object`'s type, that conversion is sequenced after the body has begun
(for eager coroutines). Keeping the two types identical avoids the whole question.

**4.4 Result and exception storage**

Since the promise outlives the body when `final_suspend` suspends, the promise is where
the result belongs. The idiomatic storage is a variant:

```cpp
#include <variant>

template <typename T> 
struct promise_type {
	std::variant<std::monostate, T, std:: exception_ptr> result_;
	
	void return_value(T v) { result_.template emplace<1>(std::move(v)); }
	void unhandled_exception() noexcept { result_.template emplace<2>(std::current_exception()); }

	T&& take() {
		if (result_.index() == 2) std::rethrow_exception(std::get<2>(result_));
		return std::move(std:: get<1>(result_));
	}
};
```

Notes on this pattern:

- `unhandled_exception` is called *inside* the catch block of the ramp, so
  `std::current_exception()` is valid there and only there.
- It may itself rethrow, in which case the exception propagates out of `resume()` to
  whoever resumed the coroutine. This is a legitimate design - but the frame is then
  already at the final suspend point, so ownership rules still apply.
- `unhandled_exception` never sees exceptions thrown by `get_return_object`,
  `initial_suspend`'s ramp portion, or `final suspend`.

**4.5 `await_transform`: controlling what may be awaited**

`await_transform` is applied to every operand of "co_await in the body. Two main uses.

Restricting the vocabulary - make awaiting the wrong type a compile error:

```cpp
struct promise_type {
	// only our own Task type may be awaited
	template <typename T>
	auto await_transform(Task<T>&& t) { return TaskAwaiter<T>{std::move(t)}; }

	// Everything else: deleted, so the diagnostic names the offending type
	template <typename T>
	void await_transform(T&&) = delete;
};
```

Injecting context - give awaited operations access to the promise, without threading it
through every call:

``` cpp
struct promise_type {
	Scheduler* sched s
	auto await_transform(GetScheduler) noexcept {
		struct Awaiter {
			Scheduler* s;
			bool await_ready() noexcept { return true; }
			void await_suspend(std::coroutine_handles<>) noexcept {}
			Scheduler* await_resume() noexcept { return s; }
		};
		return Awaiter{sched_};
	}
};

// In the body: scheduler*s = co_await Getscheduler);
```

This is how "current scheduler", "current cancellation token", and "current allocator" are
conventionally exposed. Note that `await_transform`, once declared for any type,
participates in overload resolution for *all* awaits in that coroutine - including awaits
of standard awaitables, which is why the generic `= delete` overload above is deliberate
rather than accidental.

## 5. Lifecycle of the coroutine handle

`std::coroutine_handle<P>` is a trivially copyable, pointer-sized, **non-owning**
reference to a frame. It has no destructor that does anything. Copying it does not copy
the coroutine; destroying it does not destroy the coroutine.

**5.1 Interface and the two flavours**

```cpp
std::coroutine_handle<promise_type> h;	// typed: gives access to promise()
std::coroutine_handle<>;				// type-erased: resume/destroy/done only

e = h; 									// implicit conversion, always safe
auto back = std::coroutine_handle<promise_type>::from_address(e.address()); // only if you KNOW the type
```

| Operation						 | Precondition 									 |
| ------------------------------ | ------------------------------------------------- |
| `resume()` / `operator()`		 | suspended, not at final suspend					 |
| `destroy()`					 | suspended (any point), frame alive				 |
| `done()`						 | suspended										 |
| `promise()`					 | frame alive, typed handle						 |
| `address()` / `from_address()` | round-trip only, no type checking		  	     |
| `operator bool`				 | tests against `nullptr` **only** –not "is alive"  |

That last row is a trap: `operator bool` is false only for a default-constructed or
explicitly nulled handle. It cannot detect a destroyed frame. After `h.destroy()`, `h`
still tests `true` and is dangling. Null it yourself:

```cpp
if (h_) { h_.destroy(); h_ = {}; }
```

**5.2 ownership discipline**

Because the handle is non-owning, every coroutine type needs an explicit answer to "who
calls destroy() ". The workable patterns:

- **Return object owns it.** `destroy()` in the destructor, copy deleted, move nulls the
  source. This is the `Task`/`Generator` pattern and the default choice.
- **The coroutine owns itself.**• `final_suspend` returns `suspend_never`, so completion
  self-destructs. Nobody may hold the handle afterwards. Suitable for detached
  fire-and-forget.
- **A scheduler owns it.** The handle is stored in a queue; the queue destroys frames it
  will never resume. Requires care during shutdown.

The anti-pattern is a shared handle with no designated owner, which yields either a double
`destroy()` or a leak. `std::coroutine_handle` deliberately provides no reference
counting; if you need shared ownership, put a refcount in the promise.

**5.3 Symmetric transfer and stack growth**

Returning a handle from `await_suspend` means "suspend me and resume *that* one", and the
standard requires it be performed as a tail call – the current frame's resume function
returns before the next one starts:

```cpp
struct FinalAwaiter {
	bool await_ready() noexcept { return false; }

	std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
		if (auto cont = h.promise().continuation) return cont;		// resume the awaiting coroutine
		return std:: noop_coroutine();								// nothing to resume: park
	}
	
	void await_resume() noexcept {}
};
```

Without symmetric transfer, a chain of coroutines resuming each other consumes O(N)
stack; with it, O(1). Any task type that supports awaiting other tasks needs this,
otherwise deep chains or loops of awaits overflow the stack.

`std::noop_coroutine()` returns a handle to a coroutine that does nothing and is never
done. It exists precisely so `await_suspend` can always return a valid handle instead of
branching on `void`/`bool` returns.

**5.4 The promise() and from_promise() round trip**

```cpp
auto& p = h.promise();
auto h2 = std::coroutine_handles<promise_type>::from_promise(p);
assert(h.address() == h2. address());
```

This pair is how awaiters reach the promise of the coroutine that is awaiting them, and
how a promise reaches its own handle inside `get_return_object`. It only works for the
exact promise type - there is no derived-to-base conversion, so `coroutine_handle<Base>`
from a `Derived` promise is undefined even when the inheritance is real.

## 6. Lifecycle of awaitables and awaiters

An **awaitable** is what you write after `co_await`. An **awaiter** is the object that
actually implements the three-method protocol. They are often the same object, which is
why the distinction is easy to miss - and why it matters when they are not.

**6.1 Awaitable to awaiter, precisely**

```
co_await expr
    |
    +-- promise.await_transform(expr) if the promise declares await_transform	--> awaitable
    | 	otherwise																--> awaitable = expr
    |
	+-- awaitable.operator co_await() or operator co_await(awaitable) if either exists 	--> awaiter
		otherwise																		--> awaiter = awaitable
```

Splitting them is what lets one awaitable be awaited in different ways:

```cpp
class Task {
public:
	// Awaiting an rvalue task moves the result out; awaiting an Ivalue yields a reference
	auto operator co_await()  &&	 { return Awaiter<Task&&>{std::move(*this)}; }
	auto operator co_await () const& { return Awaiter<const Task&>{*this}; }
};
```

**6.2 The three methods and their contracts**

```cpp
struct Awaiter {
	// 1. Fast path. Return true to skip suspension entirely.
	//	  Called before any state is saved. Must not assume the coroutine is suspended.
	bool await_ready()) noexcept;

	// 2. Called with the coroutine FULLY SUSPENDED. Three possible return types:
	// 		void					  -> stay suspended, return to caller
	//		bool					  -> true: stay suspended; false: resume immediately
	//		coroutine_handle<>		  -> symmetric transfer to that coroutine
	//    May resume h itself (before returning) - see the race in 2.2.
	void await_suspend (std::coroutine_handle<> h);

	//3. The value of the whole co_await expression. Runs on resumption
	//	 (or immediately, if await_ready returned true).
	//	 Throwing here is how an awaited operation reports failure.
	T await_resume();
};
```

Lifetime of the awaiter itself: it is a temporary of the enclosing full-expression,
materialised in the *frame* because it must survive the suspension. So it lives from just
before `await_ready` until just after `await_resume`. Two consequences:

- An awaiter may hold references to its own members across the suspension safely - they
  are in the frame.
- But if the coroutine is `destroy()`ed while suspended in that await, `await_resume` is
  never called. Any cleanup the awaiter owes must be in its destructor, not in
  `await_resume`. This is the awaiter-level version of 3.6.

**6.3 The standard trivial awaiters**

```cpp
struct suspend_never {
	bool await_ready() const noexcept { return true; } // never suspends
	void await_suspend(std::coroutine_handle<>) const noexcept {}
	void await_resume() const noexcept {}
};

struct suspend_always {
	bool await_ready() const noexcept { return false; }	// always suspends
	void await_suspend(std::coroutine handle<>) const noexcept {}
	void await_resume() const noexcept {}
};
```

They are worth reading because they show the minimum: `suspend_always` suspends and does
nothing else, meaning control returns to whoever resumed the coroutine and the handle must
already be known to someone, or the coroutine is stranded.

**6.4 A realistic awaiter: awaiting another task**

This ties sections 4, 5, and 6 together - the awaited task records its awaiter as a
continuation, and resumes it from `final_suspend`:

```cpp
template <typename T>
class Task {
public:
	struct promise_type;
	using handle = std::coroutine_handle<promise_type>;

	struct FinalAwaiter {
		bool await_ready() noexcept { return false; }
		std::coroutine_handles<> await_suspend(handle h) noexcept {
			// Resume whoever awaited us; if nobody did, park without destroying.
			if (auto c = h.promise().continuation_) return c;
			return std::noop_coroutine();
		}
		void await_resume() noexcept {}
	};

	struct promise_type {
		std::coroutine_handle<> continuation_{};
		std::variant<std:monostate, T, std:: exception_ptr> result_{};

		Task get_return_object() { return Task{handle::from_promise(*this)}; }
		std::suspend_always initial_suspend() noexcept { return {}; }	// lazy
		FinalAwaiter final_suspend()          noexcept { return {}; }	// resume the awaiter
		void return_value(T v) { result.template emplace<1>(std::move(v)); }
		void unhandled_exception() noexcept { result.template emplace<2>(std::current_exception()); }
	};

	// Awaiting the task: start it, and register ourselves as its continuation.
	auto operator co_await() && noexcept {
		struct Awaiter {
			handle h_;
			bool await_ready() noexcept { return !h_ || h_.done(); }
			std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
				h_.promise().continuation_ = awaiting;
				return h_;						// symmetric transfer: start the awaited task
			}
			T await_resume() {
				auto& r = h_.promise().result_;
				if (r.index() == 2) std::rethrow exception(std::get<2> (r));
				return std::move(std::get<1>(r));
			}
		};
		return Awaiter{h_};
	}

	~Task() { if (h_) h_.destroy(); }
	Task(Task&& o) noexcept : h_(std::exchange(o.h_, {})) {}
	Task(const Task&) = delete;

private:
	explicit Task(handle h) : h_(h) {}
	handle h_;
};
```

Trace the ownership: the outer `Task` object owns the frame and destroys it;
`continuation_` is a non-owning handle to the awaiting coroutine, which is owned by *its*
own return object further up the chain. Nothing is shared, and every frame has exactly one
owner.

**6.5 Common awaiter mistakes**

- **Touching the awaiter after publishing the handle** (2.2). The most common source of
  use-after-free in coroutine code.
- **`await_ready` with side effects.** It may be called without any suspension following,
  and its result may be used to skip await_suspend entirely.
- **Non-`noexcept` `await_suspend` that throws after publishing.** The coroutine is
  suspended and possibly already resumed elsewhere; the standard resumes the coroutine and
  rethrows at the await, which is nearly impossible to reason about. Make `await_suspend`
  `noexcept` and report failures from `await_resume`.
- **Returning a dangling handle** from `await_suspend`. The returned handle must refer to
  a suspended, live coroutine.
- **Assuming `await_resume` runs.** It does not if the coroutine is destroyed while
  suspended.

## 7. Putting it together: a generator

A generator is the other canonical coroutine type, and it exercises `co_yield`, lazy
start, and pull-driven resumption rather than continuations.

```cpp
#include <coroutine>
#include <exception>
#include <iterator>
#include <memory>
#include <utility>

template <typename T>
class Generator {
public:
	struct promise_type {
		T*				  current_{};		// pointer into the producer's frame - no copy
		std::exception_ptr error_{};

		Generator get_return_object() {
			return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
		}
		std::suspend_always initial_suspend() noexcept { return {}; }	// lazy: nothing runs yet
		std::suspend_always final_suspend()  noexcept { return {}; }	// stay alive so done() is observable
		std::suspend_always yield_value(T& v)  noexcept { current_ = std::addressof(v); return {}; }
		std::suspend_always yield_value(T&& v) noexcept { current_ = std::addressof(v); return {}; }
		void return_void() noexcept {}
		void unhandled_exception() noexcept { error_ = std::current_exception(); }
	};

	struct iterator {
		using iterator_category = std::input_iterator_tag;
		using value_type = T;
		using difference_type = std::ptrdiff_t

		std::coroutine_handle<promise_type> h_{};

		iterator& operator++() {
			h_.resume();
			if (h_.done()) {
				auto e = h_.promise().error_;
				h_ = {};							// become the end iterator
				if (e) std::rethrow_exception(e);	// propagate producer failure to the consumer
			}
			return *this;
		}
		T& operator*() const { return *h_.promise().current_; }
		bool operator==(std::default_sentinel_t) const { return !h_ || h_.done(); }
	};

	iterator begin() {
		if (h_) {
			h_.resume();	// run up to the first co_yield
		}
		if (h_.done()) {
			if (auto e = h_.promise().error_) std::rethrow_exception(e);
			return iterator{};
		}
		return iterator{h_};
	}
	std::default_sentinel_t end() { return {}; }

	Generator(Generator&& o) noexcept : h_(std::exchange(o.h_, {})) {}
	Generator(const Generator&) = delete;
	~Generator() { if (h_) h_.destroy(); }

private:
	explicit Generator(std::coroutine_handle<promise_type> h): h_(h) {}
	std::coroutine_handle<promise_type> h_:
};

Generator int> range(int first, int last) {
	for (int i = first; i < last; ++i) co_yield i;
}

// for (int v: range(0, 5)) ...
```

Points where this differs from the task in 6.4, and why:

- **Pull, not push.** The consumer calls `resume()`; there is no continuation handle,
  because control always returns to the same place.
- **`begin()` resumes once.** A lazy generator has produced nothing until then, so
  `begin()` must advance to the first value. That makes `begin()` non-const and
  single-pass – hence `input_iterator_tag`.
- **Values are borrowed.** `current_` points into the producer's frame. valid only while
  the producer is suspended, which is exactly when the consumer is running. Copying out is
  the consumer's choice, not a cost the generator imposes.
- **Exceptions cross the boundary.** The producer's exception is stored in the promise and
  rethrown from `++`/`begin()`, so the consumer sees it at the point of iteration.

C++23 provides `std::generator<T>`, which does all of the above plus recursive yield
(`co_yield std::ranges::elements_of(inner)`) and allocator support. Prefer it when
available; write the above only to understand what it does or to target C++20.

## 8. Threading, cancellation, and debugging

**8.1 There is no implicit concurrency**

A coroutine runs on whatever thread calls `resume()`. Suspension is not a thread switch,
and `co_await` is not "run this in parallel". A coroutine that never gets resumed simply
never continues.

Switching threads is therefore an explicit awaiter that resumes the handle elsewhere:

```cpp
struct ResumeOn {
	Executor& ex;
	bool await_ready() const noexcept ( return false; )
	void await_suspend(std::coroutine_handle<> h) const { ex.post(h); }		// resumed on ex's thread
	void await_resume() const noexcept ()
};

// co_await ResumeOn{pool};		// everything after this line runs on the pool
```

Consequences that bite in practice:

- Thread-Local state read before an await may be wrong after it. Re-read, don't cache.
- A `std::lock_guard` held across a `co_await` may be unlocked by a *different* thread –
  undefined for `std::mutex`. Never hold a standard mutex across a suspend point; use an
  awaitable async mutex, or restructure so the critical section contains no awaits.
- The frame is not synchronised. If two threads may touch a frame, the handoff itself must
  provide the ordering which the suspend/resume pair does, provided exactly one thread
  resumes.

**8.2 Cancellation is not built in**

C++20 has no coroutine cancellation. `destroy()` is the closest primitive, and it only
works on a *suspended* coroutine, which means you cannot cancel work that is currently
running or already handed to a callback.

The two workable approaches:

- **Cooperative token.** Pass a `std::stop_token` (or await one via `await_transform`,
  4.5) and check it at each suspend point, returning early. Requires the coroutine's
  cooperation but composes cleanly.
- **Destroy at a known suspend point.** Only safe when you own the frame *and* know it is
  parked, e.g. a scheduler shutting down its queue. Anything in flight in an external
  callback must complete or be joined first.

Either way, cleanup belongs in destructors (3.6), because cancellation by destruction
skips all trailing statements.

**8.3 Debugging**

The state machine is what the debugger sees, so expect:

- Stepping through a `co_await` jumps to the caller, not the next line. Set a breakpoint
  after the await instead of stepping.
- Stack traces show the resumer, not the logical awaiting chain. The `continuation_` chain
  in the promise *is* the logical stack – printing it is the usual workaround, and a
  reason to store a debug label in the promise.
- Locals may appear optimised out if they do not cross a suspend point, because they never
  reached the frame.

Useful tools:

- `-fno-omit-frame-pointer` and `-O0` to keep frames inspectable, accepting that HALO
  (2.4) then never fires.
- Clang's `-fcoroutines-ts`-era `builtin_coro_*` intrinsics are visible in IR:
- `clang -std=c++20 -emit-11vm -S` shows `11vm.coro.begin`, `11vm.coro.suspend`, and
  `11vm. coro.free` which is the most direct way to confirm what the transformation
  produced and whether the allocation was elided.
- ASan and UBSan catch the common use-after-free from resuming or reading a destroyed
  frame. Given how many of the traps in this document are lifetime bugs, running coroutine
  tests under ASan is close to mandatory.

**8.4 Frame size**

Frame size is not specified and varies by compiler and optimisation level. Two rules of
thumb:

- Every local live across a suspend point costs frame space, and the frame is sized for
  the *maximum* over all suspend points - not the current one.
- Large objects and deep inlining inflate frames quickly. If frames matter, keep bodies
  small and move big state behind a pointer.

`sizeof` cannot see the frame; measure by instrumenting `promise_type: operator new`:

```cpp
void* operator new(std: size_t n) { std::printf("frame %zu bytes\n", n); return ::operator new(n); }
```

**8.5 Applied: a minimal epoll I/O reactor**

Everything above is machinery; this is what it is *for*. The smallest useful async setup
is one awaitable that parks a coroutine on a file descriptor plus one event loop that
resumes it when the fd is ready. It ties together the pieces from sections 5-8: a
non-owning handle published in `await_suspend`, a scheduler that owns resumption, and a
fire-and-forget task.	

The awaitable registers the waiting handle with the reactor and suspends. `await_suspend`
returning `void` means "stay suspended"; the reactor resumes later. Publishing the handle
is the *Last* thing done (8.1 - once published, the frame may be resumed on another
iteration):

```cpp
#include <coroutine>
#include <sys/epoll.h>
#include <unistd.h>
#include<fcntl.h>
#include <stdexcept>

class IOReactor;

struct IoAwaitable {
	int 	 fd;
	uint32_t events;
	IOReactor& reactor;

	bool await_ready() const noexcept { return false; }		// always park: let the loop decide readiness
	void await_suspend (std::coroutine_handle<> h);		// registers (fd, events, h) then returns
	void await_resume() const noexcept {}
};
```

The task is fire-and-forget (4th flavour in 91): `suspend never` at both ends, and it
destroys its own frame on completion. That is correct here only because the reactor never
retains the handle past resumption -it deregisters the fd before the final resume, so the
loop holds no dangling pointer:

```Cpp
struct DetachedTask {
	struct promise_type {
		Detachedtask get_return_object() noexcept {return {}; }
		std::suspend_never initial_suspend() noexcept { return {}; }
		std::suspend_never final_suspend() 	 noexcept { return {}; }	// frame self-destroys at the end
		void return_void() noexcept {}
		void unhandled_exception() { std::terminate(); }
	};
};
```

The reactor owns the epoll fd and the resume decision. One-shot registration
(`EPOLLONESHOT`-style: delete before resume) keeps ownership unambiguous - the handle
lives in the `epoll_event` user data only between registration and resume:

```cpp
cLass IOReactor {
public:
	IOReactor() : epfd_(epoll_create1(0)) {
		if (epfd_ == -1) throw std::runtime_error("epol]_create1");
	}
	~IOReactor() { ::close(epfd_); }

	void add(int fd, uint32_t events, std::coroutine_handle<> h) {
		epoll_event evt{};
		ev.events 	= events | EPOLLONESHOT;	// fire once; we re-arm by re-awaiting
		ev.data.ptr = h.address();				// non-owning: the coroutine's own frame owns itself
		if (epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) == -1 &&
			epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev) == -1)
			throw std::runtime_error("epoll_ctl");
	}

	void run_once(int timeoutms =-1) {
		epoll_event evs[64];
		int n= epoll_wait(epfd_, evs, 64, timeout_ms);
		for (int i = 0; i < n; ti)
			std::coroutine_handle<>::from_address(evs[i].data.ptr).resume();
	}

private:
	int epfd_;
};

void IoAwaitable::await_suspend(std::coroutine_handle<> h) {
	reactor.add(fd, events, h);   // publish last: after this the loop may resume h
}
```

A coroutine built on this reads linearly even though every I/O call suspends:

```cpp	
Detachedrask echo (IOReactor& r, int fd) {	
	char buf[1024];
	for (;;) {
		co_await IoAwaitable(fd, EPOLLIN, r);
		ssize_t n = ::read(fd, buf, sizeof buf);
		if (n <= 0) break;						// falls through to final suspend -> self-destroy
		co_await IoAwaitable{fd, EPOLLOUT, r};
		::write(fd, buf, static_cast<size_t>(n));
	}
	::close(fd):
}
```

what this example is deliberately *not*: production-ready. It ignores partial
reads/writes, has no backpressure, no cancellation (8.2), no error propagation out of the
detached task, and resumes on the loop thread only (8.1). It also elides
edge-vs-level-triggered subtleties. The point is the *shape* - awaitable registers, loop
resumes, task self-manages - not the completeness. For anything real, use a library (9.2)
or a purpose-built reactor; the [`cox/`](../cox/) notes in this repository document one
such C++20 reactor design in depth, and section 6.4 shows the lazy-task awaiter pattern
that a non-detached version would use.

## 9. Key points to remember

- A function is a coroutine because of `co_await`/`co_yield`/`co_return` in its **body**,
  not its signature.
- **Return object**, **promise**, **handle**, and **frame** are four distinct things. Most
  confusion is a conflation of two of them.
- `co_yield v` is `co_await promise.yield_value(v)`. `co_return v` is
  `promise.return_value(v)` plus a jump to final suspend.
- The return object is created **before** the body runs; it reaches the caller at the
  **first suspension**.
- `co_await` is three calls - `await_ready`, `await_suspend`, `await_resume` – reached via
  `await_transform` and then `operator co_await`.
- In `await_suspend` the coroutine is already fully suspended: once the handle is
  published, the frame may be gone. Publish last.
- `initial_suspend` chooses eager vs lazy. Lazy is the right default for tasks.
- `final_suspend` chooses who frees the frame. `suspend_always` plus `destroy()` in the
  return object's destructor is the standard answer.
- The handle is **non-owning** and `operator bool` does not mean "alive". Exactly one
  owner must call `destroy()` exactly once.
- Return a handle from `await_suspend` for symmetric transfer, otherwise chained awaits
  grow the stack linearly.
- Parameters are copied into the frame; **references are not**. Take parameters by value.
- `destroy()` runs destructors but does not resume: cleanup goes in destructors, never
  after an await.
- A frame alive after `done()` is the **design**, not a leak. A frame with **no owner** is
  the leak – in any state, including one whose body never ran.
- ASan leak detection is reachability-based, so a retained handle hides a frame leak
  completely. Count frames in `promise_type::operator new`/`delete` instead.
- No implicit threading, no built-in cancellation. Both are library concerns you must
  design for.

**9.1 Reference: what to write for each coroutine flavour**

| Flavour			   | `initial_suspend` | `final_suspend` 						 | key promise members 					 | Who destorys 		 |
| -------------------- | ----------------- | --------------------------------------- | ------------------------------------- | --------------------- |
| Lazy task, awaitable | `suspend_aways`   | custom_awaiter resuming `continuation_` | `return_value`, `unhandled_exception` | return object dtor    |
| Eager task 	       | `suspend_never`   | `suspend_aways`					 	 | as above 							 | return object dtor	 |
| Generator 	       | `suspend_aways`   | `suspend_aways`					 	 | `yied_value`, `return_void` 			 | return object dtor	 |
| fire-and-forget 	   | `suspend_never`   | `suspend_aways`					 	 | `return_void` 						 | itself, on completion |

**9.2 Preference order**

1. Use `std::generator<T>` (C++23) for synchronous sequences.
2. Use an existing library - cppcoro, libunifex, stdexec, Boost.Cobalt, asio's `awaitable`
   - for async tasks.
3. Write your own promise type only to learn, or when no library fits. The task in 6.4 is
   a correct starting point, but production types also need cancellation, allocator
   support, and scheduler affinity.
4. Reach for coroutines at all only when the problem is genuinely suspend/resume shaped:
   async I/O, lazy sequences, state machines. A callback or an iterator is often simpler
   and always easier to debug.

## 10. Practice: diagnosing a coroutine frame leak

A procedure, in the order you should actually apply it. The background is 3.7 (deferred
destruction is not a leak) and 3.8 (why sanitisers miss these).

**10.1 Step • - decide whether there is a bug at all**

Before instrumenting anything, answer one question: **does an owner exist for this
frame?**

Not "is the frame still alive" - with `final_suspend()` == `suspend_always` a completed
frame is *supposed* to be alive. The diagnostic question is whether some object or
subsystem is contractually obliged to call `destroy()`, and whether that obligation is
reachable.

```
Is the frame still alive? ---no---> no leak
		 |
		yes
		 |
Does exactly one owner exist, and will it run?
		 |
	+----+----+
   yes		  no
    |		   |
  no Leak 	 LEAK (regardless of which state the frame is in)
```

Most "coroutine leak" reports resolve here, in either direction, without a debugger. A
frame parked at final suspend with a live `Task` holding it is fine. The same frame with
the `Task` moved-from and discarded is a leak.

**10.2 Step 1 - instrument the promise**

This is the primary tool, not the fallback. Add a counting base to the promise type and
inherit it:

```cpp
struct FrameCounter {
	static inline std::atomic<int> live{0};
	static inline std::atomic<int> created{0};

	void* operator new(std::size_t n) {
		live.fetch_add(1, std::memory_order_relaxed);
		created.fetch_add(1, std::memory_order_relaxed);
		return ::operator new(n);
	}
	void operator delete(void* p) noexcept {
		live.fetch_sub(1, std::memory_order_relaxed);
		::operator delete(p);
	}
};

struct promise_type: FrameCounter {
	//... the usual five members
};
```

Then assert at shutdown, or print:

```cpp
std::printf("created-%d live-%d\n", FrameCounter:: created.load(), FrameCounter::live. load());
assert(FrameCounter:: live.load() == 0);
```

Why this comes first:

- It counts frames created versus destroyed, which is exactly the ownership question from
  10.1 – not reachability.
- It works with no sanitiser, in release builds, and in production behind a flag.
- `created` versus `live` distinguishes "leaking steadily" from "leaked once at startup",
  which is the difference between a hot-path ownership bug and a one-off.
- It defeats HALO honestly: if the allocation was elided, `operator new` was not called,
  so an elided frame cannot show up as a false positive.

Track `created` as well as `live`, not just `live`. A `live` count that is merely non-zero
tells you much less than one you can compare against total creations.

**10.3 Step 2 - run under ASan, and know what it cannot see**
  
```sh
gt+ -std=c++20 -g -fsanitize=address,undefined -Wall -Wextra coro.cpp -o coro && ./coro
```

ASan gives you a stack trace to the *allocation*, which names the coroutine function –
this is its real value, and the counter cannot provide it:

```text
Direct leak of 40 byte(s) in 1 object(s) allocated from:
	#0 operator new(unsigned long)
	#1 FrameCounter:: operator new(unsigned long)	scenarios.cpp:19
	#2 raw_never_started ()							scenarios.cpp:55	<-- the coroutine
	#3 main											scenarios.cpp:74	<-- the call site
```

Two limitations to internalise:

- **Reachability, not ownership** (3.8). A handle retained in any live root - a global,
  a never-drained quee, a leaked container - suppresses the report entirely. This is
  precisely the scheduler-shutdown bug, so a clean Asan run is not evidence of
  correctness.
- **ASan calls '_exit() after reporting**, which skips stdio flushing. Your own counter
  output disappears exactly when a leak is detected, which looks like the instrumentation
  failing. Flush explicitly:

```cpp
std::printf("created-%d live-%d\n", created.load(), live.load());
std:: fflush(stdout);		// ASan _exit(1)s after its report
```

Use the two tools together: the counter decides *whether* there is a leak, ASan tells you
*where the frame came from*.

**10.4 Step 3 - localise which state the frame stuck in**

Once you know a frame leaked, find out where it was parked. Four scenarios cover
essentially all of them, distinguished by observable behaviour:

| Scenario	                                | `created` | `live` | ASan       | Distinguishing sign                      |
| ----------------------------------------- | --------- | ------ | ---------- | ---------------------------------------- |
| Lazy task discarded, never resumed	    | 1	        | 1	     | reports    | body side effects never happened	     |
| Suspended mid-body, handle dropped	    | 1		    | 1		 | reports	  | body partially ran, stopped at an await  |
| Completed, parked at final suspend	    | 1		    | 1		 | reports	  | `done()` was `true`, result was readable |
| Suspended, handle retained in a live root | 1		    | 1		 | **silent** | counter non-zero, ASan clean			 |

The last row is the one to recognise from the shape of the evidence: counter says leak,
sanitiser says nothing. That combination means a handle is still reachable from somewhere,
and the question becomes "which container is holding it and why is nobody draining it".

The first row is worth stating separately because it violates an intuition: creating a
lazy coroutine and never resuming it leaks a frame, even though **no user code in the body
ever executed**. The allocation happens in the ramp function before `initial_suspen`.

To identify the parked location, add a label to the promise and record the last suspend
point:

```cpp
struct promise_type : FrameCounter {
	const char* where_= "created";	//update at each await site via await trancfonm
	//…
};
```

Then dump `where_` for every frame still live at shutdown. Combined with the
`continuation_` chain (8.3), this reconstructs the logical await stack that the debugger
will not show you.

**10.5 Step 4 - fix at the ownership layer, not the symptom**

The fix is almost never "call `destroy()` somewhere". Match the cause:

| Cause	                                        | Fix																	  |
| --------------------------------------------- | ----------------------------------------------------------------------- |
| Return object does not own the frame			| Give it a destructor that destroys, delete copy, null on move (5.2)	  |
| Two owners, or an owner that also gets copied	| Delete the copy constructor; `std::exchange` in the move constructor    |
| Lazy task created and discarded				| Make discarding impossible or loud: `[[nodiscard]]` on the return type  |
| Handles parked in a queue, never drained		| Drain and `destroy()` on shutdown; make the queue the documented owner  |
| Fire-and-forget that should self-destruct		| `final_suspend()` returning `suspend_never`							  |
| Cleanup written after a `co_await`			| Move it into a destructor (3.6) - `destroy()` skips trailing statements |

`[[nodiscard]]` deserves specific mention: it converts the never-started-lazy-task leak
from a runtime problem into a compile-time warning, which is the cheapest possible fix and
costs nothing.

```cpp
template <typename T>
class [[nodiscard]] Task {/* ... */ };
```

**10.6 Step 5 - make it a regression test**

A leak fixed without a test comes back, because the ownership rules are invisible in the
type system. Assert on the counter:

```cpp
TEST (Coro, NoFrameLeaks) {
	const int before = FrameCounter::live.Load();
	{
		auto t = make_task();
		t.start ();
		EXPECT_EQ(t. result(), 42);
	}												// destructor must run here
	EXPECT_EQ(FrameCounter:: live.load(), before);	// net zero frames
}
```

Test the partial-consumption and early-destruction paths too - abandoning a generator
halfway, destroying a task while it is suspended, and throwing from a body. Those are
where ownership bugs actually live, and they are trivial to write once the counter exists:

```cpp
TEST (Coro, PartialConsumptionDoesNotLeak) {
	const int before = FrameCounter::live.load() ;
	{ auto g = range(0,1000); auto it = g.begin(); ++it; }	// abandon after 2 of 1000
	EXPECT_EQ( FrameCounter::live.load(), before);
}
```

**10.7 Checklist**

```
[ ] 0. Does an owner exist? (deferred destruction alone is not a leak)
[ ] 1. Add FrameCounter to the promise; compare created vs Live at shutdown
[ ] 2. Run under ASan+UBSan for the allocation stack trace; fflush your own output
[ ] 3. Counter says leak but ASan is silent --> a live root is holding the handle 
[ ] 4. Identify Identify the parked state: never-started / mid-body / final-suspend / retained
[ ] 5. Fix ownership, not the symptom; add [[nodiscard]] to the return type
[ ] 6. Regression-test the count, including partial consumption and early destroy
```

Appendix A. Compile-check file Every runnable snippet in this document is collected here,
tagged with the section it comes from. Because nearly every coroutine trap in this
document is a lifetime bug, this file is meant to be run under a sanitiser - a leak, or
use-after-free in a coroutine type is usually silent otherwise.

Build and run:

```sh
g++ -std=c++20 -Wall -Wextra -pedantic -g -fsanitize=address,undefined check.cpp -o check && ./check
```

Expected output:

```text
1.3 eager body ran
3.6 guard fired: ran via destroy()
4.5 scheduler id -1
6.4 chained result 42
7 range: 0 1 2 3 4
7 got 1
7 caught: producer failed
7 partial consumption clean
8.1 resumed via executor
8.4 frame 296 bytes
4.4 slot holds 99
```

verified with g++ 13.3 at `-O0` and `-O2`, under `-std=c++20` and `-std-c++23`, clean
under `-Wall -wextra-pedantic` and under ASan + UBSan with no leaks reported.

Two of the printed lines are worth reading rather than skimming:

- `4.5 scheduler id -1` – the promise's `sched_` was never set, which is the honest
  result. The point of the snippet is that `await_transform` delivers promise state into
  the body, not that a scheduler exists.
- `8.4 frame 296 bytes` – a 256-byte local crossing one suspend point, plus promise and
  bookkeeping. The exact number is compiler- and flag-specific; treat it as a measurement
  technique, not a constant.

Deliberately absent, because they cannot compile or would be undefined behaviour by
construction:

- The dangling cases in 3.5 - reference parameters bound to temporaries, and the
  immediately-invoked coroutine lambda. Both are use-after-free on purpose.
- The `WRONG` variant of `await_suspend` in 2.2, which touches the awaiter after
  publishing the handle.
- Resuming a coroutine at its final suspend point (3.1), and any second `destroy()`.
- HALO (2.4) is not asserted: elision is permitted, never guaranteed, so a test that
  requires it would be testing the optimiser rather than the code.

```cpp
// Compile-check for "Coroutines.md".
// Every runnable snippet in the document appears here, tagged with its section.
//
// g++ -std=c++20 -Wall -wextra -pedantic -fsanitize=address,undefined check.cpp -o check && ./check
//
#include <coroutine>
#include <cstdio>
#include <exception>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// ---------------------------------------------------------------- 1.3 minimal eager task
namespace s13 {
struct Task {
	struct promise_type {
		Task get_return_object() {return Task{std::coroutine_handle<promise_type>::from_promise(*this)}; }
		std::suspend_never initial_suspend() noexcept { return {}; }
		std::suspend_always final_suspend()	 noexcept { return {}; }
		void return_void() noexcept {}
		void unhandled_exception() { std::terminate(); }
	};

	explicit Task(std::coroutine_handle<promise_type> h) : h_(h) {}
	Task(const Task&) = delete;
	Task& operator=(const Task&) = delete;	
	Task (Task&& 0) noexcept : h_(std::exchange(o.h_, {})) {}
	~Task() { if (h_) h.destroy(); }

	std::coroutine_handle<promise_type> h_:
};

Task run() { std::puts("1.3 eager body ran"); co_return; }
inline void use() { auto t = run(); }
} // namespace s13

// ---------------------------------------------------------------- 2.2 await_suspend ordering
namespace s22 {	
struct SafeAwaiter {	
	int value_ = 7;	
	bool await_ready() const noexcept { return false; }
	void await_suspend(std::coroutine_handle<> h) const {
		auto copy = value_;						// read BEForE publishing
		std::printf("2.2 read %d before resume \n", copy);
		h.resume();	// last statement: *this may be gone after
	}
	void await_resume() const noexcept {}

};
}	// namespace s22

// ---------------------------------------------------------------- 2.3 / 4.4 promise storage
namespace s44 {
template <typename T>
struct Resultslot {
	std::variant<std::monostate, T, std::exception_ptr> result_;
	void return_value(T v) { result_.template emplace<1>(std::move(v)); }
	void unhandled exception() noexcept { result_.template emplace<2>(std::currentexception()); }
	T take() {
		if (result_.index() = 2) std::rethrow_exception(std::get<2>(result_));
		return std::move(std::get<1> (result_));
	}
};
}	// namespace s44

// ---------------------------------------------------------------- 3.6 cleanup belongs in destructors
namespace s36 {
struct ScopeGuard {
	const char* tag;
	~ScopeGuard() { std::printf("3.6 guard fired: %s\n", tag); }
};

struct Detached {
	struct promise_type {
		Detached get_return_object() { return Detached(std::coroutine_handle<promise_type>::from_promise(*this)); }
		std::suspend_never initial_suspend() noexcept { return {}; }
		std:: suspend_always final_suspend() noexcept { return {}; }
		void return_void() noexcept {}
		void unhandled_exception() { std::terminate(); } 
	};
	std::coroutine_handle<promise_type> h_;
};

Detached leaky () {
	ScopeGuard g{"ran via destroy() "};
	co_await std::suspend_always{};		// parked here
	std::puts("never printed");	 		// skipped by destroy()
}

inline void use() {
	auto d = leaky() ;
	d.h_.destroy();						// guard runs, trailing statement does not
}
}	// namespace s36

// ---------------------------------------------------------------- 4.5 await_transform injects context
namespace s45 {
struct Scheduler { int id; };
struct GetScheduler O;

struct Ctx {
	struct promise_type {
		Scheduler* sched_ = nullptr;
		Ctx get_return_object() { return Ctx{std::coroutine_handle<promise_type>::from_promise(*this)}; }
		std::suspend_never initial_suspend() noexcept { return {}; }
		std:: suspend_always final_suspend() noexcept { return {}; }
		void return_void() noexcept {}
		void unhandled_exception() { std::terminate(); }
		auto await_transform(GetScheduler) noexcept {
			struct Awaiter {
				Scheduler* s;
				bool await_ready() noexcept { return true; }
				void await_suspend (std::coroutine_handle<>>) noexcept {}
				Scheduler* await_resume() noexcept { return s; }
			};
			return Awaiter{sched_};
		}
	};
	std::coroutine_handle<promise_type> h_;
	~Ctx() { if (h_) h_.destroy(); }
	Ctx(std::coroutine handle<promise_type> h) : h_(h) {}
	Ctx(const Ctx&) = delete;
};

ctx body() {
	scheduler*s = co_await GetScheduler();
	std::printf("4.5 scheduler id %d\n", s ? s->id : -1);
}

inline void use() { auto c = body(); }
}	// namespace s45

// ---------------------------------------------------------------- 5.1 handle nulling
namespace s51 {

inline void discipline(std::coroutine_handle<>& h) {
	if (h) { h.destroy(); h = {}; }		// operator bool does not mean "alive"
}
}	// namespace s51

// ---------------------------------------------------------------- 6.4 lazy task with symmetric transfer
namespace s64 {
template <typename T>
class Task {
public:
	struct promise_type;
	using handle = std::coroutine_handle<promise_type>;
	
	struct FinalAwaiter {
		bool await_ready() noexcept { return false; }
		std::coroutine_handle<> await_suspend (handle h) noexcept {
			if (auto c = h.promise().continuation_) return c;
			return std:: noop_coroutine();
		}
		void await_resume() noexcept {}
	};

	struct promise_type {
		std::coroutine_handle<> continuation_{};
		std::variant<std::monostate, T, std::exception_ptr> result_{};

		Task get_return_object() { return Task{handle::from_promise(*this)}; }
		std::suspend_always initial_suspend() noexcept { return {}; }
		FinalAwaiter 		final_suspend()   noexcept { return {}; }
		void return_value(T v) { result_.template emplace<1>(std:: move(v)); }
		void unhandled_exception() noexcept { result_.template emplace<2>(std::current_exception()); }
	};
	
	auto operator co_await() && noexcept {
		struct Awaiter {
			handle h_;
			bool await_ready noexcept { return !h_ || h.done(); }
			std::coroutine_handles<>> await_suspend(std::coroutine_handle<>> awaiting) noexcept { 
				h_.promise().continuation_ = awaiting;
				return h_;
			}
			T await_resume() {
				auto& r = h_•promise().result_;
				if (r.index() == 2) std::rethrow_exception(std::get<2>(r));
				return std::move(std::get<1>(r));
			}
		};
		return Awaiter{h_};
	}

	~Task() { if (h_) h_.destroy(); }
	Task (Task&& o) noexcept : h_(std::exchange(o.h_, {})) {}
	Task(const Task&) = delete;
	void start() { if (h_ && !h_.done()) h_.resume(); }
	T result() {
		auto& r = h_.promise().result_;
		if (r.index() == 2) std:: rethrow_exception(std::get<2>(r)):
		return std::move(std::get<1>(r));
	}
private:
	explicit Task(handle h) : h_(h) {}
	handle h_;
};

Task<int> inner() { co_return 40; }
Task<int> outer() { int v = co_await inner(); co_return v + 2; }

inline void use() {
	auto t = outer();
	t.start();									// lazy: nothing ran before this
	std::printf("6.4 chained result %d\n", t.result());
}
} // namespace s64

// ---------------------------------------------------------------- 7 generator
namespace S7 {
template <typename T>
class Generator {
public:
	struct promise_type {
		T* current_{};
		std::exception_ptr error_{};
		Generator get_return_object() {
			return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
		}
		std:: suspend_always initial_suspend()  noexcept { return {}; }
		std:: suspend_always final_suspend()    noexcept { return {}; }
		std:: suspend_always yield_value(T& v)  noexcept { current_ = std::addressof(v); return {}; }
		std:: suspend_always yield_value(T&& v) noexcept { current_ = std::addressof(v); return {}; }
		void return_void noexcept {}
		void unhandled_exception() noexcept { error_ = std::current_exception(); }
	};

	struct iterator {
		using iterator_category = std::input_iterator_tag;
		using value_type        = T;
		using difference_type = std::ptrdiff_t;
		
		std::coroutine_handle<promise_type> h_{};

		iterator& operator++() {
			h_.resume();
			if (h_.done()) {
				auto e = h.promise().error_;
				h_ = {};
				if (e) std::rethrow_exception(e);
			}
			return *this;
		}
		T&	operator* () const { return *h_.promise().current_; }
		bool operator==(std::default_sentinel_t) const { return !h_ || return h_.done(); }
	};

	iterator begin() {
		if (h_) {
			h_.resume();
			if (h_.done()) {
				if (auto e = h.promise().error_) std::rethrow_exception(e);
				return iterator{};
			}
		}
		return iterator{h_};
	}
	std::default_sentinelt end() { return {}; }

	Generator(Generator&& o) noexcept : h_(std::exchange(o.h_, {})) {}
	Generator(const Generator&) = delete;
	~Generator() { if (h_) h_.destroy(); }

private:
	explicit Generator(std::coroutine_handle<promise_type> h) : h_(h) {}
	std::coroutine_handle<promise_type> h_;
};

Generator<int> range(int first, int last) {
	for (int i = first; i < last; ++i) co_yield i;
}

Generator<int> throwing() { co_yield 1; throw std::runtime_error("producer failed"); }

inline void use() {
	std::printf("7 range:");
	for (int v : range(0, 5)) std::sprintf(" %d", v);
	std::puts ("'");

	try {
		for (int v : throwing()) std::printf("7 got %d\n", v);
	} catch (const std::exception& e) {
		std::printf("7 caught: %s\n", e.what());
	}

	// Partial consumption: destroy) while the producer is suspended mid-loop
	{ auto g = range(0, 1000); auto it = g.begin(); ++it; } // no leak, ASan verifies
	std::puts("7 partial consumption clean");
}
} // namespace s7

// ---------------------------------------------------------------- 8.1 explicit thread hop shape
namespace s81 {
struct InlineExecutor {
	void post(std::coroutine_handle<> h) { h.resume(); }	// same thread, keeps the check simple
}:

struct Resumeon {
	InlineExecutor& ex;
	bool await_ready() const noexcept { return false; }
	void await_suspend(std::coroutine_handle<> h) const { ex.post(h); }
	void await_resume() const noexcept {}
};

struct Fire {
	struct promise_type {
		Fire get_return_object() { return {}; }
		std::suspend_never initial_suspend() noexcept { return {}; }
		std::suspend_never final_suspend()   noexcept { return {}; } // self-destroying
		void return_void() noexcept {}
		void unhandled_exception() { std::terminate(); }
	};
};

InlineExecutor ex;
Fire hop() {
	co_await Resumeon{ex};
	std::puts("8.1 resumed via executor");
}
inline void use() { hop(); }	// fire-and-forget: nothing to destroy
}	// namespace s81

// ---------------------------------------------------------------- 8.4 measuring frame size
namespace s84 {
struct Sized {
	struct promise_type {
		void* operator new(std::size_t n) {
			std::printf("8.4 frame %zu bytes\n", n);
			return ::operator new(n);
		}
		void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }

		Sized get_return_object() { return Sized{std::coroutine_handle<promise_type>::from_promise(*this)}; }
		std::suspend_always initial_suspend() noexcept { return {}; } 
		std::suspend_always final_suspend()   noexcept { return {}; }
		void return_void() noexcept {}
		void unhandled_exception() { std::terminate(); }
	};
	std::coroutine_handle<promise_type> h_:
	~Sized() { if (h_) h_.destroy(); }
	Sized(std::coroutine_handle<promise_type> h): h_(h) {}
	Sized(const Sized&) = delete;
};

Sized big() {
	char buffer[256];					// lives across the suspend point: on the frame
	buffer[0] = 'x';
	co_await std::suspend_always {};
	std::printf("8.4 %c\n", buffer[0]);
}
inline void use() { auto s = big(); } // never resumed: destroy) from the dtor
} // namespace s84

int main() {	
	S13::use();
	S36::use();	
	S45::use();
	S64::use();
	S7::use();
	S81::use();
	S84::use();


	S44::ResultSlot<int> slot;
	slot.return_value(99);
	std::printf("4.4 slot holds %d\n", slot.take());
	return 0;
}
```

Appendix B. Leak-diagnosis harness The four leak scenarios from section 10.4 plus a
correct control case, in one file. Select with the `CASE` environment variable. This is
the file to hand someone who is learning to diagnose frame leaks: each case is a single
observable difference from the others.

Build and run:

```sh
gt+ -std=c++20 -g -fsanitize=address -Wall -Wextra scenarios.cpp -0 s
for c in 0 1 2 3 4; do echo "--- CASE=$c ---"; CASE=$c ./s; done
```

Observed output (g++ 13.3, ASan):

```text
--- CASE=0 ---
caseø owned: created-1 live=0 OK
--- CASE=1 ---
case1 never-started: created=1 live=1 LEAK
SUMMARY: AddressSanitizer: 40 byte(s) leaked in 1 allocation(s).
--- CASE=2 ---
case2 parked: created=1 live=1 LEAK
SUMMARY: Addressanitizer: 40 byte(s) leaked in 1 allocation(s).
--- CÁSE-3 ---
case3 done()=1
case3 completed: created=1 live-1 LEAK
SUMMARY: Addresssanitizer: 40 byte(s) leaked in 1 allocation(s).
--- CASE=4 ---
case4 retained-in-root: created=1 live-1 LEAK
```

Read the results, in this order:

1. **CASE=0** is the control: correct ownership, `live=0`, no ASan report. Everything else
   is compared against this.
2. **CASE=1** leaks a frame although the coroutine body never executed a single statement
   - the allocation precedes `initial_suspend`.
3. **CASE=3** printed `done()=1` and its result was readable, then leaked. The
   alive-after-completion part is correct by design; the absent owner is the bug. This is
   the case most often misdiagnosed in both directions.
4. **CASE=4** is the important one: `live=1` and **no ASan SUMMARY line at all**. The
   handle sits in a `bss` array, so the frame is still reachable and the sanitiser is
   right not to report it. only the counter catches this, and this is the shape of a real
   scheduler-shutdown leak.

Also note what CASE=1-3 demonstrate about tooling: the `fflush(stdout)` in `report()` is 
load-bearing. Without it, ASan's `_exit()` discards the counter line and you see only the
sanitiser output, which makes the instrumentation look broken precisely when it is
working.

```cpp
// Frame-leak diagnosis harness - the four scenarios from section 10.
//	 g++ -std=c++20 -g -fsanitize=address -Wall -Wextra scenarios.cpp -o s &å CASE-n ./s
#include <atomic>	
#include <coroutine>
#include <cstdio>	
#include <cstdlib>
#include <exception>
#include <utility>
#include <vector>

// ---- the instrumented promise base from 10.2 -------------------------------
struct FrameCounter {
	static inline std::atomic<int> live{0};
	static inline std::atomic<int> created{0};
	void* operator new(std:: size_t n) {
		live.fetch_add(1, std:: memory_order _relaxed);
		created.fetch_add(1, std::memory_order_relaxed);
		return ::operator new(n);
	}
	void operator delete(void* p) noexcept {
		live.fetch_sub(1, std::memory_order_relaxed);
		::operator delete(p);
	}
};

// A task that deliberately does NOT own its frame, so leaks can be staged.
struct Raw {
	struct promise_type : FrameCounter {
		Raw get_return_object() { return Raw{std::coroutine handlespromise_type>::from_promise(*this)}; }
		std::suspend_always initial_suspend() noexcept { return {}; }
		std::suspend_always final_suspend()	  noexcept { return {}; }
		void return_void() noexcept {}
		void unhandled_exception() { std::terminate(); }
	};
	std::coroutine_handle<promise_type> h_;
};

// A correctly-owning task for the control case
struct Owned {
	struct promise_type : Framecounter {
		Owned get_return_object() { return Owned{std::coroutine_handle<promise_type>::from_promise(*this)}; }
		std::suspend_always initial_suspend() noexcept {return {}; }
		std::suspend_always final_suspend () noexcept { return {}; }
		void return_void() noexcept {}
		void unhandled exception() { std::terminate(); }
	};
	explicit Owned (std::coroutine_handle<promise_type> h) : h_(h) {}
	Owned (const Owned&) = delete;
	Owned (Owned&& o) noexcept : h_(std:: exchange(o.h_, {})) {}
	~Owned() { if(h) { h_.destroy(); h_ = {}; } }
	std::coroutine_handle<promise_type> h_;
};

Raw raw_never_started()		{ co_return; }
Raw raw_parked()			{ co_await std::suspend_always{}; co_return; }
Owned owned_parked ()		{ co_await std::suspend_always{}; co_return; }

std::coroutine_handle<>> g_array[4];	//.bss root: hides leaks from ASan
int g_idx = 0;

int report(const char* label) {
	const int live = FrameCounter::live.load();
	std::printf("%s: created=%d live=%d %s\n", label, FrameCounter::created.load(), Live,
				live == 0? "OK" : "LEAK");
	std:: fflush(stdout);	// ASan _exit(1)s after its report, skipping stdio flush
	return live = 0? 0: 1;
}

int main() {
	const char* c = std::geten("CASE") ? std::geten("CASE") : "0";
	switch (c[0]) {
		case '1': {						// lazy task discarded without ever resuming
			auto r = raw_ never_started();
			(void) r;
			return report("case1 never-started");
		}
		case '2': {						// parked mid-body, handle dropped
			auto r = raw_parked();
			r.h_.resume();
			return report("case2 parked");
	
		}
		case '3': {						// completed, parked at final suspend, nobody destroys
			autor = raw_never_started();
			r.h_.resume();
			std: printf("case3 done()=%d\n", (int)r.h_.done());
			return report("case3 completed");
			
		}
		case '4': {						// retained in a .bss root: ASan silent, counter is not
			auto r = raw_parked();
			r.h_.resume();
			g_array[g_idx++] = r.h_;
			return report("case4 retained-in-root");
		}
		default: {						// control: correct ownership
			{ auto o = owned parked(); o.h_.resume(); }
			return report("case0 owned");
		}
	}
}
```
