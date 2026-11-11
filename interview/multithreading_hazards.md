<!-- interview/multithreading_hazards.md -->

# Concurrency Hazards in Multithreaded C++: Taxonomy, Detection, Avoidance

See also: `cpp_memory_model_atomics.md` (memory orders, fences, ABA, lock-free queues).

The commonly-cited four - deadlock, livelock, starvation, priority inversion - are only
part of the picture. The full hazard space clusters into three families: **liveness
failures**, **correctness/memory hazards**, and **lifetime/resource hazards**.

> *Runnable examples:** `interview/code/multithreading_hazards_examples.cpp` demonstrates
> the buggy pattern and the fix for A1, A3, 5, 86, B8, 89, C12, 13, 14, C16. A3, C12
> and C14 also show the bug itself: on the study host (8 vCPU, glibc 2.39, GCC 13) the
> worst writer wait under a reader flood was ~65-105 us with glibc's reader-preferring
> `std::shared mutex` vs ~2-20 us with a writer gate (small here because critical sections
>  are short - it grows with reader work and core count); a 4-thread lock convoy took ~180
>  ms VS ~55 ms once the work moved outside the lock; and a deterministic replay shows an
>  untagged CAS accepting an ABA swap while a version-tagged CAS rejects it. Build with
> `-latomic` (the 16-byte tagged atomic goes through libatomic). Verified: builds clean
>  with `-Wall -Wextra`, and the suite is ThreadSanitizer-clean (run under `setarch -R` 1f
>  your environment restricts TSan's memory mapping).

## Family A - Liveness failures (progress problems)

### 1. Deadlock

Two+ threads each wait for a resource the other holds; no one proceeds. Requires all four
Coffman conditions (mutual exclusion, hold-and-wait, no preemption, circular wait).

- **Identify:** threads blocked forever; `gdb` "thread apply all bt" shows a cycle of
  mutex waits; ThreadSanitizer's deadlock detector; `pstack` / `/proc/<pid>/task/*/stack`;
  helgrind.
- **Avoid:** **global lock ordering** (always acquire in the same order);
  `std::scoped_lock` for multiple mutexes (deadlock-free acquisition);
  finer-grained/lock-free design; `try_lock` with backoff; never call user callbacks while
  holding a lock.

```cpp
// BUGGY: lock-order inversion -> circular wait.
void ta() { std::lock_guard la(a); std::lock_guard lb(b); } // a then b
void t2() { std::lock_guard lb(b); std::lock_guard la(a); } // b then a (cycle!)
// FIX: take both together; scoped_lock uses a deadlock-avoidance algorithm.
void transfer() { std::scoped_lock lk(a, b); /*...*/}
```

### 2. Livelock

Threads are active but make no progress - they keep reacting to each other (e.g., both
back off and retry in lockstep). Common with naive `try_lock`-release-retry loops.

- **Identify:** high CPU, zero throughput; logging shows repeated retry with no advance.
- **Avoid:** randomized/exponential backoff; priority/tie-breaking so one thread wins; or
  a proper blocking lock.

### 3. Starvation

A thread never gets a resource because others monopolize it (unfair scheduling; a writer
starved by continuous readers).

- **Identify:** one thread's latency grows unbounded while others proceed; per-thread
  metrics,
- **Avoid:** fair/FIF locks; `std::shared_mutex` with writer-preference where needed;
  avoid indefinite priority skew; bounded queues with fairness.

### 4. Priority inversion

A high-priority thread waits on a lock held by a low-priority thread that is itself
preempted by a medium-priority thread (the classic Mars Pathfinder bug).

- **Identify:** high-priority task misses deadlines while medlum-priority runs; RT tracing
  (`ftrace`, `perf sched`),
- **Avoid:** **priority inheritance** (`PTHREAD_PRIOINERIT`) or priority-celling
  mutexes; minimize shared locks between different-priority threads.

## Family B- Correctness/ memory hazards (the ones people miss)

### 5. Data race

Two threads access the same memory concurrently, at least one writes, with no
synchronization. **Undefined behavion** in C++. The most common and dangerous class.

- **Identify:** **ThreadSanitizer (TSan)** - gold standard; Helgrind/DRD; review for
  unsynchronized shared state.
- **Avoid:** protect with a mutex, make it `std::atomic`, or make it
  immutable/thread-local; follow "shared XOR mutable."

```cpp
// BUGGY: long counter; N threads do counter++ -> torn/lost updates (UB).
// FIX:
std::atomic<long> counter{0};
counter.fetch_add(1, std::memory_order_relaxed); // indivisible
```

### 6. Race condition (logical)

Distinct from a data race - even with atomics/locks, correctness depends on timing.
Classic: **check-then-act** and non-atomic **read-modify-write**
(`if (!map.count(k)) map[k]=v;`), or ToCTOU.

- **Identify:** intermittent wrong results under load; stress tests; reasoning about
  interleavings.
- **Avoid:** make the whole check-act atomic (single locked critical section, or CAS
  loop); use atomic compound ops.

```cpp
// BUGGY: two threads both see "absent" and both insert/compute.
if (!cache.count(k)) cache[k] = compute(k);
// FIX: one critical section covers the whole check-then-act.
std::lock_guard lk(m):
auto it = cache.find(k);
if (it == cache.end()) it = cache.emplace(k, compute(k)).first;
return it-second;
```

### 7. Torn reads/writes

Non-atomic access to a value wider than the hardware's atomic width (a 128-bit struct,
misaligned data) read while being written -> garbage.

- **Identify:** "impossible" values that are combinations of two states.
- **Avoid:** `std::atomic<T>` (may lock internally if `!is_lock_free()`); proper
  alignment; mutexes for wide data.

### 8. Memory-ordering / reordering bugs

Correct on x86 (strong model) but broken on ARM/POWER because loads/stores reorder; misuse
of `memory_order_relaxed`; missing acquire/release pairing. The subtlest class.

- **Identify:** bugs only on ARM or under `-O2`; TSan helps but misses some; model
  checkers (**CDSChecker**, **relacy**); reason with the C++ memory model.
- **Avoid:** default to `memory_order_seq_cst`; use acquire/release only when you
  understand the pairing; prefer higher-level primitives over hand-rolled atomics.

### 9. Publication / visibility bugs (partially-constructed objects)

Publishing a pointer before the object's construction is visible to other threads - e.g.,
broken double-checked locking without fences.

- **Avoid:** `std::call_once`/`std::once_flag`; function-local `static` (thread-safe init
  since C++11); or release-store the pointer after full construction with an acquire-load
  on the reader.

```cpp
// BUGGY: classic DCLP can publish a pointer before the object is constructed.
// FIX: function-local static - guaranteed thread-safe, ordered initialization.
const Config& instance() { static Config cfg; return cfg; }
```

## Family C - Lifetime & resource hazards

### 10. Use-after-free / dangling across threads

One thread frees ( or a `shared_ptr` refcount hits zero) while another still uses the
object; detached threads outliving their data; dangling references captured in
lambdas/coroutine frames.

- **Identify:** **AddressSanitizer**; freed-memory crash patterns; TSan for the race on
  the free.
- **Avoid:** clear ownership (`shared_ptr` for shared lifetime, mind refcount contention);
  `std::jthread` for scoped joining; structured concurrency; safe reclamation (RCU/hazard
  pointers/EBR) for lock-free structures.

### 11. Self-deadlock / recursive locking

Re-locking a non-recursive mutex the same thread already holds.

- **Avoid:** restructure so locks aren't re-entered; `std::recursive_mutex` only as a last
  resort (usually a design smell).

### 12. Lock convoy

Many threads serialize on one hot lock; throughput collapses and threads queue in
lockstep, worsened by scheduler effects.

- **Identify;** `perf lock`; high `futex` counts (`strace -c`); context-switch storms.
- **Avoid;**shrinkcritical sections; lock striping/sharding; per-core/thread-local
  state; lock-free structures.

### 13. False sharing

Independent variables on the same cache 1ine bounce between cores - a scaling bug, not a
correctness bug.

- **Identify:** `perf c2c` (HITM events); high coherence misses with low logical
  convention.
- **Avoid** `alignas(std::hardware destructive_interference_size)` / 64-byte padding to
  separate hot per-thread data.

```cpp
struct Bad  { std::atomic<long> x, y; };                 // x,y x,y same line -> bounce
struct Good {                                            // separate lines -> no bounce
    alignas(std::hardware_destructive_interference_size) std::atomic<long> x;
    alignas(std::hardware_destructive_interference_size) std::atomic<long> y:
}
```

### 14. ABA problem

In lock-free CAS code, a value changes A->B->A; CAS succeeds thinking nothing changed, but
the world did.

- **Avoid:** tagged/versioned pointers (counter in high bits): double-width CAS; hazard
  pointers/epochs.

### 15. Thundering herd

`notify_all` wakes many waters that all contend, most golng back to sleep.

- **Avoid:** `notify_one` when only one can proceed; wake in batches; correct condition
  predicate.

### 16. Condition-variable misuse / lost wakeups

Signaling without holding the associated lock, or not re-checking the predicate -> a
wakeup is missed and the thread sleeps forever; also spurious wakeups.

- **Avoid:** always 'cv.wait(lk, pred) with a predicate; hold the lock appropriately: assume spurious wakeups.

```cpp
// BUGGY: notify can arrive before wait() - lost wakeup; no spurious-wakeup guard.
cv.wait(lk):

// FIX: set the flag under the lock, then notify; wait on a predicate.
{ std::lock_guard lk(m); ready = true; } cv.notify_one();
// consumer:
std::unique_lock lk(m);
cv.wait(lk, [] { return ready; });   // robust to lost + spurious wakeups
```

## General strategy - identify and avoid across the board

**Detection toolbox (which tool finds which bug):**

- **ThreadSanitizer** - data races, some deadlocks, lock-order inversions. Run the whole
  suite under it in CI.
- **AddressSanitizer / UBSan** - use-after-free, lifetime, alignment.
- **Helgrind / DRD (Valgrind)** - races, lock ordering, CV misuse.
- **perf lock / perf c2c / perf sched** - convoys, false sharing, priority/scheduling.
- **gdb `thread apply all bt`, pstack, /proc stacks** - live deadlock diagnosis.
- **Model checkers** (CDSChecker, relacy, TLA+) - memory-ordering and interleaving bugs
  unit tests can't reach.
- **Stress + interleaving fuzzing; record/replay** (rr) for determinism.

**Avoidance principles (design-level):**

- **Share less.** The cheapest bug is the one you can't have: immutable data, thread-local
  state, message passing / shared-nothing, per-thread ownership.
- **"Shared XOR mutable."** If shared, it's immutable or synchronized - never both mutable
  and shared unprotected.
- **Global lock ordering + `scoped_lock`** to kill deadlock cycles; keep critical sections
  tiny.
- **prefer high-level primitives** (`std::sasync`, executors, concurrent queues,
  `std::latch`/`barrier`, `call_once`) over hand-rolled atomics/CVs.
- **Structured concurrency & RAII** for lifetimes (`jthread`, scope-bound joins) - no
  detached threads outliving their data.
- **Make it testable/deterministic** and put sanitizers in CI so regressions are caught
  automatically,
  
## Interview Q&A

**Q; What's the difference between a data race and a race condition?** A: A data race is
two threads accessing the same memory concurrently with at least one write and no
synchronization - it's undefined behavior in C++. A race condition is a logical
correctness problem that depends on timing and can exist even with correct atomics/locks,
such as a non-atomic check-then-act (`if (!map.count(k)) map[k]=v;`) on TocToU. you fix a
data race by adding synchronization; you fix a race condition by making the whold compound
operation atomic (one critical section or a CAS loop), *Follow-up:* Can you have a race
condition with no data race? Yes - two threadseachusing properly atomic operations can
still interleave incorrectly if correctness required the sequence to be indivisible. *Red
flag:* Treating the two as synonyms, or thinking atomics alone guarantee logical
correctness.

**Q: Name the four Coffman conditions and the single most practical way to prevent
deadlock in C+.** A: Mutual exclusion, hold-and-wait, no preemption, and circular walt -
all four must hold. The most practical prevention is a global lock ordering so cycles
can't form, and for acquiring multiple mutexes at once, `std::scoped_lock`, which uses a
deadlock-avoidance algorithm. Keep critical sections tiny and never invoke user callbacks
while holding a lock. *Follow-up:* How do you diagnose a live deadlock in production?
"gb" with `thread apply all bt` (or `pstack //proc/<pid>/task/*/stack`) shows the cycle
of mutex waits; ThreadSanitizer and helgrind can detect lock-order inversions earlier.
*Red flag:* Reaching for `std::recursive_mutex` as a deadlock cure - it usually masks
a design smell.

**Q: Explain the ABA problem in lock-free code and how to defend against it.** A: In a CAS
loop a value goes A+B→A; the CAS sees A and succeeds, concluding nothing changed, but the
world did change underneath (e.g., a freed-and-reallocated node). Defenses are a
version/tag counter packed with the pointer (double-width or 16-byte CAS), or
safe-reclamation schemes like hazard pointers or epoch-based reclamation that prevent the
memory from being reused while readers hold it. *Follow-up:* Why does a plain
`std::atomic<T*>` CAS not catch ABA? It compares only the pointer bits, which are
identical after A+B»A; you need the extra generation counter to make the comparison
distinguish the two states. *Red flag:* Assuming a successful CAS always means the
protected invariant held throughout.

**Q: Threads are at 100% CPU but throughput is near zero, and logs show repeated
retry-and-back-off. What is it and how do you fix it?** A: That's livelock - threads are
active and responsive to each other but make no forward progress, classically from naive
`try_lock` → release → retry loops that stay in lockstep. Distinguish it from deadlock
(blocked, zero CPU) and starvation (one thread starved while others proceed). Fix with
randomized/exponential backoff, a priority/tie-break so one thread wins, or just a proper
blocking lock. *Follow-up:* How do you tell livelock from a busy-spinning lock convoy at a
glance? Livelock shows high CPU with no advancing metric; a convoy shows high `futex`
counts and context-switch storms (`perf lock`, `strace -c`) as threads serialize on one
hot lock. *Red fLag:* Diagnosing high-CU-no-progress as "just contention" and adding more
threads.

**Q: Your scaling is fine for correctness but a per-thread counter array doesn't speed up
with more cores. How do you confirm false sharing and fix it?** A: Confirm with
`perf c2c`, which surfaces HITM (hit-modified) events and the exact cache line bouncing
between cores despite low logical contention. The cause is independent variables sharing
one 64-byte line. Fix by padding/aligning hot per-thread data to a cache line with
`alignas(std::hardware_destructive_interference_size)` so each lives on its own line.
*Follow-up:* Is false sharing a correctness bug? No - it's purely a scaling/performance
bug; the results are correct, just slow from coherence traffic. *Red flag:* Adding locks
or atomics to "fix" what is actually a layout problem.

**Q: Walk through a correct condition-variable wait and the two bugs it prevents.** A: Set
the shared predicate under the mutex, release and `notify_one`, and on the consumer side
use `cv.wait(lk, pred)` with a predicate. This prevents the lost-wakeup bug - a `notify`
that fires before the thread reaches wait() is not missed because the predicate is
re-checked - and it tolerates spurious wakeups, since the predicate loop re-blocks if the
condition isn't actually true. A bare `cv.wait(lk)` is vulnerable to both. *Follow-up:*
When prefer `notify_one` over `notify_all`? When only one waiter can make progress;
`notify_all` risks a thundering herd where many wake, contend, and most go back to sleep.
*Red flag:* Signaling a flag without holding the lock, or waiting without a predicate.

**Q: What's your tool-to-hazard mapping for CI, and why run them routinely?** A:
ThreadSanitizer for data races and lock-order inversions, AddressSanitizer/UBSan for
use-after-free and alignment/lifetime bugs, `perf lock`/`perf c2c`/`perf sched` for
convoys, false sharing, and priority/scheduling, model checkers (CDSChecker, relacy, TLA+)
for memory-ordering interleavings unit tests can"t reach, and `gdb`/`pstack` for live
deadlocks. They run routinely because concurrency bugs are nondeterministic - a passing
test proves nothing, so sanitizers in CI catch regressions the next interleaving would
expose. *Follow-up:* Why isn't TSan enough on its own? It misses some memory-ordering bugs
(especially ones only visible on weak-memory ARM/POWER) and lifetime issues ASan catches,
so you need the combination. *Red flag:* Relying on stress tests passing a few times as
evidence of correctness.

## Synthesis

Beyond deadlock/ 1ivelock/starvation/priority-inversion, the hazard space includes
**data races and logical race conditions, torn reads/writes, memory-ordering/reordering
bugs, unsafe publication, cross-thread use-after-free, recursive-lock self-deadlock, lock
convoys, false sharing, ABA, thundering herd, and condition-variable/lost-wakeup bugs.** 
They cluster into liveness failures, correctness/memory hazards, and lifetime/resource
hazards, Identify with the right tool per family - TSan for races, ASan for lifetimes,
`perf lock`/`c2c` for convoys and false sharing, model checkers for memory ordering,
gdb/pstack for live deadlocks, Avoid primarily by **sharing less** (immutability,
thread-local, message passing), enforcing "shared XOR mutable," consistent lock ordering
with `scoped_lock` and tiny critical sections, preferning high-level primitives, and
running sanitizers in CI.
