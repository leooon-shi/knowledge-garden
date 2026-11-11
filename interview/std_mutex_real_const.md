<!-- interview/std_mutex_real_cost.md -->

# The Real Cost of std: :mutex (from glibc's View) and shared mutex Writer Starvation

# Part 1: The Real Cost of std::mutex, from glibc's Point of View

"How expensive is a mutex?" has a wildly different answer depending on whether the lock is
contended. The short version: **an uncontended mutex lock/unlock is a handful of
nanoseconds - just atomic instructions in user space, no kernel involvement at all. A
contended lock is potentially thousands of nanoseconds, because it drops into a `futex`
syscall and the kernel scheduler,** The gap between those two worlds is the whole story.

### Layer 1: what std: :mutex actually is

On Linux with libstdc++, `std::mutex` is a thin wrapper, It compiles down to
`pthread_mutex_lock`/`pthread_mutex_unlock` in glibc's NPTL. There is essentially zero C++
overhead - no virtual dispatch, no allocation; the `std::mutex` object *is* a
`pthread mutex_t`. So the real cost question is: what does glibc's `pthread mutex_lock`
cost?

# Layer 2: the fast path - the uncontended case

A default `pthread_mutex_t`* is a **futex-based** lock holding (among other flelds) a
32-bit lock word. In the uncontended case:

- **Lock:** glibc does a single atomic compare-and-swap (`lock cmpxchg` on x86) to flip
  the lock word from e (free) to 1 (held). If the CAS succeeds - which it does whenever
  the lock is uncontended - **that's it. No syscall. No kernel transition.** Cost is one
  atomic RMN instruction plus the memory barrier it implies.
- **Unlock:** an atomic store (and a check) to release the word. In the uncontended case,
  **no syscall** - glibc only calls into the kernel if it sees walters queued.

Uncontended cost is dominated by the atomic instruction: on the order of **a few to ~20
nanoseconds**, depending on whether the cache line holding the lock word is already in
this core's L1 in the right coherence state. If another core recently held it, you first
pay a cache-coherence miss to pull the line over - tens of ns. Key subtlety: **even an
"uncontended" lock costs a cache-line bounce if different cores lock it in turn.** The
atomic is cheap; the coherence traffic is what you pay.

This is the "futex" idea (**f**ast **u**serspace mu**tex**): keep the common case entirely
in user space, involve the kernel only when threads must block.

## Layer 3: the slow path - contention and the futex syscall

When the CAS fails because another thread holds the lock:

1. **Optional brief spin.** glibc supports **adaptive spinning**, but **only for mutexes
   of type `PTHREAD_MUTEX_ADAPTIVE_P`** - the default mutex used by `std::mutex`" does
   *not* spin; it goes straight to the futex after the failed CAS. Adaptive mutexes spin a
   bounded number of times (tunable via `glibc.pthread.mutex_spin_count`) hoping the
   holder releases quickly. A bet that the critical section is short.
2. **`futexFUTEX_WAIT` syscall.** If spinning doesn't win, glibc calls `futex()` to put
   the thread to sleep until the lock word changes. You pay:
   - the **syscall transition** (whundreds of ns, worsened by KPTI/Spectre mitigations),
   - the **scheduler** dequeuing your thread and running someone else - a **context
     switch** (~1-5 us including indirect cost),
   - and later, when released, the holder calls **`futex(FUTEX_WAKE)`** - another syscall
     - waking a waiter, which the scheduler must pick to run, incurring a second context
     switch and a **cold cache**.

A contended lock/unlock cycle can cost **1-10+ microseconds** - two to three orders of
magnitude more than the uncontended few nanoseconds. Plus secondary costs: the woken
thread's cache is cold, and a **thundering herd** (many waiters) can cause a
wake/fail/sleep stampede.

### Layer 4: the costs people forget

- **Cache-line coherence is the real hot-path cost.** Under moderate contention without
  sleeping, the dominant cost is the lock word's cache line ping-ponging between cores
  (MEST invalidations, HITM in `perf c2c`). Often larger than the atomic itself, and why
**false sharing** silently inflates lock cost.
- **Fairness and PI variants cost more.** A default glibc mutex is *not* fair (no FIFO).
  `ERRORCHECK`, `RECURSIVE`, and especially **priority-inheritance
  (`PTHREAD_PRIO_INHERIT`)** mutexes do more kernel work and cost more.
- **Memory ordering/barriers,**Acquire/release fences are cheap on x86 (strong model)
  but emit real barriers (`dmb`) on ARM.
- **Sizeof and layout,** `pthread_mutex_t` is w40 bytes on glibc/x86-64, One per object
  hurts cache density.

### Layer 5; what this means for design

- **Uncontended mutexes are nearly free - don't fear them.** Premature lock-free rewnites
  to avoid a few-ns `cmpxchg` are usually wasted effort. Measure contention first.
- **The cost cliff is contention, not the mutex,** Optimize the *contention rate*: shrink
  critical sections, shard/stripe locks, thread-local state, shared-nothing. Reducing
  contention converts every op from the us slow path back to the ns fast path.
- **Pad the lock's cache line** (`alignas(64)`) to prevent false sharing.
- **Alternatives with clear eyes:** `std::atomic` for a single word; spinlocks only when
  critical sections are shorter than a context switch and you're not oversubscribed;
  `std::shared_mutex` for read-heavy (but heavier at low contention); RCU/seqlocks for
  read-mostly data.
- **Diagnose real cost:** `perf lock`, `perf c2c`, `strace -c` (high `futex` count is the
  fingerprint of a contended mutex),

### Synthesis

From glibc's vien, `std::mutex` is a `pthread mutex_t`, a **futex**: the fast path is a
single atomic CAS in user space costing a few nanoseconds and **zero syscalls**, while the
slow path - only under contention - descends into `futex(FUTEX_WAIT/NAKE)`, the scheduler,
and one or two context switches costing microseconds plus cold-cache aftershocks. The
"real cost" is bimodal and dominated not by the lock instruction but by two things you
control at design time: **how often threads contend** and **cache-line coherence traffic*
on the lock word. Optimize the contention, not the mutex.

______________________________________________________________________

## Part 2: Why a Writer Starves Under std: shared mutex with 20 Readers

Scenario: an operator uses std: ishared mutex"to isolate read/write and improve
performance, but 20 read threads starve the write thread for at least 3 seconds. This is
the counterintuitive downside of std: shared mutex": the primitive meant to *improve*
read/write performance instead causes **writer starvation**.

### The core mechanism: why overlapping readers block the writer

A shared/exclusive lock lets any number of readers hold shared mode simultaneously, but a
writer needs exclusive mode - it can only proceed when **the reader count hits exactly
zero**.

with 20 continuously-arriving readers, the reader count is essentially never* zero:

```
Reader A:  [====lock====]        [====lock====]
Reader B:      [====lock====]        [====lock====]
Reader C:          [====lock====]        [====lock====]
Reader count: 3 3 2 3 4 3 2 3 ... (never reaches 0)
Writer:    (waiting....................still waiting)
```

Before the count drains to zero, another reader has already acquired shared access. The
writer waits for a coincidence - all readers simultaneously out of the lock - that never
happens.

### The specific reason: no fairness/writer-priority guarantee

> **The C++ standard does not specify any acquisition ordering or writer-priority policy
> for `std::shared mutex`. Whether a waiting writer blocks new incoming readers is
> implementation-defined - and the common Linux/glibc implementation (built on
> `pthread_rwlock_t`) defaults to a policy that can favor readers.**

glibc's rulock supports two relevant policies:

- `PTHREAD_RWLOCK_PREFER_READER_NP` (**default**) - a new reader can acquire the lock
  **even while a writer is waiting**, as long as the lock is currently held shared.
  Incoming readers "jump the queue" ahead of the pending writer; the count never drains.
  This is the primary cause.
- `PTHREAD_WLOCK PREFER WRITER_NONRECURSIVE_NP` - once a writer waits, new readers are
  blocked so the count can drain and the writer gets in.

Crucially, **`std::shared_mutex` gives no API to select the writer-preferring policy.** On
glibc you get reader-preferring by default. So even "correct" use can starve writers on
Linux - the primary suspect for the 3-second stall.

# Contributing factors that make it worse

1. **High reader arrival rate + non-trivial read hold time.** With 20 threads, the
   probability of all being outside the lock at once approaches zero.
2. **Readers re-acquiring in a tight loop** (`lock_shared → read → unlock_shared → loop`)
   re-enter before the writer is scheduled.
3. **Long or variable read critical sections** (I/O, allocation, heavy compute under the
   lock) widen the overlap window.
4. **Scheduler effects.** With 20 busy readers possibly saturating cores, writer waits
   additional time just for a core once runnable.
5. **The reader count is itself a contended cache line** - 20 threads doing atomic RMW on
   the counter ping-pong the line, slowing every reader and stretching the window.

### How to confirm

- `strace -f-e futex` - writer blocked in a long `FUTEX_WAIT` while reader futex activity
  churns.
- `perf sched` / off-CPU flame graph - writer off-CPU for seconds while readers run.
- Log lock wait time around the writer's `lock()`:
- Instrument read critical-section duration; check for loop re-acquire without yielding.

### How to fix it

**1, Switch to a writer-preferring policy** via the pthreads primitive directly:

```cpp
pthread_rwlock_t rw;
pthread_rwlockattr_t attr;
pthread_rwlockattr_init(&attr);
pthread_rwlockattr_setklnd_np(&attr,
    THREAD_AWLOCK_PREFER_WRITER_NONRECURSIVE_NP); // block new readers when a writer waits
phread_rwlock_init (&rw, &attr);
```

Non-portable (glibc-specific); the "nonrecursive" variant deadlocks if a read-holder
re-enters recursively - ensure readers don't re-enter.

**2. Shrink the read critical section.** Hold shared mode only long enough to copy needed
data out, then process outside the lock. Highest-leverage fix regardless of policy.

**3. Reconsider whether shared mutex is even right.** It's heavier than a plain
`std::mutex`; it pays off only when reads are frequent and genuinely long. For short
reads, a plain `std::mutex` is often faster overall and has no starvation asymmetry.
Measure both.

**4. Use a read-optimized pattern that avoids starvation by construction:**

- **RCU** - readers take no lock; writer publishes a new version atomically. No
  starvation. Ideal for read-mostly data.
- **`std::atomic<std::shared_ptre<T>>`** (or shared ptr swap under a tiny lock) - readers
  load the current snapshot; writer swaps a new one.
- **Seqlock**-readers read optimistically and retry on concurrent write; writers never
  wait for readers.
- **Double buffering** - writer prepares an inactive buffer and flips a pointer.

**5. If writes are rare, batch/debounce** them, or give the writer elevated scheduling
priority (partial mitigation only - doesn't fix the policy).

### Synthesis

The root cause is a design mismatch: **`std::shared mutex` optimizes for reader
concurrency, but on Linux/glibc it defaults to a reader-preferring policy with no standard
way to change it, so a steady stream of 20 readers keeps the shared-mode count from ever
reaching zero and the writer starves.** Fix hierarchy: shrink read critical sections;
then, if keeping an rwlock, drop to `pthread_rwlock_t` with `PREFER WRITER_NONRECURSIVE`
for bounded writer progress; best of all, for read-mostly data replace the lock with a
snapshot/RCU/atomic-`shared_ptr` scheme where readers don't block the writer at all.
Deeper lesson: **a reader-writer lock does not eliminate the writer's need for exclusivity
- under sustained read load it can be worse* than a plain mutex. Choose it only when
reads are long and frequent, and always pin down its fairness policy explicitly.**

__________________________________________________________________________

## Interview Q&A

**Q: How expensive is an uncontended `std::mutex` lock/unlock, and why?** A: A handful of
nanoseconds - on the order of a few to ~20 ns - with zero kernel involvement. On libstdc++
a `std::mutex` *is* a `pthread_mutex_t`, and glibc's uncontended fast path is a single
atomic compare-and-swap (`lock_cmpxchg`) to flip the lock word, with an atomic store to
release. No syscall, no context switch. The dominant variable cost is cache-coherence: if
another core held the line recently you pay to pull it over. *Follow-up:* So why do people
say mutexes are slow? They're conflating the uncontended fast path with the contended slow
path, which drops into `futex` and the scheduler and costs microseconds - two to three
orders of magnitude more. *Red flag:* Claiming every lock involves a syscall, or that
lock-free is always faster.

**Q: Does glibc's default mutex spin before blocking?** A: No. The default mutex used by
`std::mutex` goes straight to `futexFUTEX_WAIT()` after the failed CAS - it does not spin.
Only `PTHREAD MUTEX_ADAPTIVE_NP` spins a bounded number of times (tunable via
`glibc.pthread.mutex_spin_count`). This is a common misconception; people assume adaptive
spinning is on by default when it isn't for `std::mutex`. *Follow-up:* When would a
spinlock beat the futex path? Only when critical sections are shorter than a context
switch and you're not oversubscribed; otherwise spinning burns CPU that the holder needs
to make progress. *Red flag:* Asserting glibc mutexes "spin a bit first" as the default
behavior.

**Q: Under moderate contention without threads sleeping, what actually dominates the
cost?** A: Cache-line coherence traffic on the lock word - the line ping-pongs between
cores via MESI invalidations, showing up as HIT events in perf c2c. This is often
larger than the atomic instruction itself and is why false sharing silently inflates lock
cost even when logical contention is low. *Follow-up:* How do you mitigate it? Pad/align
the lock to its own cache line ("alignas(64)) to stop false sharing, and reduce the
contention rate by shrinking critical sections or sharding the lock. *Red flag:* Only
counting the cmpuchg" cost and ignoring coherence traffic.

**Q: You see a high `futex` syscall count under `strace -c`. What does that tell you and
how do you dig deeper?** A: A high `futex `count is the fingerprint of a contended* mutex
- the fast path makes no syscalls, so futex activity means threads are blocking and
waking. Dig deeper with `perf lock` to attribute wait time to specific locks, `perf c2c`
to find the contended cache lines and HIT, and `mutrace` to rank mutexes by contention.
The fix targets the contention rate, not the mutex primitive. *Follow-up:* what design
changes convert the slow path back to the fast path? Shrink critical sections,
stripe/ shard locks by key, move to thread-local or shared-nothing state - anything that
lowers how often threads contend. *Red flag:* Reacting to futex counts by rewriting to
lock-free before measuring where contention comes from.

**Q: An operator uses `std::shared_mutex` with 20 readers and the single writer starves
for seconds, Why, specifically on Linux/glibc?** A: Two reasons, Mechanically, a writer
needs the reader count to hit exactly zero, and with 20 continuously-arriving readers it
essentially never does, Specifically, glibc's `pthread_rwlock_t` defaults to
`PTHREAD_RLOCK_PREFER_READER_NP`, so a new reader can acquire shared mode even while a
writer waits- incoming readers Jump the queue and the count never drains. The C++
standard specifies no fairness or writer-priority policy, and `std::shared_mutex` exposes
no API to change it, *Follow-up:* How do you confirm it? `strace -f -e futex` shows the
writer stuck in a long FUTEX_WAIT amid churning reader futex activity, and
`perf sched`/an off-CPU flame graph shows the writer off-CPu for seconds. *Red flag:*
Blaming the scheduler alone and missing the reader-preferring policy.

**Q: How would you fix the writer-starvation case, in priority order?** A: First shrink
the read critical section - copy data out under the lock and process outside it; this is
the highest-leverage fix regardless of policy. If you keep an rwlock, drop to
`pthread_rwlock_t` with `PTHREAD_RLOCK_PREFER_WRITER_NONRECURSIVE_NP` for bounded writer
progress (glibc-specific, and readers must not re-enter recursively). Best of all fo
 read-mostly data, replace the lock with a snapshot scheme - RCU, `atomic<shared_ptr<T>>`,
a seqlock, or double buffering - so readers never block the writer. *Follow-up:* Why might
a plain `std::mutex` beat `shared mutex"` here? `shared mutex` is heavier and only pays off
when reads are both frequent and genuinely long; for short reads a plain mutex is often
faster overall and has no starvation asymmetry. *Red flag:* Just raising the writer's
scheduling priority and calling it fixed - it's a partial mitigation that doesn't touch
the policy.
