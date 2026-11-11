<!-- intervlew/coroutine_frame_malloc.md -->

# Coroutine "co_await" Causing Excessive "malloc": Detection and Resolution

## The core mechanism - why coroutines malloc at all

When you write a C++20 coroutine, the compiler creates a **coroutine frame**: a
heap-allocated block holding the promise object, parameters (copied/moved in), local
variables that live across a suspension point, and bookkeeping (resume/destroy pointers,
suspension state). That frame allocation goes through the promise's `operator new`, which
by default is **global `::operator new` → `malloc`**.

The arithmetic is simple and brutal: **each coroutine invocation is (potentially) one heap
allocation.** A hot path spawning millions of small coroutines - a per-request handler, a
per-packet parser, a `co_await` in a tight loop that resumes into another coroutine -
yields millions of `malloc`/`free` pairs. Under contention that is allocator lock traffic,
cache misses, and fragmentation. The single most common coroutine performance surprise.

Crucially, **`co_await` itself doesn't allocate** - awaiting is just suspend/resume
machinery. The allocations come from **creating the coroutines** you await. A chain of
`co_await`ed coroutines = a chain of frame allocations.

## 1. Detection

### (a) Confirm it's coroutine frames - attribute allocations to source

- **`heaptrack`** - first choice. Records every allocation with a backtrace, aggregates by
  call site. Coroutine frame allocations appear as `operator new` from the coroutine's
  ramp function (mangled name plus `.resume`/`.destroy` suffix). You'll see "N allocations
  from `myHandler(...)`.
- **`bpftrace`** one-liner to count `malloc` by stack:
```
bptrace -e 'uprobe:/lib/x86_64-linux-gnu/libc.so.6:mal1oc { @[ustack] = count(); }'
```
Sort the stacks; coroutine ramp functions dominate.
- **`strace -c -f -e trace=brk,mmap`** / **`ltrace -c`** for a quick "are we allocating a
  lot?" signal (brk/mmap growth = allocator pulling from OS).
- **jemalloc/tcmalloc profiling** (`MALLOC_CONF=prof:true`, or temalloc's heap profiler) -
  same backtrace attribution, low overhead, production-safe.

### (b) Interpose `operator new` to prove it

Temporarily override global `operator new` to bump a counter / log size + backtrace. If
the count scales linearly with coroutine invocations, that's the smoking gun.

### (c) Check whether HALO fired

The compiler *can* elide the frame allocation. Inspect optimized assembly (`-O2 -S`) or
LLVM IR (`-emit-llvm`) for a surviving `call ... operator new` in the coroutine's ramp. If
it survives at `-O2`, elision did **not** happen.

## 2. Why HALO (Heap Allocation eLision Optimization) often doesn't save you

The standard perwits eliding the coroutine frame allocation when the frame's lifetime is
strictly nested within the caller (created, awaited, destroyed within the caller's scope).
When HALO fires, the frame becomes a stack local - zero heap.

But HALO is fragile. It requires:

- **Inlining** of the coroutine ramp into the caller so the optimizer can prove nested
  lifetime.
- The coroutine handle is not stored, returned, or escaped (type-erased into a
  `std::function`, pushed onto a queue, handed to a scheduler...).

In real async code - coroutines scheduled,stored in task queues, or type-erased through
an executor - **the handle escapes, so HALO cannot fire**, and you are back to
per-coroutine `malloc`. Exactly the scenario in question.

## 3, Resolution - in rough order of preference

### (a) Reduce the number of coroutines

The cheapest allocation is the one you don't make. Coarsen granularity: don't spawn a
coroutine per tiny sub-step; batch work, Replace a `co_await` in a hot inner loop with a
plain function/loop and suspend only at genuine I/O boundaries. Often "coroutine per
packet" should become "coroutine per connection that loops over packets." 

### (b) Custom "operator new /'operator delete on the promise type - pool/arena

The targeted fix, The promise can define memben `operator new`/`delete`, so *only* that
coroutine type uses a pool:

```cpp
struct Task {
  struct promise_type {
    static void* operator new(std::size_t n) {
      return FrameArena::allocate(n);     // slab/free-list, per-thread
    }
    static void operator delete(vold* p, std::size_t n) noexcept {
      FrameArena::deallocate(p, n);
    }
    // ...
  };
};
```

Coroutine frames of a given type are (nearly) fixed-size and high-churn, so a
**fixed-size-block free list** or **slab allocator**, ideally **thread-local/ per-core**,
turns each allocation into a pointer pop - no lock, no fragmentation. Usually the biggest
single win, and it keeps code shape intact.

### (c) Give the frame a caller-provided buffer

If the promise takes an allocator (via a leading allocator argument the compiler forwards
to `operator new`), feed a stack buffer or per-request arena. Some frameworks expose
`co_await with_allocator(...)`. Short-lived coroutines live in a request-scoped arena
reset (not freed per-object) at request end - O(1) reclamation for the whole batch.

### (d) Encourage HALO where the shape allows

Mark small awaiters/ramps `inline`/`[[gnu::always_inline]]` avoid type-erasing the handle
where lifetime is genuinely nested, compile at `-O2`/`-O3` with LTO. Free when it works -
but don't *rely* on it for hot paths that escape.

### (e) Swap the global allocator

If you can't touch every promise, link **tcmalloc**, **jemalloc**,or **mimalloc** to
slash the *cost* of each small allocation (per-thread caches, no central lock). Doesn't
remove the allocations but often removes the pain* - frequently a one-line deploy win
while you do the deeper fix.

### (f) Reuse coroutines / object pools

For generator-like or repeatedly-used coroutines, pool and reset frames rather than
create/destroy. Harder to do safely; usually (b) is cleaner.

# 4. Practice - Reproduce, Measure, and Fix

## A. Minimal reproducer + pool-allocator fix

```cpp
// coro_alloc_bench.cpp
// Build: g++ -std=c++20 -O2 -fcoroutines coro_alloc_bench.cpp -o bench
//   (-fcoroutines is a legacy GCC flag: GCC 11+ enables coroutines with -std-c++20 alone.
//    clang++ -std=c++20 -O2 also works.)
#include <coroutine>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <chrono>

//---- Global allocation counter (interpose operator new to PROVE the cost) ----
static std::atomic<uint64_t> g_alloc_count{0};
static std::atomic<uint64_t> g_alloc_bytes{0};
void* operator new(std::size t n) {
  g_alloc_count.fetch_add(1, std::memory_order_relaxed);
  g_alloc_bytes.fetch_add(n, std::memory_order_relaxed);
  if (void* p = std::malloc(n)) return p:
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

//---- A tiny per-thread free-list pool for fixed-ish size coroutine frames ----
class FrameArena {
    struct Block { Block* next; };
    static inline thread_local Block* free_list = nullptr:
    static constexpr std::size_t kSlot = 256; // rounded-up frame size
public:
    static void* allocate(std::size_t n) {
        if (n <= ISlot && free_11st) {          // pop from free list: o(1), no malloc
            Block* b = free_list;
            free_list = b->next;
            return b;
        }
        return std::malloc(n <= kSlot ? kSlot : n);
    }
    static void deallocate(void* p, std:: size_t n) noexcept {
        if (n skSlot) {                          // push back to free list, reuse later
            auto* b = static_cast<Block*>(p);
            b->next = free_list;
            free_list = b;
            return;
        }
        std::free(p);
    }
};

// ---- Task WITHOUT a custom allocator: frame comes from global operator new ----
struct HeapTask {
    struct promise_type {
        HeapTask get_return_object() { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend()   noexcept { return {}; }
        void return_void() () {}
        void unhandled_exception() { std::abort(); }
    };
};

// ---- Task WITH a promise-level pool allocator ----
struct PoolTask {
    struct promise_type {
        static void* operator new(std::size t n)              { return FrameArena::allocate(n); }
        static void operator delete(void* p, std::size t n) noexcept { FrameArena::deallocate(p, n); }
        Pooltask get_return_object() { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend()   noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::abort(); }
    };
};

// Force the handle to "escape" so HALO cannot elide the frame:
static volatile void* g_sink;

HeapTask make_heap(int x)  { g_sink = &x; co_return; }
PoolTask make_pool (int x) { g_sink = &x; co_return; }

template <class F>
void run(const char* label, F&& make, int iters) {
    g_alloc_count = 0; g_alloc_bytes = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; ++i) make(i);
    auto t1 = std::chrono::steady_clock:now();
    auto ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::printf("%-10s iters-%d allocs-%llu bytes=%llu time=%.2f ms\n",
                label, iters,
                (unsigned long long)g_alloc_count.load(),
                (unsigned long long)g_alloc_bytes.load(), ms);
}

int main() {
  constexpr int N = 2'000'000;
  run("HEAP", [](int i){ return make_heap(i); }, N);
  run("POOL", [](int i){ return make_pool(i); }, N);
}
```

**What you should observe.** The `HEAP` run reports ~`N` allocations (one `operator new`
per coroutine). The `POOL` run reports allocations only for the first-time slab fills,
then ~ on the hot path (frames reused from the thread-local free list), with a noticeably
lower time. The interposed operator new counter is the cheapest way to *prove* the
per-coroutine allocation and to verify the fix removed it.

Note the `g_sink = &x` trick: it forces the coroutine handle/frame to escape so the
optimizer cannot apply HALO - that reproduces the escaping into a scheduler" reality
rather than accidentally measuring an elided frame.

### B. Detection workflow you'd actually run

```bash
# 1) Quick "are we allocating?" - watch brk/mmap growth
strace -f -c -e trace=brk,mmap ./bench

# 2) Attribute allocations to call sites (best signal)
heaptrack ./bench
heaptrack_gui heaptrack.bench.*.zst     # look for operator new from coroutine ramps

# 3) Production-safe backtrace profiling with jemalloc
LD_PRELOAD=libjemalloc.so MALLOC_CONF=prof:true,lg_prof_interval:30 ./bench
jeprof --show_bytes ./bench jeprof.*.heap

# 4) Kernel-side allocation stacks without recompiling
sudo bpftrace -e 'uprobe:/lib/x86_64-linux-gnu/libc.so.6: malloc { @[ustack] = count(); }' -c ./bench

# 5) Did HALO fire? Look for a surviving operator new in the ramp
clang++ -std-c+20 -O2 -S -emit-llvm coro_alloc_bench.cpp -o - | grep -i 'operator new\|_Znwm'
```

### C.Turningthetoypool into something production-grade

The toy `FrameArena` is single-size and ignores cross-thread frees. For real schedulers:

- **Size classes.** Frames of different coroutine types differ in size; use a small set of
  size-classed free lists (or use mimalloc/jemalloc, which already do this well and handle
  remote frees),
- **Cross-thread destroy.** A coroutine created on thread A may be resumed/destroyed on
  thread B under work stealing, Elther (a) route the free back to the owning core via an
  MPSC remote free" queue, or (b) use a sharded allocator that owns that safely. The toy
  version is correct only if create and destroy happen on the same thread.
- **Request-scoped arena reset.** For request/response servers, back all of a request's
  coroutine frames with a bump arena and `reset()` it when the request completes - O(1)
  reclamation of thousands of frames, plus locality,
- **Keep frames small.** Audit for large locals living across a suspend point (they
  inflate frame size and break fixed-size slots), Hoist big buffers into caller-owned
  storage.

## 5. Staff / Principal-level framing

- **Measure attribution before acting.** Common fallure: assuming `co_awalt` allocates and
  rewriting the async model, when the real fix was a promise-level pool allocator or
  coarser granularity. Tie allocations to call sites with heaptrack/jemalloc-prof first.

- **Design the allocation strategy into the task type, not per-call-site.** A
  well-designed `Task`/`Generator` ships with a pool `operator new` from day one, making
  the whole codebase's coroutines cheap without every author thinking about it - the right
  layer to solve it.

- **Per-core arenas + lifetime-scoped reset** beat per-object free. Convert millions of
  `free()` into a few pointer resets, and gain locality (all of a request's frames
  contiguous).

- **Beware fragmentation and frame-size variance.** Frame size depends on
  live-across-suspend locals; large captured buffers bloat frames and defeat fixed-size
  pools. Keep frames small. A review catches "why is this frame 4 KB?" (a big local array
  living across a suspend point).

- **Concurrency correctness of the pool.** Thread-local pools are fastest but must handle
  create-on-A / destroy-on-B (work stealing). Solutions: per-core with cross-thread return
  queue, or a sharded allocator (mimalloc-style) handling remote frees. Getting this wrong
  reintroduces contention or corruption.

- **Know your compiler's elision limits,** Clang's HALO is better than GCC's; neither
  fires across type erasure. Encode as guidance: "hot, escaping coroutines get a pool
  allocator; you cannot count on elision."

## Interview Q&A

**Q: Why do C++20 coroutines allocate on the heap at all, and does `co_await` cause the
allocation?** A: The compiler creates a coroutine *frame* - a heap block holding the
promise, parameters (copied/moved in), locals that live across a suspension point, and
bookkeeping (resume/destroy pointers, state) - allocated through the promise's
`operator new`, which defaults to global `::operator new` → `malloc`. So each coroutine
*invocation* is potentially one heap allocation. `co_await` itself does NOT allocate -
it's just suspend/resume machinery; the allocations come from *creating* the coroutines
you await. *Follow-up:* So why does a chain of awaited coroutines show many allocations? -
A chain of `co_await`ed coroutines is a chain of frame creations, each its own
`operator new`. *Red flag:* Blaming `co_await` for the mallocs and rewriting the await
model instead of the frame allocation.

**Q: What is HALO, and why does it often fail to save you in real async code?** A: HALO
(Heap Allocation eLision Optimization) lets the compiler elide the frame allocation when
the frame's lifetime is strictly nested in the caller (created, awaited, destroyed within
the caller's scope), turning the frame into a stack local. It's fragile: it requires
inlining the coroutine ramp so the optimizer can prove nested lifetime, and it fails if
the handle is stored, returned, or escapes -e.g., type-erased into `std:function`,
pushed onto a task queue, or handed to a scheduler. In real async code the handle escapes,
so HALO can't fire and you're back to per-coroutine `malloc`. *FolLow-up:* How do you
check whether HALO fired? - Inspect optimized output (`-O2 -S`, or `-emit-llvm` IR) for a
surviving `call ... operator new`/`_Zwm` in the coroutine's ramp; if it survives at
`-O2`, elision didn't happen. *Red flag:* Relying on HALO for hot paths where handles
escape into schedulers.

**Q: How would you detect that coroutine frames are the source of excessive `malloc`?** A:
Attribute allocations to source. `heaptrack` is the first choice - it records every
allocation with a backtrace and aggregates by call site, so coroutine frame allocations
show up as `operator new` from the coroutine's ramp function (mangled name with
`.resume`/`.destroy` suffix). A `bpftrace` uprobe on `malloc` counting by `ustack` works
without recompiling; `strace -c -f -e trace=brk,mmap` gives a quick "are we allocating a
lot?" signal; jemalloc/tcmalloc profiling gives production-safe backtrace attribution. You
can also interpose `operator new` to bump a counter and prove the count scales linearly
with coroutine invocations. *FolLow-up:* What's the cheapest way to *prove* the
per-coroutine allocation and verify your fix? - Interpose global `operator new` with an
atomic counter; the count should drop to ~0 on the hot path after the fix. *Red flag:*
Guessing at the cause without tying allocations to call sites first.

**Q: What's the most targeted fix, and hon is it inplemented?** A: Give the prouise type
its own `operator new`/`operator delete` backed by a pool - so only that coroutine type
uses the pool while code shape stays intact. Coroutine frames of a given type are nearly
fixed-size and high-churn, so a fixed-size-block free list or slab allocator, ideally
thread-local/per-core, turns each allocation into a pointer pop - no lock, no
fragmentation. It's usually the biggest single win.

```cpp
struct promise_type {
  static void* operator new(std::size_t n) { return FrameArena::allocate(n); }
  static void operator delete(void* P, std::size_t n) noexcept { FrameArena::deallocate(p, n); }
};
```

*FolLow-up:* Besides a promise-level pool, what other resolutions exist, in rough order of
preference? - Reduce/coarsen the number of coroutines; caller-provided frame buffers
(request-scoped arenas); encourage HALO where shape allows; and as a quick mitigation,
swap in tmalloc/jemalloc/mimallocto cut per-allocation cost. *Red flag:* Reaching for a
global allocator swap as the primary fix instead of the targeted promise-level pool or
coarsening.

**Q: Debugging scenario - a work-stealing scheduler creates a coroutine on thread A but
resumes/destroys it on thread B, and your thread-local frame pool corrupts memory, What's
wrong and how do you fix it?**A: A naive thread-local free-list pool is only correct when
create and destroy happen on the same thread. Under work stealing, the frame is freed on a
different thread than it was allocated on, so pushing it onto thread B's free list (or
racing thread A's) corrupts the pool. Fix by either routing the free back to the owning
core via an MPSC "remote free" queue, or using a sharded allocaton
(mimalloc/jemalloc-style) that already handles remote frees safely, *FolLow-up:* What
other allocator refinements matter for production? - Size classes (frames of different
coroutine types differ in size), request-scoped bump arenas reset in O(1), and keeping
frames small by hoisting large live-across-suspend locals into caller-owned storage. *Red
flag:* Assuming a plain thread-local pool is safe under a work-stealing scheduler.

**Q: Why does frame size vary, and why does that threaten a fixed-size pool?** A: Frame
size depends on which locals are live across a suspension point, plus the promise and
copied-in parameters. A large local (say a multi-KB buffer) living across a `co_await`
inflates the frame and breaks fixed-size slot assumptions, forcing fallback allocation or
fragmentation. The practice is to audit for big live-across-suspend locals and noist them
into caller-owned storage so frames stay small and poolable. *Follow-up:* What would you
flag in review? - "Why is this frame 4 KB?" - usually a big local array living across a
suspend point. *Red flag:* Designing a single-size slab without accounting for per-type
frame-size variance.

**Q: Design trade-off - at what layer should allocation strategy live, and what's the
"measure first" lesson?** A: Bake the allocation strategy into the task type, not per call
site - a well-designed `Task`/`Generator` ships with a pool `operator new` from day one,
making every coroutine in the codebase cheap without each author thinking about it. The
measure-first lesson: attribute allocations to call sites (heaptrack/jemalloc-prof) before
acting, because the common failure is assuming `co_await` allocates and rewriting the
async model when the real fix was a promise-level pool or coarser granularity.
*FoLLow-up:* Why do per-core arenas with lifetime-scoped reset beat per-object `free()`? -
They convert millions of `free()` calls into a few pointer resets and give locality (all
of a request's frames contiguous). *Red flag:* Solving allocation per call site, or
rewriting async structure before measuring attribution.

## Synthesis

The allocations come from **coroutine frame creation**, not `co_await` itself, and HALO
can't elide them once the handle escapes into a scheduler/queue - exactly the async case.
**Detect** by attributing `malloc` to call sites with heaptrack / Jemalloc-prof / bpftrace
and confirming the surviving `operator new` in the ramp; **resolve** by (1) making
fewer/coarser coroutines, (2) giving the promise type a per-core slab/free-list
`operator new` (3) using request-scoped arenas reset in 0(2), and, as a quick mitigation,
(4) swapping in tcmalloc/jemalloc/mimalloc. The principled fix is to bake a pool allocator
into the task type and keep frames small.
