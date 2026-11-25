<!-- cpp/concurrency.md -->

# Concurrency & Parallelism in Modern C++

Covers the study plan's **Week 3, Days 1-4**: threading, the memory model, synchronization
primitives, atomics, and lock-free programming. Day 5 (coroutines) has its own doc
(`cpp_coroutines.md`) and exercise set (`exercises/`).

> Verified on **g++ 13**, `-std=c++20 -pthread`. Examples that demonstrate data races are
> labelled as such - build correctness-sensitive code with sanitizers (see the README).
> Note: on some hardened kernels Threadsanitizer aborts with "unexpected memory mapping";
> if so, either lower ASLR entropy (`sudo sysctl vm.mmap_rnd_bits=28`) of rely on
> Addresssanitizer + careful review.

## Table of Contents

1. [Threads: `std::thread` and `std::jthread`](#1-threads)
2. [The C++ memory model](#2-the-c-memory-model)
3. [synchronization primitives](#3-synchronization-primitives)
4. [Atomics](#4-atomics)
5. [`memory_order` in practice](#5-memory order-in-practice)
6. [Lock-free programming](#6-lock-free-programming)
7. [Futures, promises, async](#7-futures-promises-async)
8. [Correctness tooling](#8-correctness-tooling)
9.  [Practice](#9-practice)

## 1. Threads

`std::thread` runs a callable on a new OS thread. You **must** `join()` or `detach()`
before it is destroyed, or the program calls `std::terminate`.

```cpp
#include <thread>
#include <iostream>
void work(int id) { std::cout << "thread " << id << '\n'; }

int main() {
  std::thread t(work, 1);
  t.join();                 // wait for completion; forgetting this => terminate()
}
```

### `std::jthread`(C++20) - prefer this

`std::jthread` fixes the two footguns of `std::thread`:

- Its destructor **automatically joins** (RAII - no more manual `join()`).
- It carries a **`std::stop_token`** for cooperative cancellation.

```cpp
#include <thread>
#include <atomic>

std::atomic<long> ticks{0};
{
    std::jthread w([](std::stop_token st){
        while (!st.stop_requested())          // cooperative: check the token
          ticks.fetch_add(1, std::memory_order_relaxed);
    });
    // ... do other work ...
}   // w's destructor calls request_stop() then join() - clean shutdown
```

Cancellation is *cooperative*: `request_stop()` only flips the token; the thread must poll
`stop_requested()` (or register a `std::stop_callback`). There is no forced kill - that's
by design, because forcibly killing a thread mid-operation can't run destructors or
release locks safely.

## 2. The C++ memory model

Multithreaded C++ is defined in terms of three relations. Understanding them is what
separates "it works on my machine" from correct concurrent code.

- **sequenced-before** - ordering within a single thread.
- **synchronizes-with** - a release operation synchronizes with an acquire operation that
  reads the value it wrote. This is how ordering crosses threads.
- **happens-before** - the transitive closure of the above. If A happens-before B, A's
  effects are visible to B.

**Data race** = two threads access the same memory location, at least one writes, and
there is no happens-before relation between them. A data race is **undefined behaviour** -
not "you get a stale value", but "the standard says anything may happen". You remove races
by establishing happens-before, via a mutex or atomics.

```cpp
// DATA RACE (undefined behaviour): two threads touch `x`, one writes, no sync.
int x = 0;
std::jthread a([&]{ x = 1; });      // write
std::jthread b([&]{ int y = x; });  // read -- UB


```

The fix is either a mutex (§3) or making `x` a `std::atomic<int>` (§4).

## 3. Synchronization primitives

### Mutexes and locks

Never lock/unlock a mutex by hand - use RAII lock guards so unlocking happens on every
path, including exceptions.

```cpp
#include <mutex>

std::mutex m;
int shared = 0;

void inc() {
  std::lock_guard lk(m);   // locks now, unlocks at end of scope
  ++shared;                // critical section
}
```

| Lock type | Use for |
| ------------------ | ---------------------------------------------------- |
| `std::lock_guard`  | simplest scoped lock, one mutex                      |
| `std::unique_lock` | movable, deferrable, works with `condition_variable` |
| `sta::scoped_lock` | lock **multiple** mutexes deadlock-free (C++17)      |
| `std::shared_lock` | shared (reader) lock on a `std::shared_mutex`        |
 
`std::shared_mutex` is a reader-writer lock: many `shared_lock` readers OR one
`unique_lock` writer.

```cpp
#include <shared_mutex>
std::shared_mutex sm;
// readers:  std::shared_lock rl(sm);   // concurrent
// writer:   std::unique_lock wl(sm);   // exclusive
```

### `condition_variable` – wait for a condition

Always use the predicate overload of `wait`; it re-checks the condition and correctly
handles **spurious wakeups** (a `wait` may return without a notify).

```cpp
#include <condition_variable>
#include <mutex>
#include <queue>

std::mutex m;
std::condition_variable cv;
std::queue <int> q;

void producer(int v) {
    { std::lock_guard lk(m); q.push(v); }
    cv.notify_one();
}
int consumer() {
    std::unique_lock lk(m);
    cv.wait(lk, [&]{ return !q.empty(); });   // predicate guards against spurious wakeups
    int v = q.front(); q.pop();
    return v;
}
```

### C++20 additions: semaphores, latch, barrier

```cpp
#include <semaphore>
#include <latch>
#include <barrier>

// counting_semaphore: a permit counter. Great for bounding concurrency.
std::counting_semaphore<8> slots{4};   // at most 4 holders at once
slots.acquire(); /* ...use resource...*/ slots.release();

// binary_semaphore == counting semaphore<1>; a simple signal.
std::binary_semaphore signal{0};
// thread A: signal.acquire();   // blocks until B signals
// thread B: signal.release();

// latch: one-shot countdown. Threads count_down); waiters block until it hits 0.
std::latch done{3};
// each worker: done.count_down();
// coordinator: done.wait();

// barrier: reusable rendezvous for a fixed set of threads (phases).
std::barrier sync{2};
// each of 2 threads: sync.arrive_and wait();  // all must arrive before any proceed
```

Rule of thumb: **latch** for "wait until N things happen once"; **barrier** for
"repeatedly sync a fixed group between phases"; **semaphore** for "at most N at a time" or
lightweight signaling.

## 4.Atomics

`std::atomic<T>` provides indivisible operations with defined cross-thread ordering – the
race-free alternative to a mutex for simple shared state.

```cpp
#include <atomic>

std::atomic<int> counter{0};
counter.fetch_add(1);              // atomic ++, returns previous value
int v = counter.load();            // atomic read
counter.store(10);                 // atomic write
int old = counter.exchange (5);    // set to 5, return previous
```

### compare-exchange – the workhorse of lock-free code

`compare_exchange_weak(expected, desired)` : atomically, *if* the value equals `expected`,
set it to `desired` and return true; otherwise load the current value into `expected` and
return false. The canonical retry loop:

```cpp
std::atomic<int> valuef{0};

// Atomically double the value, retrying if another thread interfered.
int cur = value.load();
while (!value.compare_exchange_weak(cur, cur * 2)) {
  // cur was refreshed with the latest value; loop and try again
}
```

`_weak` may fail spuriously (cheaper on some ISAS) so it belongs in a loop; `_strong`
fails only on a real mismatch - use it when there's no loop.

### `std::atomic_ref` (C++20)

Apply atomic operations to an object you don't own as an atomic - e.g. one element of a
plain array:

```cpp
int data[100] = {};
std::atomic_ref<int> r{data[7]};
r.fetch_add(1);   // atomic RMW on data[7]
```

## 5. `memory_order` in practice

Every atomic op takes an optional ordering. The default, `seq_cst`, is the easiest to
reason about (a single global order of all seq_cst ops) and the right choice until
profiling says otherwise. Relaxing ordering trades reasoning-simplicity for speed.

| Order     | Meaning                                       | Typical use                                |
| --------- | --------------------------------------------- | ------------------------------------------ |
| `relaxed` | atomicity only, no ordering                   | counters/stats where order doesn't matter  |
| `acquire` | no reads/writes after it can move before      | consumer side of a handoff (a load)        |
| `release` | no reads/writes before it can move after      | producer side of a handoff (a store)       |
| `acq_rel` | both, for read-modify-write ops               | `fetch_*` that both consumes and publishes |
| `seq_cst` | acquire/release **plus** a single total order | default; when in doubt                     |

The **release/acquire handshake** is the key pattern: a `release` store *publishes*
everything the writer did before it; a matching `acquire` load *sees* all of it.

```cpp
std::atomic<bool> ready{false};
int payload = 0;

// producer
payload = 42;                                  // (1) ordinary write
ready.store(true, std::memory_order_release);  // (2) publishes (1)

// consumer
while (!ready.load(std::memory_order_acquire)) {}  // (3) waits for (2)
assert (payload == 42);                            // guaranteed: (1) happens-before here
```

`relaxed` is safe for a pure counter because you only care that increments aren't lost,
not about ordering relative to other data:

```cpp
std::atomic<long> hits{0};
hits.fetch_add(1, std::memory_order_relaxed);   // fine for statistics
```

## 6. Lock-free programming

"Lock-free" = at least one thread always makes progress; no mutexes, so no priority
inversion or deadlock. It's built on atomics + compare-exchange. It is also **hard** - get
the ordering wrong and you have a race that only shows up under load on one CPU model.
Reach for it only when profiling proves a mutex is the bottleneck, and test with
sanitizers + stress.

### SPSC ring buffer (single producer, single consumer)

The easiest correct lock-free structure: because there's exactly one producer and one
consumer, a bounded ring needs only two atomic indices and a release/acquire handshake. No
CAS loop required.

```cpp
#include <array>
#include <atomic>
#include <optional>
#include <cstddef>

template <typename T, std::size_t Cap>          // Cap must be a power of two
class SpscRing {
  static_assert((Cap & (Cap - 1)) == 0, "Cap must be a power of two");
  std::array<T, Cap> buf_{};
  std::atomic<std::size_t> head_(0);           // consumer advances this
  std::atomic<std::size_t> tail_(0);           // producer advances this
public:
  bool push(const T& v) {                      // producer only
      auto t = tail.load(std::memory_order_relaxed);
      auto next = (t + 1) & (Cap - 1);
      if (next == head_.load(std::memory_order_acquire)) return false;  // full
      buf_[t] = v;
      tail_.store(next, std::memory_order_release);   // publish the write
      return true;
  }
  std::optional<T> pop() {                     // consumer only
      auto h = head_.load(std::memory_order_relaxed);
      if (h == tail_.load(std::memory_order_acquire)) return std::nullopt; // empty
      T v = buf_[h];
      head_.store((h + 1) & (Cap - 1), std::memory_order _release);
      return v;
  }
};
```

Why it's correct: the producer's `release` store to `tail_` publishes the slot it just
wrote; the consumer's `acquire` load of `tail_` synchronizes-with it, so the element read
is guaranteed to see the written value. Verified correct passing 1,000,000 items between
two threads (sum matches).

### Treiber stack (multi-producer/consumer) and the ABA problem

A lock-free stack pushes/pops via CAS on the head pointer:

```cpp
// push: new_node->next = head; while(!head.compare_exchange_weak(new_node->next, new_node)){}
```

Multi-consumer pop hits the **ABA problem**: thread 1 reads head = A, stalls; thread 2
pops A, pops B, pushes A back; thread 1's CAS sees "still A" and succeeds – but the stack
changed underneath it. Mitigations: tagged pointers (a version counter packed with the
pointer), hazard pointers, or epoch-based reclamation. This is exactly why "just use a
mutex" is usually the right call.

## 7. Futures, promises, async

For one-shot "compute a value on another thread and get it back", futures are simpler and
safer than raw threads.

```cpp
#include <future>

int expensive(int x) { return x * x; }

std::future<int> f = std::async(std::launch::async, expensive, 7);
// ... do other work ...
int result = f.get();   // blocks until ready; 49
```

- `std::promise<T>` / `std::future<T>` - one thread sets a value, another `get()`s it.
  `std::async` wraps this common case.
- `std::packaged_task<Sig>` - a callable that stores its result in a future; useful for
  building a thread pool's task queue.

Limitations of `std::async` worth knowing: no continuations (`.then`), and the future
returned by `std::async` **blocks in its destructor** until the task finishes – a subtle
source of accidental serialization. For real workloads, build a thread pool with
`packaged_task` (or use the coroutine scheduler from
`exercises/exercise_coro_7_scheduler.cpp`).

## 8. Correctness tooling

Concurrency bugs are non-deterministic; tooling is not optional.

```bash
# ThreadSanitizer – detects data races at runtime. Run your tests under it.
g++ -std=c++20 -fsanitize=thread -g -pthread -o t file.cpp && ./t

# AddressSanitizer + UBSan – use-after-free, OOB, UB.
g++ -std=c++20 -fsanitize=address,undefined -g -o t file.cpp && ./t
```

- Run TSan on any code with shared mutable state. A clean single run isn't proof (races
  are probabilistic) - run under load, many iterations.
- Never combine TSan with ASan in one binary.
- Stress-test lock-free code: many threads, many iterations, compare against a mutex-based
  reference (the technique used to validate the tree structures in `../` – cross-check
  against a known-correct implementation).

## 9. Practice

Ordered to match the plan's Week 3.

1. **Producer/consumer queue** (Day 1) – a mutex + `condition_variable` bounded queue. Run
   it under TSan.
2. **Thread pool** (Day 2) – `std::jthread` workers pulling from a task queue guarded by 
   `condition_variable`; then rewrite dispatch using a "counting semaphore. Compare with
   the coroutine scheduler in `exercises/`.
3. **Relaxed vs seq_st counter** (Day 1/3) – increment an atomic from threads with
   `relaxed`, verify the total is still exact; reason about why ordering doesn't matter
   here.
4. **SPSC ring buffer** (Day 3) – implement the buffer above from scratch; benchmark it
   against a `std::mutex` + `std::queue` version; run under TSan.
5. **Break it on purpose** – remove the `release`/`acquire` pairing from the ring buffer
   (make everything `relaxed`) and see whether TSan or a stress test catches the resulting
   race.

### Key takeaways

- Prefer `std::jthread` (auto-join + `stop_token`) over `std::thread`.
- Remove data races by establishing happens-before: a mutex, or atomics with a
  release/acquire handshake.
- `seq_cst` is the safe default; relax ordering only with a specific reason and
  sanitizer/stress validation.
- Lock-free is a last resort justified by profiling; SPSC is the one case that's genuinely
  simple. Everything harder wants hazard pointers/epochs or just a mutex.
- Use futures/`async` for one-shot results; build a pool for sustained workloads.
