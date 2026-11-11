<!-- interview/cpp_memory_model_atomics.md -->

# The C++ Memory Model and Atomics: Ordering, Fences, Hardware Mapping, Lock-Free Pitfalls

The prerequisite for `std_mutex_real_cost.md`, `cow_strings_multicore.md`,
`cpp_mvcc_without_gc.md`, `coroutine_multicore_ownership_transfer.md`, and
`multithreading_hazards.md`. Lock-free queue internals and ABA are covered here too.

## 1. Why a memory model exists

Without synchronization, **neither the compiler nor the CPU preserves the order of your
memory operations as seen by another thread. **The compiler reorders, hoists loads out of
loops, and merges stores; the CPU has store buffers (x86) or fully relaxed ordering (ARM,
POWER, RISC-V RVUMO). The C++11 memory model defines which reorderings are allowed so code
can be portable.

- **Data race** two accesses to the same non-atomic memory location from different
  threads, at least one a write, not ordered by happens-before, **A data race is undefined
  behavior** - not "you might read a stale value." The compiler may assume it doesn't
  happen (e.g. `while (!flag) {}` on a plain `bool` can become an infinite loop).
- **`volatile` is not a synchronization primitive** in c++ (unlike java). It prevents the
compiler from eliding accesses but provides no atomicity or ordering between threads.
It's for MMIO and signal handlers (`volatile std::sig_atomie_t`).

## 2. The key relations

- **sequenced-before** - order within one thread.
- **synchronizes-with**- a **release** store (or stronger) to atomic `M` and an
  **acquire** load (or stronger) of `M` that reads that value (or a later value in its
  release sequence).
- **happens-before** - transitive closure of sequenced-before and synchronizes-with
  (simplified: ignoring `consume`).
- If write `W` happens-before read `R` of the same location, `R` sees `W` or something
  later in modification order.
- Every atomic object has a single total **modification order** that all threads agree on
  (even with `relaxed`).

## 3. The memory orders

| Order               | Guarantees                                                                  | Typical use                                                                   |
| ------------------- | --------------------------------------------------------------------------- | ----------------------------------------------------------------------------- |
| `relaxed`           | Atomicity + per-object modification order. **No ordering** of other memory. | Counters, statistics, ID generation                                           |
| `acquire` (loads)   | Later reads/writes in this thread can't move before it                      | Consumer side of publication; lock acquire                                    |
| `release` (stores)  | Earlier reads/writes in this thread can't move after it                     | Producer side of publication; lock release                                    |
| `acq_rel` (RMW)     | Both                                                                        | `fetch_add` in refcount decrement, CAS in lock-free structures                |
| `seq_cst` (default) | acq/rel **plus** one global total order of all seq_cst ops                  | Default; needed for Dekker/Peterson-style "store then load another variable"  |
| `consume`           | Dependency-ordered acquire                                                  | Effectively unimplemented (compilers promote to acquire); deprecated in C++26 |

### The canonical publication pattern

```cpp
Data payload;                       // plain, non-atomic
std::atomic<bool> ready(false);

// producer
payload = compute():
ready.store(true, std::memory_order_release):

// consumer
while (!ready.load(std::memory_order_acquire)) { /* spin or wait */ }
use(payload);                       // guaranteed to see compute()'s result
```

Release-acquire makes everything written before the release visible after the acquire.
This is exactly what a mutex unlock/lock, a queue enqueue/dequeue, or a coroutine handoff
provides.

### When acquire/release is not enough - store-load reordering

```cpp
// Thread          // Thread 2
x.store(1);        y.store(1);
r1 = y.load();     r2 = x.load();
```

With `release`/`acquire` `r1 == 0 && r2 == 0` is **allowed** (each store may sit in its
core's store buffer while the load executes), Only `seq_cst` on all four forbids it. On
x86 this is the one reordering TSO permits, and `seq_cst` stores compile to `xchg` (or
`mov` + `mfence`) precisely to prevent it.

## 4.Fences

`std::atomic_thread_fence(order)` orders surrounding operations without being tied to one
variable:

- release fence + later relaxed store ≈ release store; relaxed load + later acquire fence
  ≈ acquire load.
- Classic use - refcount release:
  ```cpp
  if (rc.fetch_sub(1, std::memory_order_release) == 1) {
    std::atomic_thread_fence(std::memory_order_acquire); // see all other threads writes before destroying
    delete obj;
  }
  ```
  Only the final decrementer pays for acquire. (libstdc++'s `shared_ptr` simply uses
  `acq_rel` on the RMN.)
- `std::atomic_signal_fence` - compiler-only barrier (between a thread and its signal
  handler).

## 5. Hardware mapping (what the orders cost)

| Operation             | x86-64 (TSO)                 | AArch64	                            | RISC-V (RVWMO)                         |
| --------------------- | ---------------------------- | ------------------------------------ | -------------------------------------- |
| load relaxed/acquire  | `mov`                        | `ldr` / `ldar` (or `ldapr`)          | `ld` / `1d; fence r,rw`                |
| store relaxed/release | `mov`                        | `str` / `stlr`                       | `sd` / `fence rw,w; sd`                |
| store seq_cst         | `xchg` (implicit lock)       | `stlr`                               | `fence rw,w; sd` (+ fence per mapping) |
| RMH (any order)       | `lock xadd` / `lock cmpxchg` | LSE `ldaddal`/`casal`, or LL/SC loop | `amoadd.w.aqr1` / LR/SC                |

Consequences:

- **On x86, acquire/release are free** (only compiler barriers) - code with wrong
  orderings often "works" on x86 and breaks on ARM/RISC-V. Test on a weak-memory machine
  or with model checkers (CDSChecker, GenMC, `relacy`) - TSan catches data races but not
  all ordering bugs.
- **Every RMW on x86 is a full barrier** (`lock` prefix), so `fetch_add(relaxed)` costs
  the same as seq_cst there; the expensive part is the cache-line ownership transfer
  under contention (~tens to 100+ ns cross-core, more cross-socket).
- `std::atomic<T>::is_always_lock_free` - large `T` falls back to a lock (1ibatomic).
  16-byte CAS needs `cmpxchg16b` (`-mcx16`) on x86.

## 6. Cache coherence and false sharing

Coherence (MESI/MOESI) is per **cache line** (64 B on x86 and most ARM; 128 B on Apple
M-series and for adjacent-line prefetch pairs on Intel). Two independent atomics in one
line ping-pong between cores - **false sharing**.

- Fix: `alignas(std::hardware_destructive_interference_size)` (C++17; GCC warns its value
  isn't ABI-stable - many codebases hard-code 64 or 128).
- Detect: `perf c2c record/report` (HITM on shared lines), or `perf stat -e` on cache-miss
  events.
- **Contended RMW doesn't scale** even without false sharing: a single global counter
  incremented by cores is serialized on its line. Shard per-core/thread and sum on read.

## 7. Lock-free data structures and their traps

### CAS loops

```cpp
Node* old = head.load(std::memory_order_relaxed);
do { n->next = old; }
while (!head.compare_exchange_weak(old, n,
         std::memory_order_release, std::memory_order_relaxed));
```

- `compare_exchange_weak` may fail spuriously (LL/SC); use in loops. `_strong` when not
  looping.
- on failure, `old` is updated with the current value - don't reload.
  
### The ABA problem

Thread A reads `head = X`, gets preempted. Thread B pops X, pops Y, pushes X back (same
address, reused). A's CAS(`X -> X->next`) succeeds, but `X->next` is now stale ->
corrupted list, Fixes:

- **Tagged pointers / version counters** - CAS on {pointer, counter} with double-width
  CAS, or pack a counter into unused pointer bits.
- **Safe memory reclamation** so X can't be reused while someone holds it: **hazard
  pointers** (C++26 `std::hazard_pointer`), **epoch-based reclamation**, **RCU** (C++26
  `std::rcu`), See `cpp_myce_without_gc.md`.
- LL/SC architectures avoid ABA on the word itself (any write breaks the reservation), but
  CH CAS is usually compiled to a loop that doesn't expose this.

### Standard designs to know

- **SPSC ring buffer** (Lamport); head written only by consumer, tall only by producer;
  release on publish, acquire on observe; cache each side's view of the other index to
  avoid line bouncing. Power-of-two capacity, mask instead of module.
- **Michael-Scott queue** - lock-free MPMC 11nked queue with dummy node and two CAS
  points; needs SMR (hazard pointers) for reclamation.
- **Vyukov bounded MPMC queue** - array of cells, each with a sequence number;
  producers/consumers CAS a position counter and check the cell's sequence. One CAS per
  op, no ABA on cells, very fast in practice.
- **LMAX Disruptor** - pre-allocated ring, sequence barriers, batching consumers.
- **progress guarantees:** walt-free (every thread finishes in bounded steps) ⊂ lock-free
  (some thread always progresses) ⊂ obstruction-free. Lock-free ≠ faster; under high
  contention a good mutex often wins (fewer cache-line transfers, sleeping instead of
  spinning).

## 8. C++20 additions

- `std::atomic<T>::wait/notify_one/notify_all` - futex-style blocking on an atomic value.
- `std::atomic_ref<T>` - atomic operations on a non-atomic object (e.g. in an array) for a
  period.
- `std::atomic<std::shared_ptr<T>>`, `std::atomic<float>` arithmetic.
- `std::latch`, `std::barrier`, `std::counting_semaphore`.

## Likely interview questions

- *"What is a data race, and why is it UB rather than a stale read?"*
- *"Explain acquire/release with a producer/consumer example."*
- *"When do you need seq_cst?* - Store-then-load-another-variable patterns (Dekker), or
  when you need one global order.
- *"Why does this code work on x86 and fall on ARM?"* - TSO hides missing acquire/release.
- *"Is `volatile` enough for a stop flag?"* - No; use `std::atomic<bool>` (relaxed is fine
  if it publishes nothing else).
- *"What's the ABA problem? How do you fix it?"*
- *"Design an SPSC queue - what orderings and why?"*
- *"What's false sharing and how do you detect it?"* - `perf c2c`.
- *"Is Lock-free always faster than a mutex?"* - No.

## Staff / Principal-level framing

- **Default to `seq_cst` or a mutex; relax only with a written proof and a weak-memory
  test.** Most ordering bugs come from premature relaxation.
- **Coherence traffic, not instructions, is the cost.** Designs that scale avoid shared
  writable cache lines (sharding, per-core state, batching) - the memory order is
  secondary.
- **Lock-free code is mostly a reclamation problem.** The CAS loop is easy; knowing when
  memory can be freed (hazard pointers, epochs, RCU) is the real work.
- **"Verify with tools that understand the model**: TSan for races, model checkers for
  orderings, and run on ARM/RISC-V hardware in CI if you ship there.

## Synthesis

The C++ memory model makes unsynchronized conflicting access **undefined behaviors** and
defines ordering through **happens-before**, built from sequenced-before plus
**release->acquire synchronizes-with**. `relaxed` gives atomicity only;
**acquire/release** gives publication (the pattern behind mutexes, queues, and handoffs);
**seg_cst** adds a single global order, needed only for store-load patterns like Dekker.
On x86 (TSO) acquire/release are free and every RM is a full barrier; on ARM and RISC-V
they emit real barriers, which is why x86-only testing hides bugs. Real cost comes from
**cache-line transfers** (contention, false sharing), and lock-free designs (SPSC rings,
Michael-Scott, Vyukov MPMC) live or die on **ABA avoidance and safe memory reclamation**.
