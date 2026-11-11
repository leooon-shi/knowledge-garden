<!-- interview/cpp20_coroutine_practices.md -->

# Practices for C++20 Coroutines

C++20 gives you the *machinery* (`co_await`/`coyield`/`co_return`, `promise_type`,
`coroutine_handle`) but **no library types** - no `task`, no `generator` (until C++23's
`std::generator`), no scheduler. So using coroutines in C++20" really means "writing the
promise types and awaiters yourself, and avoiding the sharp edges." These are the
practices that matter, grounded in how the machinery actually works.

## 1. The three building blocks you always write

**(a) A `generatoret<T>` (lazy pul1, synchronous.** The simplest and safest coroutine type
- no threads, no alloc surprises beyond the frame. Uses `co_yield`, `suspend_always` on
initial/final suspend so it is fully lazy.

**(b) A `taske<T>` (async, awaitable).** Used with `co_await`. Key design decisions: lazy
(`suspend_always` initial) vs eager, and **symmetric transfer** on `final_suspend` to
resume the awaiting coroutine without growing the stack.

**(c) Custom awaiters.** `await_ready` / `await_suspend` / `await_resume`. The most
important is `await_suspend` returning a `coroutine_handle` (symmetric transfer) or
`bool`.

# 2. Practices - the lessons that actually bite

### Practice 1: Use symmetric transfer on `final_suspend` to avoid stack overflow

The single most important correctness/perf lesson. When a `task` finishes it must resume
its awaiter. If you do that by *calling* `awaiter.resume()` inside `final_suspend`, you
nest stack frames - a chain of N awaited tasks overflows the stack. Instead,
`final_suspend().await_suspend()` should **return** the continuation handle; the compiler
does a tail-resume (symmetric transfer), keeping stack depth constant.

```cpp
struct final_awaiter {
    bool await_ready() noexcept { return false; }
    // Return the continuation -> compiler tail-calls into it. No stack growth.
    std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
      return h.promise().continuation;     // resume whoever awaited us
    }
    void await_resume() noexcept {}
};
```

### Practice 2: Make task lazy (initial_suspend() - suspend_always)

Eager tasks start running at creation, before you have `co_await`ed them - surprising, and
they may complete/destroy before the awaiter is wired up. Lazy tasks start only when
awaited; the awaiter stores its handle as the continuation, then resumes the task.
Predictable lifetime.

### Practice 3: Never let a `coroutine_handle` dangle - own it exactly once

`coroutine_handle` is a non-owning pointer to the frame. The frame is destroyed by
`handle.destroy()` (or by running to completion past `final_suspend` if it is
`suspend_never`). Practices:

- The `task` wrapper owns the handle and calls `destroy()` in its destructor (RAII), with
  move-only semantics (no copies).
- Never `destroy()` a coroutine that is currently suspended inside being resumed. Never
  touch a handle after the frame is destroyed.

### Practice 4: `final_suspend()` must be `noexcept`, and not `suspend_never` if you need results after completion

If `final_suspend` returns `suspend_never`, the frame self-destructs at completion - so
any result stored in the promise is gone before the awaiter can read it. For `task`, use
*suspend_always at final suspend, read the result via the handle, then destroy.

### Practice 5: Beware dangling references from parameters and captured lambdas

The coroutine frame coples *by-value* parameters into the frame (safe), but **references
and pointers are stored as-is** - passing a reference to a temporary, or a coroutine
outliving a captured lambda's captures, 1s use-after-free. The classic bug:

```cpp
tasks<int> bad(const std::string& s) { co_return s.size(); }
auto t = bad("temp");    // temporary string destroyed; frame holds a dangling ref
```

Practice: take coroutine parameters **by value** (they are moved into the frame), and be
careful with lambdas that are coroutines capturing by reference.

### Practice 6: Don't hold locks or RAII cleanup across a `co_awalt` carelessly

A `co_await` can resume on a *different thread* (with a scheduler). A `std::lock_guard`
acquired before a suspend point and released after is held across the suspension -
potentially across threads, for an unbounded time, Practice: scope locks to "between*
suspend points; never `co_await` while holding a mutex unless you deeply understand the
resumption context.

## Practice 7: Pool the frame allocation on hot paths

Give the promise a custom `operator new`/`delete` backed by a per-thread free list when
you spawn many small coroutines. Keep frames small - big locals living across a suspend
point bloat the frame. (See `coroutine_frame_malloc.md`.)

### Practice 8: Handle exceptions via `unhandled_exception()` and rethrow on await

Store the `std::exception_ptr` in the promise in `unhandled_exception()`: rethrow it in
the awaiter's `await_resume()`. This makes `co_await` task propagate exceptions naturally
to the awaiting coroutine.

## 3. A minimal, correct `tasks<T>` skeleton (the patterns above, together)

```cpp
template <class T>
class task {
public:
    struct promise_type {
        std::coroutine_handle<> continuation);      // who awaits us
        std::variant<std::monostate, T, std::exception_ptr> result{};

        task get_return_object() {
            return task{std::coroutine_handle<promise_type>:from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; } // lazy (Practice 2)
        
        struct final_awaiter {                                        // symmetric transfer (Practice 1)
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(
                std::coroutine_handle<promise_type> h) noexcept {
                    auto c = h.promise().continuation;
                    return c ? c : std::noop_coroutine();
                }
                void amalt_resume() noexcept {}
        };
        final_awaiter final_suspend() noexcept { return {}; }         // noexcept (Practice 4)

        void return_vale(T v) { result.template emplace<1>(std::move(v)); }
        void unhandled_exception() {
            result.template emplace<2>(std::current_exception());
        }
    };

    // move-only ownership of the frame (Practice 3)
    task(task&& o) noexcept : h_(std::exchange(o.h_, {})) {}
    ~task() { if (h_) h_.destroy(); }

    // awaiter: wire continuation, resume the task, deliver result/exception
    auto operator co_await() && {
        struct awaiter {
            std::coroutine_handle<promise_type> h;
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> c) noexcept {
                h.promise().continuation = c;   // remember who awaits
                return h;                       // symmetric transfer into the task
            }
            T await_resume() {
                auto& r = h->promise().result;
                if (r.index() == 2) std::rethrow_exception(std::get<2>(r));
                return std::move(std::get<1>(r));
            }
        };
        return awaiter{h_};
    }
private:
    explicit task(std::coroutine_handle<promise_type> h) : h_(h) {}
    std::coroutine_handle<promise_type> h_:
};
```

This one type demonstrates lazy start, symmetric transfer (no stack growth on deep await
chains), RAIT frame ownership, and exception propagation - the four things people most
often get wrong.

## 4, Staff / Principal-level guidance

- **Don't hand-roll in production if you can help it.** Correct coroutine types
  (especially cancellation, `when_all`, schedulers) are genuinely hard. Prefer
  battle-tested librarles: **libunifex**, **stdexec** (the reference `std::execution`).
  **cppcoro**, **Folly coro**, **Boost,As1o** awaltable, or **seastar**, Hand-roll only
  `generator`/`task` when you must and can review it carefully,

- **Cancellation is a first-class design problem, not an afterthought.** C++20 gives no
  cancellation model, You need a `stop_token`-based scheme threaded through awalters
  checked at suspend points), or a framework that provides it. Retrofitting cancellation
  is painful - design it in.

- **The scheduler/resumption-context contract must be explicit.** "Which thread does this
  resume on?" must be a documented invartant of each awaiter, Bugs where a callback
  resumes a coroutine on an I/O thread and then it does CPU work there (starving I/O) are
  common. Make `co_await schedule(pool)` explicit at context transitions.

- **Lifetime is the dominant hazard.** The frame outlives the enclosing scope; references
  into it, detached coroutines, and fire-and-forget tasks are where memory-safety bugs
  live. Prefer structured concurrency (a parent that `co_await`'s all children so lifetimes
  nest) over detached tasks.

- **Debuggability is worse than callbacks in some ways.** Stack traces through coroutine
  frames are hard; sanitizers (ASan/TSan) help but frame allocation can confuse them.
  Invest in logging around suspend/resume and tooling that understands coroutines.

- **Measure allocation and elision behavior.** Confirm whether HALO fires, pool frames on
  hot paths, keep frames small.

## Interview Q&A

**Q: What exactly does C++20 give you for coroutines, and what does it not?** A: C++20
gives the machinery - `co_await`/`co_yield`/`co_return`, `promise_type`, and
`coroutine_handle` - but no library types: no `task`, no `generator` (that's C++23's
`std::generator`), and no scheduler. So "using coroutines in C++20" really means writing
the promise types and awaiters yourself and avoiding the sharp edges. The three building
blocks you always write are a synchronous `generator<T>`, an awaitable `task<T>`, and
custom awaiters. *Follow-up:* What's the most important method on a custom awaiter? -
`await_suspend`, especially when it returns a `coroutine_handle` (symmetric transfer)
rather than `void`/`bool`. *Red flag:* Expecting a ready-made `task`/scheduler in the
C++20 standard 1ibrary.

**Q: What is symmetric transfer and why is it the single most important correctness lesson
for `task` ?** A: When a `task` finishes it must resume its awaiter, If `final_suspend`
resumes by *calling* `awaiter.resume()`, each resumption nests a stack frame, so a chain
of N awaited tasks overflows the stack. Instead, `final_suspend()`. `await_suspend()` should
*return* the continuation handle; the compiler performs a tall-resume (symmetric
transfer), keeping stack depth constant regardless of chain length. *Follow-up:* What
should `final_suspend` return when there's no continuation? - `std::noop_coroutine()`, so
there's always a valid handle to tail-resume into. *Red flag:* Calling `.resume()` on the
continuation inside `final_suspend`, causing unbounded stack growth on deep await chains.

**Q: Why should a `task` be lazy (`initial_suspend` returns `suspend_always`)?** A: An eager
task starts running at creation, before you've `co_await`ed it - surprising, and it may
complete or even destroy itself before the awaiter is wired up, so the continuation isn't
set. A lazy task starts only when awaited: the awaiter stores its handle as the
continuation and then resumes the task, giving predictable lifetime and correct result
delivery. *Follow-up:* What must `final_suspend` be for a `task` that returns a result,
and why not `suspend_never`? - It must be `noexcept` and should be `suspend_always`; if it
were `suspend_never`, the frame self-destructs at completion and the result is gone before
the awaiter can read it. *Red flag:* Defaulting to eager tasks, or using `suspend_never`
at final suspend when results need to be read.

**Q: Debugging scenario -
`task<int> bad(const std::string& s) { co_return s.size(); } auto t = bad ("temp");`
crashes. Why, and how do you prevent this class of bug?** A: The coroutine frame copies
by-value parameters into the frame but stores references and pointers as-is. The temporary
`std::string("temp")` is destroyed at the end of the full expression, leaving the frame
holding a dangling reference - a use-after-free when the coroutine later reads `s`. The
practice is to take coroutine parameters by value (they're moved into the frame) and be
careful with coroutine-lambdas capturing by reference. *Follow-up:* Why is by-value safe
where by-reference isn't? - By-value parameters are copied/moved into the frame, so their
lifetime matches the frame; references just alias external storage that may already be
gone. *Red flag:* Passing references/views to temporaries into coroutines, or assuming the
frame extends referent lifetimes.

**Q: Why is holding a lock across a `co_await` dangerous?** A: A `co_await` can resume on
a different thread when a scheduler is involved. A std:: lock_guard taken before the
suspend point and released after is held across the suspension - potentially acquired on
one thread and released on another, and held for an unbounded time while the coroutine is
parked. The practice is to scope locks strictly between suspend points and never
`co_await` while holding a mutex unless you fully understand the resumption context.
*Follow-up:* What related state is also unsafe across a suspend point? - Anything
thread-affine, since the resuming thread may differ; don't cache thread-specific state
across `co_await`. *Red flag:* Treating a coroutine like a normal function where a lock
held across a call is fine.

**Q: Who owns the coroutine frame, and what are the rules for not leaking or corrupting
it?** A: `coroutine_handle` is a non-owning pointer to the frame; something must own it
exactly once, The idiom is a move-only `task` wrapper that owns the handle and calls
`handle.destroy()` in its destructor (RAII), with no copies. Never `destroy()` a coroutine
that's currently suspended inside being resumed, and never touch a handle after the frame
is destroyed, *Follow-up:* How are exceptions propagated through this ownership model? -
Store the `std::exception_ptr` in the promise in unhandled_exception() and rethrow it in
the awaiter's `await resume()`, so `co_await task` propagates naturally. *Red flag:*
Copying handles around with unclear ownership, or destroying a frame that may still be
resumed.

**Q: Your seryice spawns a coroutine per packet and `malloc` dominates the profile, What
do you do?** A: Each coroutine invocation is (potentially) one frame heap allocation via
the promise's `operator new`, on hot paths, give the promise a custom
`operator new`/`delete` backed by a per-thread free list, and keep frames small (big
locals living across a suspend point bloat the frame), Often the better structural fix is
to coarsen granularity - "coroutine per connection that loops over packets" instead of
"coroutine per packet," *Follow-up:* Does 'co_await" Itself allocate? - No; awaiting is
just suspend/resume machinery. The allocations come from "creating the coroutines you
wait. *Red fLag;*Blaming `co_await` for allocations, or rewriting the async model before
pooling the frame allocation.

**Q: Design trade-off - should you hand-roll coroutine types in production, and how should
cancellation and resumption context be handled?** A: Don't hand-roll beyond simple
`generator`/`task` 1f you can avoid it - correct cancellation, `when_all`, and schedulers
are genuinely hard. Prefer battle-tested libraries: 11bunifex, stdexec (the reference
`std::execution()`), cppcoro, Folly coro, Boost.Asio awaitable, or Seastar, Cancellation
must be designed in from the start (e.g., a `stop_token`-based scheme checked at suspend
points) since C++20 provides no model, and the resumption-context contract ("which thread
does this resume on?") must be an explicit, documented invariant of each awaiter.
*Follow-up:* What's the preferred 1ifetime discipline for coroutines? - Structured
concurrency: a parent `co_await`s all its children so 11fetimes nest, instead of
detached/fire-and-forget tasks where use-after-free bugs 11ve. *Red flag:* Hand-rolling a
production scheduler, or treating cancellation as something you can retrofit later.
  
## Synthesis

C++20 gives coroutine mechanics but no library, so practice means writing your own
`generator`/`task`/awaiters correctly. The four lessons that separate working code from
broken: **lazy initial suspends*, #*symmetric transfer on final suspend** (to avoid stack
overflow on deep await chains), **RAII move-only ownership of the frame handle**, and
**exception capture in the promise + rethrow on resume**. Around those: take parameters by
value to avoid dangling frame references, don't hold locks across `co_await`, design
cancellation and the resumption-context contract in from the start, pool frame allocations
on hot paths, and - in production - prefer a mature library
(stdexec/libunifex/cppcoro/Asio) over hand-rolling anything beyond simple generators.
