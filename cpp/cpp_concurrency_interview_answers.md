<!-- cpp/cpp_concurrency_interview_answers.md -->

# C++ concurrency interview answers

Answers to [`cpp_concurrency_interview.md`](cpp_concurrency_interview.md). Sections and
numbering match the questions file: `Q1`- `Q245` are the numbered questions and `C1`-`C10`
are the code-based questions. Link to an answer with its anchor, for example
`cpp_concurrency_interview_answers.md#q42`.

Try the questions first, then compare.

## Fundamental concepts

#### Q1

**What is the difference between concurrency and parallelism?**

Concurrency is about structure: dealing with multiple tasks that are in progress over
overlapping time periods, which a single core can achieve by interleaving them.
Parallelism is about execution: literally running multiple tasks at the same instant,
which requires multiple hardware execution units. A program can be concurrent without
being parallel (coroutines on one core) and the two concerns are orthogonal: concurrency
is a way of composing independent activities, parallelism is a runtime property of doing
them simultaneously.

#### Q2

**What is the difference between a process, a thread, and a task?**

A process is an OS-level unit of isolation with its own virtual address space, file
descriptors, and resources. A thread is a unit of scheduling that executes within a
process and shares that process's address space with sibling threads, having only its own
stack, registers, and thread-local storage. A task is a logical unit of work, an
abstraction independent of how it is run; it may be mapped onto a thread, run on a thread
pool, or executed inline. Threads and processes are OS concepts with real costs, whereas a
task is a programming-model concept.

#### Q3

**What are the advantages and disadvantages of using multiple threads?**

Advantages: they can exploit multiple cores for throughput, keep the program responsive by
moving blocking work off the critical path, and overlap I/O latency with computation.
Disadvantages: shared mutable state introduces data races and the need for
synchronization, which brings deadlock, contention, and hard-to-reproduce timing bugs;
threads cost memory (stacks) and context-switch overhead; and reasoning, testing, and
debugging all become substantially harder. More threads is not automatically faster.

#### Q4

**What is a data race?**

A data race occurs when two threads access the same memory location concurrently, at least
one access is a write, and the accesses are not ordered by any synchronization (and are
not both atomic). In Ct a data race is undefined behavior per \[intro.races\]: the
standard gives no meaning to the program at all. It is a precise, formally defined
condition, distinct from the more general notion of a race condition.

#### Q5

**What is the difference between a data race and a race condition?**

A data race is the specific, language-defined UB of unsynchronized conflicting memory
accesses. A race condition is a broader logic flaw where the correctness of the result
depends on the relative timing or interleaving of operations, even when every individual
access is properly synchronized. You can have a race condition with no data race (e.g. a
check-then-act on a mutex-protected variable where the lock is released between the two
steps), and fixing all data races does not guarantee freedom from race conditions.

#### Q6

**What is undefined behavior in a multithreaded C++ program?**

The most common source is a data race, which [intro.races] defines as UB. Other examples
include destroying a `std::thread` that is still joinable (calls `std::terminate`), using
an object after its lifetime ends from another thread, or torn reads/writes of non-atomic
objects. Under UB the compiler is permitted to assume the situation never happens and may
optimize accordingly, so the observable effects can be nonsensical and are not limited to
"wrong value" outcomes.

#### Q7

**What does thread-safe mean?**

A piece of code or a type is thread-safe if it behaves correctly when invoked concurrently
from multiple threads, without the caller having to add external synchronization for the
guaranteed operations. Thread safety is always relative to a contract: for the standard
library, distinct objects are safe to use concurrently, and `const` member functions on
the same object are generally safe to call concurrently, but concurrent mutation of the
same object is not unless documented.

#### Q8

**What is the difference between thread safety and reentrancy?**

Reentrancy means a function can be safely interrupted and called again (re-entered) before
the first invocation completes, typically because it keeps no shared or static state and
relies only on its arguments and local variables; this matters for signal handlers and
recursion. Thread safety means concurrent calls from multiple threads behave correctly,
usually achieved via locks or atomics. They are independent: a function can be thread-safe
(via a mutex) yet not reentrant (it would deadlock if re-entered on the same thread), and
a reentrant function that touches no shared state is naturally thread-safe.

#### Q9

**What is shared mutable state?**

State that is both reachable from more than one thread and modifiable. It is the
fundamental root of concurrency hazards: if data is not shared, or is shared but
immutable, no synchronization is needed. The two standard strategies for safety are to
eliminate sharing (give each thread its own copy / confine ownership) or eliminate
mutability (make shared data immutable), falling back to synchronization only where
genuinely shared mutable state remains.

#### Q10

**Why are global variables particularly risky in concurrent programs?**

Globals (and function-local statics, mutable singletons) are implicitly shared across all
threads, so any mutation is shared mutable state that is easy to overlook and hard to
audit, since access is not visible in a function's signature. They create hidden coupling
and data races, complicate testing, and function-local statics additionally carry a
one-time thread-safe initialization guarantee (the compiler inserts a guard) but no
protection for subsequent accesses.

#### Q11

**What is thread affinity?**

Thread affinity is the binding of a thread to a specific CPU or set of CPUs so the
scheduler keeps it there rather than migrating it. The main benefit is preserving cache
locality (L1/2 and TLB warth) and NUMA locality, reducing cross-core cache traffic. It
is an OS feature (`sched_setaffinity`, `SetThreadAffinityMask`), not part of standard C++,
and overusing it can hurt by preventing the scheduler from balancing load.

#### Q12

**What is the difference between CPU-bound and I/O-bound workloads?**

A CPU-bound workload is limited by compute throughput; its threads spend most of their
time executing on a core, so the useful degree of parallelism is roughly the number of
cores. An I/O-bound workload is limited by waiting on external resources (disk, network,
locks); its threads spend most of their time blocked, so you can profitably run far more
concurrent tasks than cores, or use asynchronous/event-driven I/O to avoid dedicating a
thread per wait.

#### Q13

**When can adding more threads reduce performance?**

When there are more runnable threads than cores (oversubscription), causing context-switch
overhead and cache thrashing; when threads contend heavily on a shared lock or atomic, so
added threads just queue and generate coherence traffic; when the workload is
memory-bandwidth-bound rather than compute-bound; and when false sharing or
synchronization serializes the work anyway. Amdahl's law bounds the gain by the serial
fraction, so past a point more threads add cost without adding throughput.

#### Q14

**What is oversubscription?**

Oversubscription is running more active (runnable) threads than there are hardware
execution contexts. The OS must time-slice them, incurring context-switch cost, scheduler
overhead, and cache/TLB pollution as threads are evicted and reloaded. A modest amount can
help hide I/O latency, but for CPU-bound work it generally degrades throughput and
increases latency jitter.

#### Q15

**What is thread starvation?**

Starvation is when a thread is perpetually denied the resources (CPU time, a lock, a queue
slot) it needs to make progress, because other threads keep acquiring them first. Causes
include unfair locks, aggressive priority scheduling where high-priority threads
monopolize the CPU, or a steady stream of competitors that always win a race for a
resource. Fairness mechanisms (FIFO queues, fair mutexes, aging) mitigate it.

#### Q16

**What is livelock?**

In livelock, threads are not blocked and are actively executing, but they keep responding
to each other's state changes in a way that prevents any of them from making real
progress, like two people repeatedly stepping aside in the same direction in a corridor.
It often arises from naive deadlock-avoidance schemes where threads detect contention,
back off, and retry in lockstep. Randomized backoff breaks the symmetry.

#### Q17

**What is deadlock?**

Deadlock is a state where a set of threads are each permanently blocked waiting for a
resource held by another thread in the set, so none can proceed. It requires the four
Coffman conditions to hold simultaneously: mutual exclusion, hold-and-wait, no preemption,
and circular wait. Breaking any one of them (e.g. imposing a global lock-ordering to
prevent circular wait) prevents deadlock.

#### Q18

**What is priority inversion?**

Priority inversion occurs when a high-priority thread is blocked waiting on a resource
held by a low-priority thread, and a medium-priority thread that does not need the
resource preempts the low-priority holder, indefinitely delaying the high-priority thread.
The classic fix is priority inheritance (the holder temporarily inherits the waiter's
priority) or the priority-ceiling protocol. The Mars Pathfinder resets were a famous
real-world case.

## C++ threading

#### Q19

**How do you create a thread using `std::thread`?**

You construct a `std::thread` with a callable and its arguments, and the thread begins
executing immediately: `std::thread t(func, arg1, arg2);` or with a lambda
`std::thread t([]{ work(); });`. The constructor copies/moves the callable and arguments
into thread-owned storage (decay-copied), then you must eventually `join()` or `detach()`
it before the `std::thread` object is destroyed.

#### Q20

**What happens if a `std::thread` object is destroyed while it is still joinable?**

The destructor calls `std::terminate()`, aborting the program. This is a deliberate design
choice (rationale in Hans Boehm's paper N2802): silently joining could cause unexpected
blocking, and silently detaching could cause lifetime/UB surprises, so the standard forces
you to make an explicit decision. You must call `join()` or `detach()` before the object
goes out of scope (or use `std::jthread`, which joins automatically).

*Red flag:* a candidate who thinks the destructor "just detaches."

#### Q21

**What is the difference between `join()` and `detach()`?**

`join()` blocks the calling thread until the target thread finishes and then releases its
resources; afterward the `std::thread` is non-joinable. `detach()` severs the association,
letting the thread run independently in the background with the runtime responsible for
cleanup at its termination; the `std::thread` object becomes non-joinable immediately.
After either call `joinable()` returns false. You must call exactly one of them.

#### Q22

**When is detaching a thread dangerous?**

When the detached thread outlives objects it references. Because you lose the handle, you
cannot join it at shutdown, so it may still be running and touching globals, captured
references, or static data as they are destroyed (including during program exit),
producing data races or use-after-free. Detached threads also swallow any way to observe
completion or propagate exceptions. In practice an owned, joined thread (or `jthread`)
with cooperative cancellation is almost always safer.

#### Q23

**How do you determine whether a thread is joinable?**

Call `t.joinable()`, which returns true if the object represents an active thread of
execution that has not yet been joined or detached. A default-constructed thread, a
moved-from thread, or one that has already been joined/detached is not joinable. Note
joinable only reflects the association, not whether the thread function has actually
finished.

#### Q24

**What is the purpose of `std::jthread`?**

`std::jthread` (C++20) is an RAII-owning thread that joins automatically in its destructor
and has built-in cooperative cancellation support. It removes the most common
`std::thread` footgun (the terminate-on-destroy-while-joinable trap) and bundles a
`std::stop_source`/`std::stop_token` so threads can be asked to stop cleanly. It is the
recommended default thread type in modern C++.

#### Q25

**How does `std::jthread` differ from `std::thread`?**

Two main differences: (1) `jthread`'s destructor calls `request_stop()` and then `join()`
if the thread is joinable, instead of calling `std::terminate`; (2) `jthread` integrates
`std::stop_token` cancellation, and if the thread's callable accepts a `std::stop_token`
as its first parameter the `jthread` passes one in automatically. Otherwise the interface
is the same. `jthread` gives you automatic, exception-safe joining and a standard
cancellation channel.

#### Q26

**How does cooperative cancellation work with `std::jthread` and `std::stop_token`?**

Cancellation is cooperative, meaning it only requests a stop; the running code must check
for it and return voluntarily (threads cannot be forcibly killed in standard C++). The
`jthread` owns a `std::stop_source`; calling `request_stop()` (or the `jthread`
destructor) sets the shared stop state. The worker receives a `std::stop_token` and
periodically checks `token.stop_requested()`, or registers a `std::stop_callback` to be
invoked on request so it can, e.g., wake a condition variable.
`std::condition_variable_any::wait` has overloads taking a stop_token to make blocking
waits cancellable.

#### Q27

**What happens if an exception escapes from a thread function?**

If an exception propagates out of the top-level function of a thread (and is not caught
within it), `std::terminate()` is called, ending the program. Exceptions do not cross
thread boundaries implicitly. To handle this you must catch the exception inside the
thread and transfer it explicitly, for example via `std::exception_ptr` and
`std::current_exception()`, or by using `std::promise`/`std::packaged_task`/`std::async`,
which capture it into the associated future.

#### Q28

**How can exceptions be transferred from a worker thread to the calling thread?**

Catch the exception in the worker and capture it with `std::current_exception()`, storing
the resulting `std::exception_ptr` in shared state; the consumer rethrows it with
`std::rethrow_exception()`. The higher-level mechanisms do this for you:
`std::promise::set_exception(std::current_exception())` surfaces it through the paired
`std::future`, and `std::async` / `std::packaged_task` automatically store any escaping
exception in the future so it is rethrown when `get()` is called.

#### Q29

**What is the lifetime risk when a thread captures a reference to a local variable?**

If the thread captures a local by reference (or by pointer) and the enclosing scope exits
before the thread finishes using it, the referent is destroyed and the thread dereferences
a dangling reference: undefined behavior, typically a use-after-free. This is especially
dangerous with `detach()` since you cannot join to bound the lifetime. The fix is to
capture by value, extend the referent's lifetime (e.g. `shared_ptr`), or ensure the thread
is joined before the scope ends.

#### Q30

**What is wrong with this code?**

```cpp
void start() {
    int value = 42;

    std::thread worker([&] {
        use(value);
    });

    worker.detach();
}
```

The Lambda captures `value` by reference, and the thread is detached, so `start()` returns
and destroys `value` while the detached thread may st1l1 be running. When `use(value)`
executes it reads through a dangling reference to a destroyed stack variable: a data race
/ use-after-free, which is undefined behavior. Detach makes it worse because there is no
join to bound the lifetime. Fix by capturing by value (so the thread owns its own copy):

```cpp
void start() {
    int value = 42;
    std::thread worker([value] {   // capture by value
        use(value);
    });
    worker.join();                  // or jthread; avoid bare detach
}
```

*Follow-up:* even with by-value capture, prefer `std::jthread` with `join()` so the thread
is bounded and exceptions/cancellation are handled.


#### Q31

**How should thread arguments be passed safely?**

Prefer passing by value so the thread owns an independent copy; `std::thread` decay-copies
its arguments into internal storage, so the original can safely go out of scope. If you
genuinely need to pass a reference, wrap it with `std::ref`/`std::cref` and guarantee the
referent outlives the thread (join before it dies). Move-only resources should be passed
with `std::move`. For heap objects shared across threads use `std::shared_ptr` to tie
lifetime to use.

#### Q32

**What is the purpose of `std::thread::hardware_concurrency()`?**

It is a static function returning a hint for the number of hardware thread contexts that
can run truly concurrently (physical/logical cores). It is typically used to size a thread
pool or choose a default parallelism level. It is only a hint, not a guarantee of anything
about what the program is allowed to use.

#### Q33

**Why should its return value not always be treated as the exact number of usable CPU
cores?**

Because it is explicitly documented as a hint and may return 0 when the value cannot be
determined, so you must handle that (fall back to a sensible default). It also does not
account for CPU affinity masks, cgroup CPU quotas (common in containers/Kubernetes),
hyperthreading effects, or other processes competing for cores, so it can overstate the
parallelism actually available to your process. For containerized deployments you often
need to read the cgroup quota instead.

## Mutexes and locks

#### Q34

**What is a mutex?**

A mutex (mutual-exclusion primitive) is a synchronization object that ensures only one
thread at a time holds the lock, serializing access to the shared data it guards. In C++,
`std::mutex` provides `lock()`, `try_lock()`, and `unlock()`, and establishes a
happens-before relationship: everything done before an `unlock()` is visible to the thread
that subsequently acquires the lock. It should almost always be used through an RAII
wrapper rather than locked/unlocked by hand.

#### Q35

**What is the difference between `std::mutex` and `std::recursive_mutex`?**

A `std::mutex` must not be locked again by a thread that already holds it; doing so is
undefined behavior (typically deadlock). A `std::recursive_mutex` allows the same thread
to lock it multiple times, maintaining an ownership count, and it is released only after a
matching number of unlocks. The recursive variant is heavier and usually signals a design
that lets locked code call back into other locked code.

#### Q36

**Why is recursive locking often a design warning?**

Needing a recursive mutex usually means a locked public method calls another locked public
method, indicating unclear ownership of the critical section and tangled invariants. It
makes it harder to reason about where the lock is held and what state is consistent, and
it can mask bugs that a plain mutex would catch as deadlock. The cleaner design typically
splits code into a public method that locks and a private unlocked helper that assumes the
lock is held.

#### Q37

**What is `std::lock_guard`?**

`std::lock_guard` is a minimal RAII wrapper that locks a mutex in its constructor and
unlocks it in its destructor, guaranteeing release even if an exception is thrown. It is
non-copyable, non-movable, and offers no manual lock/unlock operations, so it is the
lightest-weight choice when you simply want to hold a lock for a scope. It can also adopt
an already-locked mutex via `std::adopt_lock`.

#### Q38

**What is `std::unique_lock`?**

`std::unique_lock` is a more flexible RAII lock wrapper that supports deferred locking
(`std::defer_lock`), adopting (`std::adopt_lock`), timed and try-locking, manual
`lock()`/`unlock()`/`release()`, and crucially is movable. That flexibility makes it
transferable and usable with `std::condition_variable::wait`, which needs to unlock and
relock. It carries a small amount of extra state (a flag for whether it owns the lock)
compared to `lock_guard`.

#### Q39

**What is the difference between "std::lock_guard" and std::unique_lock ?**

`lock_guard` is fixed-scope, non-movable, lock-on-construct/unlock-on-destroy, and has
essentially zero overhead beyond the lock itself. `unique_lock` is movable and supports
deferred/timed/try locking, manual unlock/relock, and ownership transfer, at the cost of a
bool ownership flag. Use `lock_guard` (or `scoped_lock`) by default for a simple scoped
lock, and reach for `unique_lock` when you need condition variables, deferred locking, or
to move ownership.

#### Q40

**When would you use `std::scoped_lock`?**

Use `std::scoped_lock` (C++17) as the default RAII guard, especially when locking two or
more mutexes at once, because it locks them all using a deadlock-avoidance algorithm. With
a single mutex it behaves like `lock_guard`. It supersedes the old pattern of
`std::lock(m1, m2)` followed by two `lock_guard`s with `adopt_lock`.

#### Q41

**How does `std::scoped_lock` help avoid deadlock when locking multiple mutexes?**

When constructed with several mutexes, `scoped_lock` acquires them with the same
all-or-nothing algorithm as `std::lock`: it uses a combination of blocking locks and
`try_lock`s with back-off so that it never holds some while blocking indefinitely on
another. The effective acquisition order is unspecified, but the guarantee is that two
threads both locking the same set of mutexes through `scoped_lock`/`std::lock` cannot
deadlock against each other via that set. It does not protect against ordering issues with
mutexes locked outside that single call.

#### Q42

**What does `std::defer_lock` mean?**

`std::defer_lock` is a tag passed to a `unique_lock` (or `lock_guard`/`scoped_lock`)
constructor telling it to associate with the mutex but not lock it yet. The wrapper still
owns the responsibility to unlock on destruction once you do lock it, but you lock later
(e.g. via `std::lock` on several deferred locks, or a manual `lock.lock()`). It is the
tool for building multi-mutex acquisition manually without acquiring on construction.

#### Q43

**What is the purpose of `std::adopt_lock`?**

`std::adopt_lock` tells the RAII wrapper that the mutex is already locked by the current
thread, so the constructor should take over ownership without locking again, and still
unlock it on destruction. It is used after a manual `std::lock(m1, m2)` to wrap the
already-held mutexes for scope-based release, or any time you have pre-locked a mutex and
want RAII cleanup. Misusing it (adopting a mutex you do not actually hold) is undefined
behavior.

#### Q44

**What is a timed mutex?**

A timed mutex (`std::timed_mutex`, `std::recursive timed_mutex`) adds
`try_lock_for(duration)` and `try_lock_until(time_point)` to the normal mutex interface,
letting a thread attempt to acquire the lock but give up after a timeout instead of
blocking indefinitely. This is useful to bound latency, avoid livelock, or implement
fallback behavior when a resource is contended. The timed operations may fail spuriously,
so their result must always be checked.

#### Q45

**What is the difference between `try_lock()`, `try_lock_for()`, and `try_lock_until()`?**

`try_lock()` attempts to acquire the lock once and returns immediately with true/false,
never blocking. `try_lock_for(d)` blocks up to a relative duration `d` waiting for the
lock, returning false if it cannot be obtained in time. `try_lock_until(tp)` blocks until
an absolute time point `tp`. The timed variants require a timed mutex; all three return a
bool you must check, and the timed ones are allowed to return false spuriously.

#### Q46

**How do you protect multiple related variables with a mutex?**

Guard all variables that participate in a shared invariant with the same mutex, and ensure
every read and write of any of them happens while holding that one lock, so the invariant
is never observed in a half-updated state. Keep the data and its mutex together (e.g, as
members of a class whose methods lock), and perform the full logical update inside a
single critical section rather than locking/unlocking between related steps. Using
separate mutexes for variables that must change together reintroduces race conditions.

#### Q47

**Why should the critical section usually be as small as practical?**

Because the critical section is serialized across threads, so its length directly limits
scalability (Amdahl's law) and increases the chance of contention, where other threads
stall waiting for the lock. A short critical section reduces how long the lock is held,
lowering contention and improving throughput and latency. The usual pattern is to do
expensive or blocking work (allocation, I/O, computation) outside the lock and hold it
only for the minimal state mutation.


#### Q48

**Why should potentially blocking operations generally be avoided while holding a mutex?**

Holding a mutex serializes every other thread that needs it, so blocking on I/O, a
condition, allocation, or another lock while holding it dramatically extends the critical
section and can collapse throughput. Worse, blocking on another lock while holding one is
a classic recipe for deadlock, and calling unknown/user code under the lock can reenter
and deadlock or violate invariants. Acquire needed data, release the lock, then block.

#### Q49

**What is lock contention?**

Lock contention is when multiple threads try to acquire the same lock at the same time, so
some must wait. High contention serializes the program, causes threads to block and
context-switch, and generates cache-coherence traffic on the lock's cache line, so
throughput can actually drop as you add threads. It is often the dominant scalability
bottleneck and is measurable with profilers that report lock wait time.

#### Q50

**How can lock contention be reduced?**

Shrink the critical section; use finer-grained locks so independent data uses independent
locks (e.g. lock striping / sharding); replace shared mutable state with per-thread data
that is aggregated occasionally; use reader-writer locks when reads dominate; use
lock-free atomics for simple counters; and reduce the frequency of locking by batching.
Choosing a better algorithm or data layout that shares less is often more effective than
tuning the lock itself.

#### Q51

**What is lock convoying?**

Lock convoying happens when a thread holding a lock is preempted or blocked, and a
"convoy" of other threads piles up waiting behind it, all ending up serialized and
repeatedly context-switching even after the holder resumes. It tends to arise with
frequently acquired locks under scheduler pressure, and it can persist because the queued
threads keep handing the lock around in lockstep. Reducing lock frequency, using
try-lock/back-off, or lock-free structures mitigates it.

#### Q52

**What is lock granularity?**

Lock granularity describes how much data a single lock protects. Coarse granularity means
one lock guards a large amount of state (simple, but high contention); fine granularity
means many locks each guard a small piece (more parallelism, but more complexity and
overhead). Choosing granularity is a central design trade-off between
simplicity/correctness and scalability.

#### Q53

**Compare coarse-grained locking and fine-grained locking.**

Coarse-grained locking uses one lock for a large region of state: it is easy to reason
about and hard to get wrong, but it serializes unrelated operations and limits scalability
under contention. Fine-grained locking uses many small locks so independent operations
proceed in parallel, improving throughput, but it raises per-operation overhead, increases
the risk of deadlock (multiple locks to order), and is far harder to verify. Start coarse
for correctness and refine only where profiling shows contention.

#### Q54

**What are the risks of using too many fine-grained locks?**

More locks mean more lock-acquire overhead per operation, higher memory footprint, and
greater cache traffic, which can erase the parallelism gains. They sharply increase
deadlock risk because operations now touch multiple locks that must be consistently
ordered, and they make the code much harder to reason about and test. There is also a
point where contention simply moves elsewhere, so the added complexity buys little.

#### Q55

**How can a mutex be accidentally copied or moved?**

It cannot, by design: `std::mutex` is neither copyable nor movable (both the copy and move
operations are deleted). The accidental trap is at the enclosing level, for instance
writing a class that holds a `std::mutex` member and then relying on a compiler-generated
copy/move constructor; because the mutex is non-copyable/non-movable, those special
members are implicitly deleted, so the class silently becomes non-copyable/non-movable too
(and attempts to copy it fail to compile). If you need a copyable type that contains a
mutex you must write special members by hand that create a fresh mutex rather than copying
it.

*Red flag:* trying to "move the lock state" with the object; the lock protects the object,
it is not part of its value.

#### Q56

**What is lock ordering, and how does it prevent deadlocks?**

Lock ordering is a convention that all threads acquire multiple locks in the same globally
agreed order (for example by a fixed hierarchy or by comparing addresses). This eliminates
the circular-wait Coffman condition, because you can never have a cycle of "A holds L1
waiting for L2 while B holds L2 waiting for L1" if everyone always takes L1 before L2. It
is the standard manual technique; `std::lock`/`std::scoped_lock` achieve the same
guarantee automatically for locks taken together.

#### Q57

**What is the difference between a deadlock and a livelock?**

In a deadlock, threads are blocked and inactive, each waiting forever for a resource
another holds; nothing runs. In a livelock, threads are not blocked, they are actively
running and changing state, but their mutual reactions prevent any of them from making
real progress. Deadlock shows up as stuck, idle threads (zero CPU on them); livelock shows
as busy threads burning CPU with no forward progress. Randomized back-off cures livelock;
lock-ordering or avoidance cures deadlock.

#### Q58

**How would you debug a deadlock?**

Capture a stack trace of all threads while it is hung (gdb `thread apply all bt`, a core
dump, or a thread dump) and look for the cycle of threads each blocked on a lock held by
another; that reveals the offending lock-acquisition order. Dynamic tools like
ThreadSanitizer and Valgrind's Helgrind/DRD detect lock-order inversions, often before an
actual deadlock occurs. Then fix the root cause by enforcing a consistent lock ordering,
using `std::scoped_lock` for multi-lock acquisition, shrinking or splitting critical
sections, or replacing nested locking with a lock-free or single-lock design.

## Condition variables

#### Q59

**What problem does `std::condition_variable` solve?**

It lets a thread wait efficiently for a condition on shared state to become true without
busy-polling, releasing the CPU until another thread signals a change. A naive spin loop
over a flag wastes cycles and still needs a mutex to avoid races; a condition variable
couples a wait with the mutex so the waiter atomically releases the lock and blocks, then
reacquires the lock on wakeup. It solves the "wait for an event produced by another
thread" coordination problem (e.g. a consumer waiting for a producer to enqueue work).

#### Q60

**How does a condition variable work with a mutex?**

The waiter holds a `std::unique_lock` on the mutex that protects the shared state, then
calls `wait(lock)`. `wait` atomically unlocks the mutex and blocks the thread; when
notified it reacquires the mutex before returning, so the predicate is always evaluated
under the lock. The mutex closes the window between checking the condition and going to
sleep: without it a notification could slip in between the test and the wait and be lost.
The notifying thread should mutate the shared state under the same mutex, then call
`notify_one`/`notify_all` (notifying may be done with or without the lock held).

#### Q61

**Why must a condition variable always be used with a predicate?**

```cpp
condition.wait(lock, [&] {
    return !queue.empty() || stopped;
});
```

Because `wait` can return without a corresponding notification (spurious wakeup) and
because a notification may have been delivered before the wait began. The predicate
overload is exactly equivalent to `while (!pred()) wait(lock);`, so it re-checks the
actual condition on every wakeup and only proceeds when it is genuinely satisfied. Without
a predicate you may act on state that is not ready, or miss state that was already set. It
also makes the code correct against lost wakeups where the condition became true before
you started waiting.

#### Q62

**What is a spurious wakeup?**

A spurious wakeup is when a thread blocked in `wait` returns even though no thread called
`notify_one`/`notify_all`. It is permitted by the standard (and by the underlying OS
primitives such as futexes/pthreads) because preventing it would be more expensive. The
practical consequence is that you can never assume a return from `wait` means the
condition holds; you must re-test it. Using the predicate overload (or a manual `while`
loop) handles this transparently.

#### Q63

**Why must the condition be checked in a loop?**

Because a return from `wait` does not guarantee the condition is true: it may be a
spurious wakeup, or a `notify_all` may have woken several threads of which only one can
proceed (the others find the condition already consumed). Re-checking in a loop means the
thread only continues when the predicate actually holds and otherwise waits again. The
predicate overload of `wait` encodes this loop for you (`while (!pred()) wait(lock);`),
which is why it is the preferred form.

#### Q64

**What is the difference between `notify_one()` and `notify_all()`?**

`notify_one` wakes at most one waiting thread; `notify_all` wakes every waiting thread.
Use `notify_one` when a single unit of work was made available and any one waiter can
handle it (e.g. one item pushed to a queue), which avoids a thundering herd. Use
`notify_all` when the state change may let multiple waiters proceed or when different
waiters wait on different predicates sharing one condition variable (e.g. shutdown, or a
state that unblocks everyone). If in doubt about which waiters can proceed, `notify_all`
with a correct predicate is safe but may cause extra wakeups.

#### Q65

**Can a notification be lost?**

Yes. Notifications are not stored or queued: if `notify_one`/`notify_all` is called when
no thread is currently blocked in `wait`, the notification has no effect and is simply
lost. This is the classic lost-wakeup bug when the producer sets the condition and
notifies before the consumer reaches `wait`. The fix is to check a predicate over shared
state under the mutex, so that a waiter that arrives after the state changed sees it is
already true and never blocks.

#### Q66

**What is the difference between a lost wakeup and a spurious wakeup?**

A lost wakeup is a missed notification: an event/notify happened but no thread was waiting
to receive it, so a later waiter blocks forever (or until another notify). A spurious
wakeup is the opposite: a waiter wakes with no notification at all. Both are defended
against by the same technique: always wait on a predicate over shared state protected by
the mutex. The predicate makes spurious wakeups harmless (re-wait) and makes lost wakeups
impossible (a late waiter sees the state is already true and does not block).

#### Q67

**What is the purpose of `wait_for()` and `wait_until()`?**

They are timed waits: `wait_for` blocks for at most a relative duration and `wait_until`
until an absolute time point, instead of waiting indefinitely. They let you bound how long
a thread sleeps so it can periodically do other work, check a stop flag, or report a
timeout. The predicate overloads return `bool` indicating whether the predicate was
satisfied (true) or the timeout expired (false), and like plain `wait` they loop
internally to absorb spurious wakeups while respecting the deadline.

#### Q68

**How would you implement a producer-consumer queue using a condition variable?**

Protect the queue with a mutex, use one condition variable for "non-empty", and wait on a
predicate. The producer pushes under the lock and calls `notify_one`; the consumer waits
until the queue is non-empty or shutdown, then pops.

```cpp
template <class T>
class Queue {
    std::mutex m_;
    std::condition_variable cv_;
    std::queue<T> q_;
    bool stopped_ = false;
public:
    void push(T v) {
        { std::lock_guard lk(m_); q_.push(std::move(v)); }
        cv_.notify_one();
    }
    bool pop(T& out) {
        std::unique_lock lk(m_);
        cv_.wait(lk, [&] { return !q_.empty() || stopped_; });
        if (q_.empty()) return false;        // stopped and drained
        out = std::move(q_.front());
        q_.pop();
        return true;
    }
    void stop() {
        { std::lock_guard lk(m_); stopped_ = true; }
        cv_.notify_all();
    }
};
```

#### Q69

**What happens if the producer adds data before the consumer starts waiting?**

Nothing is lost, provided the consumer waits on a predicate. The producer sets the shared
state (pushes the item) under the mutex and notifies; even if that notify lands when no
one is waiting and is therefore lost, the consumer's `wait(lock, pred)` first evaluates
the predicate and sees the queue is already non-empty, so it returns immediately without
blocking. If the consumer instead used a bare `wait(lock)` with no predicate, that early
notification would be lost and the consumer could block forever despite data being
available.

#### Q70

**How should shutdown be implemented for a condition-variable-based worker queue?**

Add a `stopped` flag protected by the same mutex and include it in the wait predicate
(`return !queue.empty() || stopped;`). To shut down, set `stopped = true` under the lock
and call `notify_all` so every blocked worker wakes. Workers should typically drain
remaining items first and only exit once the queue is empty *and* `stopped` is set, which
gives a clean "finish queued work then stop" semantics. Using `notify_all` (not
`notify_one`) ensures all workers observe the shutdown rather than leaving some blocked.

## Atomics and the C++ memory model

#### Q71

**What is `std::atomic`?**

`std::atomic<T>` is a template wrapping a value so that operations on it are indivisible
with respect to other threads and free of data races, and so that it carries
memory-ordering semantics that establish inter-thread ordering. It provides operations
such as `load`, `store`, `exchange`, `fetch_add`, and `compare_exchange_*`, each taking an
optional `std::memory_order`. It is the standard-library tool for lock-free communication
between threads; accessing the same non-atomic object concurrently with a write would be a
data race and undefined behavior.

#### Q72

**Why is incrementing a normal integer from multiple threads unsafe?**

`++x` on a plain `int` is not atomic: it is a load, an increment, and a store, so two
threads can both read the same old value and each write back the same incremented value,
losing an update. Worse, concurrent access where at least one is a write is a data race,
which is undefined behavior in C++, so the compiler may assume it never happens and
optimize accordingly. The fix is `std::atomic<int>` with `fetch_add`, or a mutex.

#### Q73

**Is every operation on an atomic object automatically lock-free?**

No. `std::atomic<T>` guarantees atomicity and correct ordering but not that it is
implemented without locks; for types the hardware cannot handle with a single instruction,
the library may use an internal lock (e.g. a lock pool). Whether a given type is lock-free
can be queried with `is_lock_free()` at runtime or `is_always_lock_free` at compile time.
Only `std::atomic_flag` is guaranteed to be lock-free on every implementation.

#### Q74

**What does `is_lock_free()` indicate?**

It reports, for a particular atomic object at runtime, whether its operations are
implemented without locks (typically mapping to hardware atomic instructions). It can in
principle vary by alignment or platform, which is why it is a member function rather than
purely a compile-time trait. For a compile-time guarantee use the static
`is_always_lock_free` constant; if it is true, the type is lock-free on that
implementation unconditionally.

#### Q75

**What is the difference between atomicity, visibility, and ordering?**

Atomicity means an operation is indivisible: no thread observes a half-completed update.
Visibility means that a write performed by one thread becomes observable to another thread
(as opposed to being held in a register or store buffer indefinitely). Ordering means the
relative order in which one thread's memory operations become visible to others.
`std::atomic` provides atomicity for a single object, and its memory-order arguments
control visibility and ordering of surrounding operations; a relaxed atomic is atomic but
establishes no ordering of other accesses.

#### Q76

**What is the C++ memory model?**

It is the part of the standard that defines how threads interact through memory: what
constitutes a data race (UB), and the rules – happens-before, synchronizes-with,
sequenced-before, and the modification order of each atomic – that determine which writes
a read may observe. It abstracts away specific CPU memory models so portable reasoning is
possible. In short, if your program is free of data races and you respect the memory-order
semantics of your atomics, the model defines exactly what results are permitted.

#### Q77

**What is a happens-before relationship?**

Happens-before is the partial order the memory model uses to decide which side effects are
visible to a given evaluation. It is formed from sequenced-before (ordering within a
thread) composed with inter-thread synchronizes-with edges. If a write happens-before a
read of the same location and no other write intervenes, the read must observe that write;
if two conflicting accesses are unordered by happens-before and at least one is a write,
you have a data race (UB). It is the central concept for reasoning about correctness of
concurrent code.

#### Q78

**What is a synchronizes-with relationship?**

Synchronizes-with is an inter-thread ordering edge created by matching atomic operations,
most importantly a release store that is observed by an acquire load of the same atomic
object. When an acquire load reads the value written by a release store, the release
"synchronizes with" the acquire, and everything sequenced before the release in the writer
happens-before everything sequenced after the acquire in the reader. This is the mechanism
that lets you publish non-atomic data through an atomic flag safely.

#### Q79

**What is sequential consistency?**

Sequential consistency means all sequentially-consistent operations across all threads
appear to execute in a single total order that is consistent with each thread's program
order. It is the strongest and most intuitive model – the default for `std::atomic`
operations (`memory_order_seq_cst`) - and it rules out the subtle reorderings that
acquire/release alone permit (such as the Dekker/store-buffer anomaly). The guarantee
costs more on some hardware because it requires additional fences.

#### Q80

**What are the C++ memory-order options?**

- `memory_order_relaxed` – atomicity only, no ordering or synchronization with other
  accesses.
- `memory_order_acquire` – on a load; no reads/writes after it can be reordered before it,
  and it can form a synchronizes-with edge.
- `memory_order_release` – on a store; no reads/writes before it can be reordered after
  it, pairs with an acquire.
- `memory_order_acq_rel` – forread-modify-write ops; both acquire and release semantics.
- `memory_order_seq_cst` – acquire/release plus participation in a single global total
  order; the default.
- `memory_order_consume` – a weaker, dependency-ordered form of acquire; its use is
  discouraged and in practice every major compiler promotes it to acquire.
 
#### Q81

**When is `memory_order_relaxed` appropriate?**

When you need the operation to be atomic but do not need to order any other memory
accesses relative to it. The classic example is a statistics counter incremented with
`fetch_add(1, relaxed)`: you only care that increments are not lost and that the final
total is correct, not that the increment publishes other data. It is also used for the
increment side of reference counts (the decrement-to-zero needs acquire/release). Never
use it to publish non-atomic data, because it establishes no happens-before.

#### Q82

**What guarantee does a release operation provide?**

A release store guarantees that all memory writes sequenced before it in the same thread
are completed and will become visible to any thread that performs an acquire operation
reading that stored value (or a later value in its release sequence). It prevents
preceding reads and writes from being reordered after the store. On its own it
synchronizes nothing; it only takes effect when paired with a matching acquire that
observes the released value.

#### Q83

**What guarantee does an acquire operation provide?**

An acquire load guarantees that once it reads a value written by a release store (or its
release sequence), all memory writes the releasing thread made before that release are
visible to the acquiring thread, and that no reads or writes sequenced after the acquire
are reordered before it. In other words it "receives" the publication made by the release.
Alone it provides only the no-reorder-downward barrier; the synchronizes-with edge
requires a matching release whose value it actually reads.

#### Q84

**Why are acquire and release commonly used for producer-consumer communication?**

Because they express exactly the publish/consume pattern cheaply: the producer writes the
data (non-atomic) and then does a release store to a flag/index; the consumer does an
acquire load of that flag and, once it sees the published value, is guaranteed to see all
the data writes that preceded the release. This gives correct visibility without the cost
of full sequential consistency and without a mutex. It is the standard idiom for lock-free
single-writer publication and for ring-buffer head/tail indexes.

#### Q85

**What does "memory_order_seq_cst provide beyond acquire and release?**

In addition to acquire and release semantics, all `seq_cst` operations participate in a
single global total order observed consistently by every thread. This rules out anomalies
that acquire/release permits, notably the store-buffer case where two threads each store
to one variable with release and then load the other with acquire and both can read the
old value. With `seq_cst` such an outcome is forbidden because a consistent total order
must exist.

#### Q86

**Why is "memory_order_seg_cst" often easier to reason about?**

Because you can reason as if there is one global interleaving of all atomic operations
consistent with program order, which matches most programmers' intuition and makes classic
concurrent algorithms behave as textbook analysis assumes. You avoid the subtle,
hard-to-test reorderings that relaxed/acquire/release can expose. It is the default for
exactly this reason: correct first, then weaken the ordering deliberately if profiling
shows it matters.

#### Q87

**What are the performance and correctness risks of using relaxed memory ordering?**

Correctness: relaxed operations create no happens-before, so using them to publish data or
coordinate ordering leads to subtle bugs where one thread sees stale or reordered data;
such bugs are rare, hardware-dependent, and extremely hard to reproduce and test.
Performance: on strongly-ordered hardware like x86 you may gain little over
acquire/release because loads/stores are already fairly ordered, so the added risk buys
little. The guidance is to use relaxed only where no cross-thread ordering is needed (e.g.
independent counters) and to default to `seq_cst` otherwise.

#### Q88

**What is a release sequence?**

A release sequence headed by a release store is that store followed, in the atomic
object's modification order, by any number of read-modify-write operations (by any thread
and relaxed stores by the same thread. An acquire load synchronizes-with the head release
if it reads any value in this sequence, not only the original released value. This is what
makes patterns like a reference-count decrement chain or a lock-free queue work: later
RMWs do not break the publication established by the original release.

#### Q89

**Why does a data race remain undefined behavior even on hardware that appears to execute
safely?**

Because the C++ abstract machine, not the hardware, defines program meaning: a data race
is UB by definition, which licenses the compiler to optimize assuming races never occur
(hoisting loads out of loops, reusing register values, reordering, tearing). So even if
the CPU would execute the naive machine code "correctly," the generated code may be
nothing like the naive translation, and behavior can change with optimization level,
compiler, or surrounding code. Apparent safety in a test build is not a guarantee; the
only fix is to remove the race with atomics or locks.

#### Q90

**Does `volatile` make operations thread-safe?**

No. `volatile` only tells the compiler the object may change outside the program's
control, so it must not elide or reorder accesses to that object at compile time; it
provides neither atomicity nor any inter-thread ordering/visibility guarantee, and does
not prevent CPU reordering. Using `volatile` for thread communication is a data race and
UB. Its legitimate uses are memory-mapped I/O and signal handlers
(`volatile sig_atomic_t`), not multithreading.

#### Q91

**What is the difference between `volatile` and `std::atomic`?**

`volatile` prevents the compiler from optimizing away or reordering accesses to a specific
object but gives no atomicity, no memory-ordering across other objects, and no protection
against CPU reordering or torn reads. `std::atomic` guarantees indivisible operations and
provides the memory-order semantics that establish happens-before between threads. For
thread communication use `std::atomic`; `volatile` is for hardware registers and signal
handlers. (They are orthogonal – you can even have `volatile std::atomic<T>` for an MMIO
atomic, though that is rare.)

#### Q92

**Can an atomic operation prevent all compiler and CPU reordering?**

Only a `seq_cst` operation (or a `seq_cst` fence) prevents reordering globally enough to
enforce a single total order; weaker orders restrict reordering only in specific
directions. `acquire` stops later accesses from moving before it and `release` stops
earlier accesses from moving after it, but neither is a full barrier, and `relaxed`
imposes no ordering on surrounding accesses at all. So "atomic" does not automatically
mean "no reordering"; the memory order you choose determines exactly which reorderings are
forbidden.

#### Q93

**What is an atomic flag?**

`std::atomic_flag` is the simplest atomic type, representing a single boolean-like flag
with `test_and_set` and `clear` operations. It is the only atomic type the standard
guarantees to be always lock-free on every implementation. Since C++20 it also has
`test()` (a non-modifying read) and the `wait`/`notify_one`/`notify_all` blocking
interface, making it usable as a lightweight building block for spinlocks and simple
signaling.

#### Q94

**When would you use `std::atomic_flag` instead of `std::atomic<bool>`?**

Use `std::atomic_flag` when you need a guaranteed-lock-free primitive and only need
set/clear/test semantics – classically to build a spinlock via `test_and_set`.
`std::atomic<bool>` is more convenient (it supports plain `load`/`store`/`exchange` and
reads like a boolean) but is not guaranteed lock-free on every platform, though in
practice it almost always is. In modern C++ the ergonomic gap has narrowed since
`atomic_flag` gained `test()` and wait/notify, but `atomic<bool>` remains the more natural
choice unless the hard lock-free guarantee matters.

#### Q95

**What is `std::atomic_ref`?**

`std::atomic_ref<T>` (C++20) provides atomic operations on an existing non-atomic object
referred to by the reference, without that object being declared `std::atomic`. It lets
you apply atomic access selectively, e.g. to an element of an array or a field of a struct
you cannot change the type of, for the lifetime of the `atomic_ref`. While any
`atomic_ref` to an object exists, all access to that object must go through an
`atomic_ref`, or you have a data race.

#### Q96

**What restrictions apply when using `std::atomic_ref`?**

The referenced object must satisfy `atomic_ref<T>::required_alignment` (which can be
stricter than `alignof(T)`); referencing a misaligned object is UB. For as long as any
`atomic_ref` to an object exists, every access to that object – reads and writes, by any
thread – must be made through an `atomic_ref`; mixing a plain access with atomic_ref
access is a data race. The referenced object must also outlive the `atomic_ref`, and the
referent is not const.

#### Q97

**Can an atomic object be copied?**

No. `std::atomic` is neither copy-constructible nor copy-assignable (and not movable); its
copy/move operations are deleted. This is because a "copy" would require atomically
reading one object and writing another, which cannot be done as a single atomic step, so
the standard forbids it to avoid a false sense of atomicity. You can construct from a
plain `T`, and you can transfer a value explicitly via `b.store(a.load())`, accepting that
the two loads/stores are separate atomic operations.

#### Q98

**How do you atomically update a compound value?**

For a value that depends on the previous value (and is not a single built-in RMW like
`fetch_add`), use a compare-and-exchange loop: load the current value, compute the new
value, then `compare_exchange_weak(expected, desired)` and retry if it fails (expected is
refreshed with the current value on failure). For simple arithmetic/bitwise updates,
prefer the dedicated `fetch_add`/`fetch_sub`/`fetch_or`/etc. which are single atomic RMWS.
If the "compound" is really multiple independent variables that must change together, an
atomic cannot express that transaction – use a mutex.

## Compare-and-exchange

#### Q99

**What is compare-and-exchange?**

Compare-and-exchange (CAS) is an atomic read-modify-write: it atomically compares the
atomic object with an `expected` value and, if they are equal, stores a `desired` value
and returns true; if not equal it writes the current value back into `expected` and
returns false. It is the fundamental primitive for lock-free algorithms because it lets a
thread commit an update only if no other thread changed the value in the meantime. In C++
it is `compare_exchange_weak`/`compare_exchange_strong`, each taking success and
(optionally) failure memory orders.

#### Q100

**What is the difference between `compare_exchange_weak()` and
`compare_exchange_strong()`?**

`compare_exchange_strong` fails only when the values genuinely differ.
`compare_exchange_weak` may also fail spuriously – returning false even though `expected`
equals the current value – because on some architectures (LL/SC, e.g. ARM/PowerPC) the
primitive can be interrupted. `weak` can therefore be cheaper and is intended for use
inside a retry loop where a spurious failure just costs one extra iteration; `strong` is
preferred when there is no loop (a single-shot CAS) so a spurious failure would be wrong
or wasteful.

#### Q101

**Why can `compare_exchange weak()` fail even when the comparison appears to succeed?**

Because on load-linked/store-conditional architectures the store-conditional can fail for
reasons unrelated to the value – an intervening interrupt, context switch, or cache-line
eviction between the load-linked and the store-conditional – causing a spurious failure
even though the value still equals `expected`. The standard explicitly permits this for
the weak form so implementations can map it directly to a single LL/SC attempt without an
internal loop. The caller's own loop absorbs the spurious failure.

#### Q102

**Why is the weak form usually used inside a loop?**

```cpp
auto expected = old_value;

while (!value.compare_exchange_weak(
    expected,
    new_value,
    std::memory_order_acq_rel,
    std::memory_order_acquire)) {
    // expected is updated after failure
}
```

Because `weak` can fail spuriously, a single call is unreliable; you must retry. A loop is
needed anyway in a real CAS algorithm because a genuine failure means another thread
changed the value, so you must recompute `new_value` from the refreshed `expected` and try
again. Since you are looping regardless, the weak form's possible spurious failure costs
nothing extra and may be faster per attempt, which is why `weak` is the idiomatic choice
inside CAS loops.

#### Q103

**What happens to the expected value after a failed compare-and-exchange?**

On failure, the current (actual) value of the atomic is loaded into the `expected`
argument (it is passed by reference and updated in place). This is convenient: your loop
can immediately recompute the desired value from the up-to-date `expected` without a
separate reload. On success, `expected` is left unchanged. Note that for the weak form a
spurious failure also writes back the current value into `expected`, which simply equals
what you already had.

#### Q104

**What is the ABA problem?**

The ABA problem is when a CAS incorrectly succeeds because a value changed from A to B and
back to A between a thread's read and its CAS, so the CAS sees "still A" and assumes
nothing happened – when in fact the underlying state did change (e.g. a node was freed and
reused). It is dangerous in pointer-based lock-free structures like stacks, where a reused
address can corrupt the structure. CAS compares only the value (the pointer bits), not the
history, so it cannot detect the intervening changes.

#### Q105

**How can tagged pointers or version counters help solve the ABA problem?**

By pairing the value with a monotonically increasing counter and CAS-ing the pair
atomically: even if the pointer returns to A, the version/tag will have advanced, so the
CAS sees A/N vs the current A/N+2 and correctly fails. Implementations either steal
low/high bits of an aligned pointer for a tag (tagged pointer) or use a double-width CAS
over {pointer, counter} (e.g. `cmpxchg16b`). This makes a return to the same value
distinguishable from "never changed." Alternatives that avoid reuse altogether include
hazard pointers and epoch/RCU reclamation.

#### Q106

**What is the difference between a fetch-add operation and a compare-and-exchange loop?**

`fetch_add` is a single hardware atomic RMW: it always succeeds in one step, is wait-free,
and is the right tool when the update is a fixed arithmetic delta independent of complex
conditions. A CAS loop is general-purpose: it reads, computes an arbitrary new value from
the old, and conditionally commits, retrying on contention – necessary when the new value
is a non-trivial function of the old (e.g. atomic max, clamping, updating a packed
struct). Prefer `fetch_add` when it fits because it avoids retries and has predictable,
lock-free progress.

#### Q107

**How would you implement an atomic maximum operation?**

There is no `fetch_max` for general types before C++26, so use a CAS loop: load the
current value, and if the candidate is larger, try to install it, retrying while another
thread beats you.

```cpp
void atomic_store_max(std::atomic<int>& a, int v) {
    int cur = a.load(std::memory_order relaxed):
    while (v > cur &&
           !a.compare_exchange_weak(cur, v,
                std::memory_order_release,
                std::memory_order_relaxed)) {
            // cur is refreshed with the latest value on failure
    }
}
```

On failure `cur` is reloaded automatically; the loop re-tests `v > cur`, so it stops as
soon as some value >= v is present, never lowering the stored maximum.

## Locks versus atomics

#### Q108

**When should a mutex be preferred over an atomic?**

Prefer a mutex when the invariant spans more than one variable or a non-trivial critical
section – anything a single atomic RMW cannot express as one indivisible step. Mutexes are
also clearer and less error-prone, allow arbitrary code (including calls that may block or
throw) inside the critical section, and compose far better than hand-rolled atomic
protocols. Reach for atomics only for small, well-understood, single-location operations
(a counter, a flag, a published pointer/index) where profiling justifies it.

#### Q109

**When is an atomic counter appropriate?**

When many threads need to increment/decrement a single integer and you only care about the
aggregate value, not about ordering other data relative to it – e.g. statistics, hit
counts, or reference counts. `fetch_add(1, relaxed)` is ideal for a pure counter
(reference counts need release on decrement and acquire before destruction). It avoids
mutex overhead and blocking. If the counter is highly contended, consider per-thread
counters aggregated later to avoid the cache-line ping-pong of one shared atomic.

#### Q110

**Why is replacing every mutex with atomics usually a bad idea?**

Because atomics only make individual operations atomic; they cannot make a multi-variable
invariant hold as a transaction, so naively "atomic-izing" each variable introduces subtle
races between them. Lock-free code built from raw atomics is extremely hard to get right
(memory ordering, ABA, reclamation) and hard to test, and it often is not even faster
under contention due to cache-line contention and retries. Mutexes are simpler,
composable, and frequently perform comparably; atomics should be a targeted optimization,
not a blanket replacement.

#### Q111

**Can multiple atomic variables be updated as one transaction?**

No. Each atomic operation is independent and atomic only with respect to its own object;
there is no way to update two separate atomics so that other threads see both changes as a
single indivisible step. Another thread can observe one updated and the other not. If you
need a multi-variable transaction, use a mutex to guard all of them together, or redesign
so the related data fits in one atomically-updatable object (e.g. pack into a single word,
or use a double-width CAS).

#### Q112

**How would you protect an invariant involving multiple variables?**

Guard all the variables with the same mutex and update them together inside one critical
section, so no other thread can observe a partially-updated, invariant-violating state.
Keep the critical section scoped (RAII `lock_guard`/`scoped_lock`) and as small as
practical. If you must avoid a lock for performance, the only atomic-based options are to
pack the whole invariant into a single atomic word (or a type supporting double-width CAS)
and update it with one CAS, or to use a published-pointer/RCU scheme that swaps in a new
immutable snapshot atomically.

#### Q113

**Why can an atomic operation still be expensive?**

Because an atomic RMW or a `seq_cst` operation typically requires cache-coherency traffic
and/or memory fences: the core must obtain exclusive ownership of the cache line (RFO),
which under contention causes the line to bounce between cores, and fences inhibit
CPU/store-buffer optimizations. So even though it is "lock-free," a hot shared atomic can
be slower than expected and even slower than a well-designed mutex in some workloads.
False sharing and high contention amplify this; the cost is in the coherency and ordering,
not just the instruction.

#### Q114

**What is the difference between lock-based and lock-free algorithms?**

Lock-based algorithms use mutual exclusion so that only one thread is in a critical
section at a time; if the lock holder is delayed (preempted, blocked), other threads must
wait, so overall progress depends on that thread. Lock-free algorithms use atomic
operations (typically CAS loops) so that the system as a whole always makes progress
regardless of any individual thread being delayed – no thread can block all others by
holding a lock. Lock-free avoids deadlock, priority inversion, and convoying, at the cost
of much greater complexity and the memory-reclamation problem.

#### Q115

**What do wait-free, lock-free, and obstruction-free mean?**

These are progress guarantees, from strongest to weakest. Wait-free: every thread
completes its operation in a bounded number of its own steps, regardless of other threads
– no starvation. Lock-free: at least one thread makes progress in a bounded number of
steps system-wide, so the whole system progresses, but an individual thread can starve
(e.g. always losing its CAS). Obstruction-free: a thread completes in bounded steps if it
runs in isolation (no contention), but concurrent threads may indefinitely interfere, so
it can livelock without a contention manager.

#### Q116

**Does lock-free mean faster?**

No. Lock-free is a progress guarantee, not a performance guarantee. Under high contention,
CAS retry loops waste work and the shared cache line bounces between cores, so a lock-free
structure can be slower than a simple mutex-based one; mutexes also let a waiting thread
sleep instead of spinning. Lock-free wins mainly where blocking is unacceptable
(real-time, signal/interrupt contexts, avoiding priority inversion) or at moderate
contention with good design. Always measure rather than assuming lock-free is faster.

#### Q117

**Does lock-free mean free from starvation?**

No. Lock-free only guarantees that *some* thread makes progress system-wide; a particular
thread can repeatedly lose its compare-exchange to faster threads and never complete, i.e.
starve. Freedom from starvation is the stronger *wait-free* guarantee, where every thread
finishes in a bounded number of its own steps. So lock-free rules out deadlock and
system-wide stalls, but not individual-thread starvation.

## False sharing and cache behavior

#### Q118

**What is false sharing?**

False sharing occurs when two or more threads modify distinct variables that happen to
reside on the same cache line. The cache-coherence protocol (e.g. MESI) works at
cache-line granularity, so even though the threads touch logically independent data, each
write invalidates the line in the other cores' caches, forcing the line to bounce between
cores. The result is correctness-neutral but can devastate performance, turning what looks
like contention-free code into a heavily serialized workload.

#### Q119

**How can two logically independent variables cause cache-line contention?**

If two variables sit within the same ~64-byte cache line (e.g. adjacent members of a
struct or neighboring elements in an array), a write to one marks the entire line dirty
and invalidates every other core's copy. A thread reading or writing the "othen" variable
then suffers a coherence miss and must re-fetch the line, even though its own datum never
changed. The variables are independent at the language level but coupled at the hardware
level because the line is the unit of coherence.

#### Q120

**How can `alignas` help reduce false sharing?**

```cpp
struct alignas(64) Counter {
    std::atomic<std::uint64_t> value{0};
};
```

`alignas(64)` forces each object onto its own cache line (and pads its size up to a
multiple of 64), so an array of `Counter` places each counter on a separate line and
writes from different threads no longer invalidate each other. In portable code you would
use `alignas(std::hardware_destructive_interference_size)`. Note that alignment alone
separates the *start* of the object; you also need padding so the *tail* of one object
does not share a line with the next hot datum.


#### Q121

**Why is the cache-line size platform-dependent?**

Cache-line size is a microarchitectural choice made by the CPU designer, not the language.
Most x86-64 and many ARM cores use 64-byte lines, but Apple M-series and some POWER chips
use 128 bytes, and some adjacent-line prefetchers on x86 effectively couple two 64-byte
lines. Because it varies, hard-coding 64 is fragile.
`std::hardware_destructive_interference_size` (C++17) exposes a hint, but it is a
*compile-time constant* chosen by the implementation and may not match the CPU the binary
actually runs on, which is why 128-byte padding is sometimes the safer choice.

#### Q122

**Is aligning a variable to 64 bytes always sufficient?**

No. On architectures with 128-byte lines or with adjacent-line (spatial) prefetchers that
pull in a 128-byte pair, 64-byte alignment still allows two hot objects to share an
effective coherence unit. It also only aligns the start of the object; if the object is
smaller than a line, a neighboring hot variable can share the remainder. The robust
approach is to align *and* pad to `std::hardware_destructive_interference_size`, and on
uncertain targets pad to 128 bytes.

#### Q123

**What are the costs of padding objects to separate cache lines?**

Padding trades memory and cache footprint for reduced coherence traffic. Each padded
counter consumes a full line (64 or 128 bytes) regardless of how little real data it
holds, so an array of them uses far more memory and occupies more of the limited cache,
increasing capacity misses and hurting data that is only read. It can also waste bandwidth
when you stream through the array. Padding is a win only when the objects are genuinely
written concurrently by different threads.

#### Q124

**Why can per-thread counters outperform one shared atomic counter?**

A single shared atomic counter is a single cache line that every incrementing thread must
acquire in exclusive state, serializing the increments and generating heavy coherence
traffic (the line ping-pongs across cores). Per-thread counters give each thread its own
line that it owns and writes without contention; aggregation happens rarely, by summing
the per-thread values. This replaces N contended atomic RMW operations per unit time with
N independent local writes, which scales far better.

#### Q125

**How would you design cache-conscious producer and consumer indexes?**

Place the producer's write index (tail/head) and the consumer's index on separate cache
lines, each `alignas(hardware_destructive_interference_size)`, so the producer writing its
index does not invalidate the consumer's line and vice versa. Additionally, each side
caches a snapshot of the other's index locally and only re-reads the shared index when the
local snapshot indicates the queue is full/empty, minimizing cross-core reads. This is the
classic layout used in high-performance SPSC ring buffers.

#### Q126

**What is the difference between sharing data and sharing cache lines?**

Sharing data means threads access the *same logical variable* and therefore genuinely need
synchronization for correctness. Sharing a cache line (false sharing) means threads access
*different* variables that merely happen to be colocated; there is no correctness
requirement to share, only an accidental performance coupling imposed by the hardware's
line granularity. True sharing is a design property; false sharing is a layout artifact
you fix with padding/alignment without changing semantics.

#### Q127

**How can object layout affect a multithreaded container?**

Layout determines which fields land on the same cache line, so poor layout can put a
mutex, a size counter, and head/tail pointers on one line that every operation touches,
serializing otherwise independent operations. Grouping hot fields written by different
roles (producer vs consumer) onto separate lines, and separating frequently-written fields
from read-mostly fields, reduces coherence traffic. Conversely, interleaving per-element
metadata with data can cause element updates by different threads to collide on shared
lines.

#### Q128

**Why might reducing the size of a structure make a multithreaded program slower?**

Shrinking a struct can pack more instances into a single cache line, so objects that were
previously on separate lines now share one and begin to suffer false sharing when written
by different threads. A size optimization that improves single-threaded cache density can
therefore increase multi-core coherence traffic and reduce throughput. The lesson is that
size and alignment tuning must account for concurrent write patterns, not just footprint.

## Futures, promises, and asynchronous tasks

#### Q129

**What is `std::future`?**

`std::future<T>` is a one-time handle to the result of an asynchronous operation; it
refers to a shared state that will eventually hold a value or an exception. You call
`get()` (which blocks until the result is ready and then moves it out), `wait()` or
`wait_for()`/`wait_until()` for timed waiting. A default-constructed or already-consumed
future holds no shared state and `valid()` returns false.

#### Q130

**What is 'std::promise ?**

`std::promise<T>` is the producer-side counterpart to `std::future`: it owns a shared
state into which a thread writes a result via `set_value()` or an exception via
`set_exception()`. You obtain the associated future once with `get_future()`. Setting the
value/exception makes the result ready and unblocks any waiter; destroying the promise
without setting anything stores a `broken_promise` exception in the shared state.

#### Q131

**How do `promise` and `future` communicate a result?**

The promise and its future share a reference-counted shared state allocated on the heap.
The producer thread calls `promise.set_value(x)` (or `set_exception`), which stores the
result and signals readiness; the consumer thread calls `future.get()`, which blocks until
ready and then retrieves (moves) the result. This provides a thread-safe, one-shot
hand-off with a happens-before relationship: everything the producer did before
`set_value` is visible to the consumer after `get`.

#### Q132

**How are exceptions transferred through a future?**

The producer captures the exception with `std::current_exception()` inside a `catch` and
stores it via `promise.set_exception(eptr)` (for `std::async`/`packaged_task` this happens
automatically when the task throws). The exception is held in the shared state, and when
the consumer calls `future.get()`, that stored exception is rethrown in the consumer's
thread. This lets errors propagate across thread boundaries with full type information.

#### Q133

**What happens when `future::get()` is called more than once?**

`get()` has a precondition `valid() == true`, and it invalidates the future by moving the
result out, so after the first successful call `valid()` becomes false. Calling `get()`
again violates the precondition and is undefined behavior; standard implementations
typically throw `std::future_error` with `std::future_errc::no_state`. If you need
multiple retrievals or multiple waiters, use `std::shared_future`.

#### Q134

**What is `std::async`?**

`std::async` launches a callable and returns a `std::future` for its result, handling the
shared-state plumbing for you. Depending on the launch policy it may run the task on a new
thread (`std::launch::async`) or lazily on the calling thread at `get()` time
(`std::launch::deferred`). Exceptions thrown by the task are captured and rethrown at
`get()`.

#### Q135

**What is the difference between `std::launch::async` and `std::launch::deferred`?**

`std::launch::async` guarantees the task runs on a separate thread, starting (potentially)
immediately and concurrently. `std::launch::deferred` performs lazy evaluation: the task
does not run until `get()` or `wait()` is called, and then it runs synchronously on the
calling thread. With deferred, no new thread is created and `wait_for` never reports
`ready` (it returns `deferred`).

#### Q136

**Why can relying on the default launch policy be risky?**

The default is `std::launch::async | std::launch::deferred`, letting the implementation
choose. If it picks deferred, the task never runs unless you call `get()`/`wait()`, so
code that "fires and forgets" or polls with `wait_for` can deadlock or silently never
execute. You also lose any guarantee of true concurrency, and thread-local state or timing
assumptions break. Always pass an explicit policy when behavior matters.

#### Q137

**What is `std::packaged_task`?**

`std::packaged_task<R(Args...)>` wraps a callable so that invoking it stores the result
(or exception) into an associated shared state, which you access through `get_future()`.
Unlike `std::async`, it does not launch anything itself: you invoke it explicitly,
typically on a thread or inside a thread pool. It decouples "producing the future" from
"deciding where/when to run," making it the natural building block for custom schedulers.

#### Q138

**Compare `std::async`, `std::packaged_task`, and a custom thread pool.**

`std::async` is the highest level: it both schedules and runs a task, but gives little
control over threads and may create one per call. `std::packaged_task` produces the
future/result plumbing but leaves execution to you, so you decide the thread or queue. A
custom thread pool gives full control over the number of threads, queueing, backpressure,
and reuse, avoiding per-task thread creation; it typically uses `packaged_task` (or
`promise`) internally to deliver results. Choose async for convenience, packaged_task for
flexibility, and a pool for throughput and resource control.

#### Q139

**What is the purpose of `std::shared_future`?**

`std::shared_future<T>` allows the result of an asynchronous operation to be retrieved
multiple times and by multiple threads. Unlike `std::future`, its `get()` does not
invalidate the state and returns a const reference to the stored value, so many consumers
can wait on and read the same result. You create one via `future.share()` or by copying an
existing shared_future. Concurrent `get()` calls on the same shared_future from different
threads are safe.

#### Q140

**What happens if a future referring to an asynchronous task is destroyed?**

In general, destroying a `std::future` just releases its reference to the shared state and
does not block. The special case is the future returned by `std::async` with
`launch::async`: if it holds the *last* reference to the shared state, its destructor
blocks until the task completes (effectively a join). Futures from `promise` or
`packaged_task` do not have this blocking behavior, and additional futures sharing the
state do not block either.

*Red flag:* writing `std::async(f); std::async(g);` as separate statements serializes
them, because each temporary future blocks in its destructor at the end of its
full-expression.

## Thread pools and task scheduling

#### Q141

**Why use a thread pool instead of creating a new thread for every task?**

Thread creation and destruction are expensive (stack allocation, kernel thread setup,
scheduler bookkeeping), and spawning one thread per task can cause unbounded
oversubscription, destroying cache locality and thrashing the scheduler. A pool amortizes
creation cost by reusing a fixed set of worker threads, bounds concurrency to match
hardware, and provides a central point for queueing, backpressure, and shutdown. This
yields more predictable latency and higher throughput under load.

#### Q142

**What components are needed in a basic thread pool?**

At minimum: a set of worker threads, a task queue (typically guarded by a mutex), a
condition variable to wake idle workers when work arrives, and a stop/shutdown flag. You
also need a submit interface that enqueues a callable and usually returns a `std::future`
(via `packaged_task`), plus join logic in the destructor to drain and stop cleanly.

#### Q143

**How should a thread pool be shut down?**

Set a `stop` flag under the queue mutex, then `notify_all()` on the condition variable so
every waiting worker wakes, observes stop, drains (or discards per policy) remaining
tasks, and exits its loop. Finally `join()` all worker threads in the destructor. The flag
must be checked inside the condition-variable predicate so no worker sleeps through
shutdown, and new submissions after stop should be rejected.

#### Q144

**How do worker threads wait for new tasks?**

Each worker loops: acquire the queue mutex,
`cv.wait(lock, [&]{ return stop || !queue.empty(); })` so it sleeps while the queue is
empty and no stop is requested, then dequeues a task, releases the lock, and runs the task
outside the lock. The predicate form guards against spurious wakeups and lost
notifications, and running the task without holding the mutex avoids blocking other
workers.

#### Q145

**What happens to queued tasks during shutdown?**

That is a policy choice. A *graceful* shutdown drains the queue, running all pending tasks
before workers exit. An *immediate* shutdown discards queued tasks and stops as soon as
in-flight tasks finish. Futures tied to discarded tasks must be resolved somehow (e.g. a
`broken_promise` exception when the `packaged_task` is destroyed unexecuted), so document
and hand that so waiters do not hang.

#### Q146

**Should a thread pool have one queue or one queue per worker?**

A single shared queue is simple and gives natural load balancing, but it becomes a
contention point and a false-sharing/coherence bottleneck at high thread counts.
Per-worker queues reduce contention and improve locality, but can leave some workers idle
while others are backlogged. The common high-performance compromise is per-worker queues
plus work stealing, getting low contention with balanced load.

#### Q147

**What is work stealing?**

Work stealing gives each worker its own double-ended task queue: the owner pushes and pops
from one end (LIFO, good for locality), and idle workers "steal" from the opposite end
(FIFO) of a busy worker's deque. This keeps workers busy without a single global
contention point, balances load dynamically, and favors cache locality for the owning
thread. It is the scheduling model used by TBB, Cilk, and many fork-join frameworks.

#### Q148

**What are the benefits and costs of work-stealing queues?**

Benefits: low contention (owners usually operate on their own deque uncontended), good
load balancing, and cache-friendly LIFO execution of recently spawned work. Costs: more
complex, often lock-free, deque implementation; steals touch remote cache lines and can
cause coherence traffic; fairness and ordering guarantees are weaker; and reasoning about
memory ordering/correctness is harder. The complexity is justified mainly for
fine-grained, highly parallel workloads.

#### Q149

**How can task granularity affect thread-pool performance?**

Too-fine granularity makes per-task overhead (enqueue, synchronization, wakeups,
scheduling) dominate the actual work, so parallel speedup is lost to bookkeeping and
contention. Too-coarse granularity limits parallelism and causes load imbalance, leaving
cores idle while one long task runs. The goal is tasks large enough to amortize scheduling
overhead yet small enough to balance load across workers, often achieved by chunking or
adaptive splitting.

#### Q150

**What is queue backpressure?**

Backpressure is the mechanism by which a full or overloaded task queue pushes resistance
back to producers so they slow down or stop submitting, preventing unbounded memory growth
and latency blow-up. With a bounded queue, submission blocks (or fails) when the queue is
full, signaling producers that the system cannot currently absorb more work. It couples
producer rate to consumer capacity and is essential for stability under overload.

#### Q151

**How should a bounded task queue behave when it is full?**

Per policy: it can *block* the producer until space frees up (backpressure), *reject* the
submission (return an error / throw / return false), *drop* the oldest or newest item, or
*overwrite* stale data. Blocking is the usual default for work that must not be lost;
rejecting is appropriate when callers can shed load; dropping/overwriting suits real-time
or telemetry data where freshness beats completeness. The choice depends on whether losing
work is acceptable.

#### Q152

**How can priority scheduling introduce starvation?**

If high-priority tasks are always dequeued ahead of low-priority ones, a steady stream of
high-priority work can indefinitely postpone low-priority tasks, which never make
progress. This is starvation. Mitigations include aging (gradually raising the priority of
long-waiting tasks), reserving capacity or time slices for lower priorities, or using
fair/weighted scheduling so every class guarantees some forward progress.

#### Q153

**How do you prevent a task from waiting forever?**

Use timeouts on waits (`wait_for`/`try_lock_for`), ensure shutdown is observable via a
stop flag/`stop_token` checked in every blocking predicate, and avoid priority schemes
without aging. Avoid dependency cycles where a task in the pool blocks on another task
that can never be scheduled (see nested parallelism). Bounding queue depth and providing
cancellation give every task a path to either run or fail promptly rather than hang.

#### Q154

**What is nested parallelism, and why can it exhaust a thread pool?**

Nested parallelism is when a task running on a pool worker itself submits subtasks to the
*same* pool and then blocks waiting for them. If all worker threads are simultaneously
blocked waiting on subtasks that need a free worker to run, the pool deadlocks: no thread
is available to execute the queued children. Remedies include work-stealing schedulers
where a blocked worker can execute pending tasks, running child tasks inline, using
separate pools, or continuation-style (non-blocking) composition instead of blocking
joins.

## Thread-safe containers and queues

#### Q155

**Are C++ standard containers thread-safe?**

Not in general. The standard guarantees only that distinct objects can be used
independently by different threads, and that concurrent operations on a *single* container
are safe only when they are all read-only. Any configuration with a concurrent writer
requires external synchronization. The containers are designed for zero-overhead
single-threaded use, not built-in thread safety.

#### Q156

**Can multiple threads read the same container concurrently?**

Yes. Concurrent calls to `const` member functions of a standard container on the same
object are safe, as are read-only operations like `size()`, iteration, and lookup,
provided no thread is modifying the container at the same time. Const correctness is the
rule of thumb: if every thread only observes, no synchronization is needed.

#### Q157

**Can one thread read a container while another thread modifies it?**

No, not without external synchronization. A concurrent writer plus any other access (read
or write) to the same container is a data race and undefined behavior. Modifying
operations may reallocate, invalidate iterators/references, and leave the structure
momentarily inconsistent, so you must serialize with a mutex or use a reader-writer lock /
a dedicated concurrent container.

#### Q158

**Can different threads modify different elements of the same container?**

Yes, for most containers: modifying *distinct elements* concurrently is safe and is not
considered modifying the container itself, as long as no thread performs an operation that
touches the container's structure (insert, erase, rehash, reallocation). The explicit
exception is `std::vector<bool>`, whose proxy bit-packing means separate "elements" share
storage and cannot be modified concurrently.

#### Q159

**What additional rules apply to `std::vector<bool>`?**

`std::vector<bool>` is a space-optimized specialization that packs booleans into bits, so
multiple logical elements live in the same underlying word and `operator[]` returns a
proxy rather than a `bool&`. Consequently the normal guarantee that distinct elements may
be modified concurrently does *not* hold: writing two different bits can race because they
share a word. Treat it as a single shared object for synchronization purposes.

#### Q160

**How would you make a std::vector" safe for concurrent access?**

Wrap it with external synchronization: guard all accesses with a `std::mutex` (or a
`std::shared_mutex` for many readers / rare writers), ensuring the lock covers compound
operations like check-then-use. For append-heavy patterns, consider reserving capacity up
front to limit reallocation, or switch to a purpose-built concurrent structure. The key is
that every access, including reads that overlap a write, goes through the same lock.

#### Q161

**How would you implement a bounded blocking queue?**

Use a fixed-capacity buffer guarded by a mutex and two condition variables (one for "not
full", one for "not empty"):

```cpp
template <class T>
class BoundedQueue {
    std::mutex m_;
    std::condition_variable not_full_, not_empty_;
    std::queue<T> q_;
    std::size_t cap_;
public:
    explicit BoundedQueue(std::size_t cap) : cap_(cap) {}
    void push(T v) {
        std::unique_lock lk(m_);
        not_full_.wait(lk, [&]{ return q_.size() < cap_; });
        q_.push(std::move(v));
        not_empty_.notify_one();
    }

    T pop() {
        std::unique_lock lk(m_);
        not_empty_.wait(lk, [&]{ return !q_.empty(); });
        T v = std::move(q_.front());
        q_.pop();
        not_full_.notify_one();
        return v;
    }
};
```

Producers block when full, consumers block when empty; each predicate guards against
spurious wakeups, and a real design adds a stop flag to both predicates for shutdown.

#### Q162

**What is the difference between a blocking queue and a lock-free queue?**

A blocking queue uses a mutex and condition variables: threads sleep when they cannot
proceed, which is simple and CPU-efficient under light load but can suffer contention,
priority inversion, and convoying. A lock-free queue uses atomic operations (CAS) so that
at least one thread always makes progress without mutexes, avoiding blocking and sleeping,
but it is far harder to implement correctly (memory reclamation, ABA) and does not
guarantee lower latency or starvation-freedom. Blocking is the right default; lock-free is
for specialized high-contention or latency-sensitive paths.

#### Q163

**What is a single-producer/single-consumer queue?**

An SPSC queue is designed for exactly one producer thread and one consumer thread. Because
only one thread ever writes the tail and only one ever writes the head, you can implement
it as a ring buffer with two atomics and no mutex or CAS, using only acquire/release
ordering. It is the simplest and fastest concurrent queue.

#### Q164

**How does an SPSC queue differ from an MPSC or MPMC queue?**

In SPSC each index has a single writer, so plain atomic loads/stores with acquire-release
suffice and no atomic read-modify-write is needed. MPSC has multiple producers contending
on the tail, so producers need CAS or fetch-add to claim slots, while the single consumer
stays simple. MPMC has contention on both ends, requiring atomic RMW and careful
sequencing (e.g. per-slot sequence numbers as in the Vyukov queue) on both head and tail,
making it the most complex.

#### Q165

**Why is an SPSC queue generally easier to implement?**

With one producer and one consumer, each shared index (head, tail) has exactly one writer,
eliminating the need for compare-and-exchange or multi-writer contention handling.
Synchronization reduces to publishing data with a release store on one side and observing
it with an acquire load on the other. There is no ABA problem on the indices and no
contention loop, so correctness reasoning is local and straightforward.

#### Q166

**What synchronization is needed for a multi-producer queue?**

Multiple producers must atomically claim distinct slots, so you need an atomic
read-modify-write: a `fetch_add` on a monotonically increasing tail or a
`compare_exchange` loop to reserve a position. After claiming a slot a producer must
publish its element with a release operation (often via a per-slot sequence/ready flag) so
the consumer, using an acquire load, only reads fully-written slots. Simply CAS-ing the
tail is not enough; you must also coordinate when a slot's data is visible.

#### Q167

**How can a ring buffer avoid dynamic allocation during normal operation?**

A ring buffer preallocates a fixed-size contiguous array once (at construction) and reuses
the slots as head and tail advance and wrap around. Enqueue and dequeue only read/write
existing storage and update indices, so no allocation or deallocation happens on the hot
path. This gives predictable latency and cache locality, at the cost of a fixed capacity
and a full-queue policy decision.

#### Q168

**What does the head index represent in a ring buffer?**

Conventionally the head (or read index) marks the position of the next element to be
*consumed* (dequeued). The consumer reads from head and then advances it. (Naming
conventions vary; some codebases swap head/tail, so the important invariant is that one
index marks the next-to-read and the other the next-to-write.)

#### Q169

**What does the tail index represent?**

The tail (or write index) marks the position where the next element will be *produced*
(enqueued). The producer writes at tail and then advances it. The occupancy of the queue
is derived from the relationship between tail and head.

#### Q170

**How do you distinguish a full ring buffer from an empty ring buffer?**

The ambiguity is that `head == tail` can mean either empty or full. Two standard
solutions:

```cpp
// (a) sacrifice one slot: capacity is N, usable is N-1
bool empty() const { return head == tail; }
bool full()  const { return (tail + 1) % N == head; }

// (b) monotonically increasing unsigned counters (never wrapped into range)
bool empty() const { return head == tail; }
bool full()  const { return tail - head == N;}   // size - tail - head
size_t size() const { return tail - head; }
```

Option (b) uses free-running unsigned indices and masks only when addressing the array
(`idx & (N-1)` for power-of-two N); `tail - head` gives the exact count and relies on
well-defined unsigned wraparound. A separate count variable is a third option but adds a
shared field both sides update.

#### Q171

**How can wraparound of indexes be handled safely?**

With a power-of-two capacity, keep free-running unsigned indices and compute the physical
slot with a bitwise mask (`index & (capacity - 1)`); this is branch-free and correct
across the unsigned wrap of the index itself. Unsigned integer overflow is well-defined
(modular), so `tail - head` still yields the correct occupancy as long as capacity never
exceeds the index type's range. Avoid signed indices (overflow is UB) and avoid `%` with
non-power-of-two sizes on the hot path for performance.

#### Q172

**Why should a ring buffer use unsigned indexes carefully?**

Unsigned arithmetic wraps modulo 2^n, which is exactly what you want for free-running
counters, and `tail - head` remains correct across a wrap. But you must ensure the
capacity is less than the modulus so the difference never becomes ambiguous, and be
careful with comparisons: an underflow (e.g. `head - tail` when you meant `tail - head`)
yields a huge positive value rather than a negative, producing silent logic bugs. Signed
indices, by contrast, invoke UB on overflow, so unsigned is correct but demands
disciplined difference/comparison logic.

#### Q173

**How can false sharing affect ring-buffer head and tail indexes?**

If head and tail sit on the same cache line, the producer's store to tail invalidates the
line in the consumer's cache and vice versa, so every enqueue/dequeue triggers coherence
traffic even though the two sides touch different indices. Placing head and tail on
separate cache lines (`alignas(hardware_destructive_interference_size)`), and caching the
other side's index locally, eliminates this ping-pong and is the single biggest
performance win for SPSC ring buffers.

#### Q174

**What is backpressure in a high-throughput queue?**

Backpressure is the feedback that slows or halts producers when consumers cannot keep up,
bounding the queue depth and preventing unbounded memory growth and latency explosion. In
a bounded queue it manifests as producers blocking, failing, or being throttled when the
queue is full. It aligns producer throughput with consumer capacity and keeps the system
stable rather than collapsing under overload.

#### Q175

**Should a full queue block, drop, overwrite, or reject new data?**

It depends on the data's semantics. *Block* when no item may be lost and producers can
wait (reliable work pipelines). *Reject* (fail fast) when callers can shed or retry and
you must not stall producers. *Drop* (oldest or newest) or *overwrite* when only fresh
data matters and staleness is worse than loss (sensor samples, live telemetry, video
frames). The decision trades completeness against latency and liveness.

#### Q176

**How would you select a queue policy for a packet-forwarding system?**

For packet forwarding, latency and freshness usually outweigh perfect delivery (upper
layers often retransmit), so under overload dropping packets (tail-drop or a RED-style
early drop) is typically preferable to blocking, since blocking a forwarding thread stalls
all downstream traffic and inflates latency for everyone. Use a bounded lock-free (or SPSC
per-core) ring sized for burst tolerance, cache-line-separated indices, and a drop policy
with counters for observability. Blocking/backpressure may be used selectively toward
trusted internal producers, but on the fast path you generally drop and measure rather
than stall.

## Lock-free programming

#### Q177

**What does lock-free programming mean?**

Lock-free means the algorithm guarantees *system-wide* progress: at any point, at least
one thread will complete its operation in a bounded number of steps, regardless of delays,
preemption, or failure of other threads. It is defined by this progress property, not by
the mere absence of the word "mutex." Crucially, individual threads can still starve;
lock-free guarantees the system as a whole advances, not that every thread does.

#### Q178

**What are the main difficulties of writing lock-free code?**

The hardest problems are safe memory reclamation (you cannot free a node while another
thread may still dereference it), the ABA problem, and getting memory ordering exactly
right so publication and consumption are correctly synchronized. CAS-retry loops must
handle contention and interleavings you cannot easily enumerate, and bugs are rare,
timing-dependent, and nearly impossible to reproduce. Testing is weak evidence, so
correctness usually needs formal reasoning or model checking.

#### Q179

**What is memory reclamation?**

Memory reclamation is the problem of determining when it is safe to free (or reuse) a node
that has been logically removed from a lock-free data structure. Because other threads may
hold or be about to load a pointer to that node without any lock, you cannot simply
`delete` it on removal. Reclamation schemes (hazard pointers, epochs, RCU, reference
counting) defer freeing until no thread can still access the node.

#### Q180

**Why is deleting a node immediately unsafe in a lock-free data structure?**

Another thread may have already read a pointer to the node (or be partway through an
operation that will dereference it) before the removing thread frees it. If you `delete`
immediately, that other thread dereferences freed memory, a use-after-free and undefined
behavior, and in a reuse scenario it also enables the ABA problem. Safe removal therefore
requires deferring reclamation until all concurrent readers have provably finished with
the node.

#### Q181

**What is hazard-pointer reclamation?**

Hazard pointers let each thread publish, in a per-thread single-writer slot, the pointer
it is currently about to dereference. Before freeing a retired node, the reclaiming thread
scans all threads' hazard pointers; if the node is listed by any thread, freeing is
deferred, otherwise it is safe to delete. This bounds the number of unreclaimed nodes and
is robust to stalled threads. C++26 standardizes this as `std::hazard_pointer`.

#### Q182

**What is epoch-based reclamation?**

Epoch-based reclamation (EBR) maintains a global epoch counter; threads announce the epoch
they entered when starting an operation and place retired nodes on a per-epoch limbo list.
A node can be freed once all active threads have advanced past the epoch in which it was
retired, guaranteeing no thread can still hold a reference. EBR has very low per-operation
overhead but a stalled thread stuck in an old epoch can stall reclamation and let memory
grow unboundedly.

#### Q183

**What is read-copy-update, or RCU?**

RCU is a reclamation/synchronization technique optimized for read-mostly data: readers
access the structure with essentially no synchronization inside a "read-side critical
section," while a writer makes a modified copy, atomically publishes it, and defers
freeing the old version until a *grace period* has elapsed, i.e. all pre-existing readers
have finished. This gives extremely cheap, wait-free reads at the cost of more expensive,
deferred updates. C++26 adds `std::rcu`.

#### Q184

**What is deferred reclamation?**

Deferred reclamation is the general strategy of not freeing a removed node at the moment
of removal, but instead retiring it and reclaiming it later once it is provably
unreachable by any concurrent thread. Hazard pointers, epochs, RCU, and reference counting
are all concrete deferred-reclamation schemes. The deferral window trades a bounded amount
of extra memory for safety against use-after-free.

#### Q185

**How does the ABA problem affect a lock-free stack?**

In a CAS-based Treiber stack, a thread reads the head pointer A, gets preempted, and
meanwhile other threads pop A, push other nodes, and push a *new node that happens to
reuse A's address*; the head is A again. The preempted thread's `compare_exchange` on head
still sees A and succeeds, even though the stack's state changed underneath it, corrupting
the structure (e.g. linking to a freed/wrong `next`). The pointer value matching does not
imply the state is unchanged.

#### Q186

**How can tagged pointers help with lock-free structures?**

A tagged pointer packs a monotonically increasing version counter alongside the pointer
(in unused low bits of an aligned pointer, or in a double-width CAS). Each successful
modification bumps the tag, so even if the pointer value repeats, the tag differs and a
stale CAS fails, defeating ABA. This turns the compare-and-exchange into comparing
(pointer, version) as a unit.

#### Q187

**What is pointer alignment, and why can it matter for tagged pointers?**

Alignment is the requirement that an object's address be a multiple of some power of two;
heap/aligned allocations of objects with alignment N guarantee the low `log2(N)` bits of
their addresses are zero. Those guaranteed-zero low bits can be repurposed to store a
small tag/version, letting you pack pointer + tag into a single word for a single-word
CAS. This only works because alignment makes those bits reliably unused; misaligned or
under-aligned objects would corrupt the pointer when you mask the tag back off.

#### Q188

**Why can memory reclamation dominate lock-free data-structure performance?**

The core operations (CAS on a pointer) are cheap, but safe reclamation adds per-access
bookkeeping: publishing hazard pointers, scanning other threads' hazard arrays,
maintaining epoch/limbo lists, or reference counting, often with fences on the hot path.
Hazard-pointer scans are O(threads x retired), and reclamation can touch many remote cache
lines, so under high thread counts the reclamation machinery can cost more than the
structural operation itself. This is why "lock-free" structures are frequently bounded by
their reclamation scheme, not their CAS.

#### Q189

**How would you test a lock-free queue?**

Combine stress testing (many producer/consumer threads, randomized timing, long duration)
with invariant checks: every enqueued item is dequeued exactly once, no duplicates, no
losses, FIFO order where promised. Run under ThreadSanitizer to catch data races and under
AddressSanitizer for use-after-free in reclamation. Use model checkers / exhaustive
interleaving tools (CDSChecker, Relacy, loom-style) to explore orderings, and vary CPU
count, affinity, and inject delays to surface ABA and ordering bugs. Remember that passing
tests is weak evidence; reasoning matters.

#### Q190

**How would you prove that a lock-free algorithm is correct?**

Establish two things: *safety* (linearizability, usually by identifying a linearization
point for each operation and showing the concurrent history is equivalent to a legal
sequential one) and *liveness* (the lock-free progress property, that some operation
always completes in bounded steps). You verify memory-ordering correctness against the C++
memory model's happens-before relations, and typically back the hand proof with a model
checker that exhaustively explores interleavings and relaxed-memory executions.

#### Q191

**What is linearizability?**

Linearizability is a correctness condition stating that every concurrent operation appears
to take effect instantaneously at some single moment between its invocation and its
return, and that this ordering is consistent with a legal sequential execution of the
object and respects real-time order (if operation A returns before B begins, A is ordered
before B). In other words, the concurrent history is equivalent to some sequential history
where each operation is atomic. It is the standard correctness criterion for concurrent
objects and composes.

#### Q192

**What is the linearization point of an operation?**

The linearization point is the single instant during an operation's execution at which it
logically takes effect, the moment that fixes its position in the equivalent sequential
order. For a CAS-based push, it is typically the successful `compare_exchange` that swings
the head; for a dequeue, the successful CAS that detaches the node. Identifying a valid
linearization point for every operation is the usual way to prove linearizability.

#### Q193

**What is progress compared with correctness?**

Correctness (safety) is about the results being right, e.g. linearizability: operations
behave as if executed atomically in a legal order. Progress (liveness) is about operations
actually *completing*, characterized by guarantees like wait-free, lock-free, or
obstruction-free. They are orthogonal: an algorithm can be correct but deadlock (no
progress), or make progress but produce wrong results. A good lock-free design must
establish both.

#### Q194

**Why should lock-free code usually be isolated behind a small, well-tested interface?**

Lock-free code is extraordinarily hard to get right (memory ordering, ABA, reclamation)
and its bugs are rare and non-reproducible, so you want to confine that risk to a tiny,
auditable surface rather than spread subtle atomics throughout the codebase. A narrow
interface (push/pop, enqueue/dequeue) lets you exhaustively test and model-check that
unit, swap the implementation (even for a mutex-based one) without touching callers, and
keep the dangerous reasoning in one place. Encapsulation turns a global hazard into a
single reviewable component.

## C++20 and C++23 synchronization

#### Q195

**What is `std::counting_semaphore`?**

A `std::counting_semaphore<LeastMaxValue>` holds an internal non-negative counter
representing a number of available permits. `acquire()` decrements it, blocking while the
count is zero; `release(n)` increments it by `n` and wakes waiters. Unlike a mutex it has
no notion of an owner, so the thread that releases a permit need not be the one that
acquired it, which makes it suitable for signalling and for limiting concurrency to N
resources. `try_acquire`, `try_acquire_for`, and `try_acquire_until` give non-blocking and
timed variants.

#### Q196

**What is the difference between a binary semaphore and a mutex?**

A mutex has strict ownership: only the thread that locked it may unlock it, and it is
designed for mutual exclusion of a critical section. A binary semaphore is just a counting
semaphore with a maximum of one and has no owner, so one thread can acquire and a
different thread can release it, which makes it a signalling primitive rather than a lock.
Using a binary semaphore as a lock loses the ownership checks and
recursion/priority-inheritance support a real mutex may provide, so prefer a mutex when
you mean mutual exclusion.

#### Q197

**What is `std::binary_semaphore`?**

It is an alias for `std::counting_semaphore<1>`, a semaphore whose counter is at most one.
It is the idiomatic C++20 primitive for one-shot or repeated thread-to-thread signalling,
such as a producer releasing to wake a single consumer. Because it is ownerless, the
acquire and release can happen on different threads, unlike a mutex.

#### Q198

**When would a semaphore be preferable to a condition variable?**

Prefer a semaphore when the thing you are tracking is a count of permits or available
units and there is no complex predicate over shared state, for example limiting
concurrency to N or a simple producer-wakes-consumer handoff. The semaphore needs no
external mutex and cannot suffer a lost wakeup, since `release` persists the permit even
if no one is currently waiting. Use a condition variable when waiters must evaluate an
arbitrary predicate over mutex-protected state, or when you need `notify_all` broadcast
semantics.

#### Q199

**What is `std::latch`?**

`std::latch` is a single-use downward counter initialized to a count. Threads call
`count_down()` (optionally by more than one) and `wait()` blocks until the counter reaches
zero; `arrive_and_wait()` combines the two, and `try_wait()` tests without blocking. It is
ideal for fan-out/fan-in or a one-time "all workers ready" gate. Once it reaches zero it
cannot be reset or reused.

#### Q200

**How is a latch different from a barrier?**

A latch is single-use: it counts down to zero once and is then exhausted. A barrier is
reusable across multiple phases; after all participating threads arrive, the barrier
resets for the next phase and can optionally run a completion function once per phase. Use
a latch for a one-time synchronization point and a barrier for repeated loop-step
synchronization among a fixed group of threads.

#### Q201

**What is `std::barrier`?**

`std::barrier` is a reusable synchronization point for a fixed number of participating
threads across repeated phases. Each thread calls `arrive_and_wait()` (or `arrive()` then
`wait()`); when the expected count of arrivals is reached, the barrier runs the optional
completion function exactly once and then releases all waiters and advances to the next
phase. `arrive_and_drop()` lets a thread leave the participant set, decrementing the
expected count for subsequent phases.

#### Q202

**Can a barrier be reused?**

Yes, that is the defining difference from a latch. After every expected participant
arrives for a phase, the barrier automatically resets its counter and begins a new phase,
so the same object synchronizes successive iterations of a loop. The participant count
stays fixed unless threads call `arrive_and_drop()`.

#### Q203

**What is a barrier completion function?**

It is a callable supplied to the barrier constructor that the library invokes exactly once
per phase, on one (unspecified) participating thread, after all threads have arrived but
before any are released. It runs while all participants are blocked, so it is the safe
place to perform the serial, between-phase work such as aggregating results or swapping
buffers without additional locking. It must not throw (a throw calls `std::terminate`).

#### Q204

**What is `std::atomic_wait()`?**

`atomic.wait(old)` (and the free function `std::atomic_wait`) blocks the calling thread
while the atomic's value still equals `old`, waking when the value changes and a notify is
issued. It gives you an efficient block-until-changed primitive directly on an atomic
without a separate mutex and condition variable. It still must be used in a loop because,
like a condition variable, it may return spuriously and the value may have changed and
changed back.

#### Q205

**How does `atomic_wait()` differ from repeatedly polling an atomic?**

A spin/poll loop keeps the CPU busy burning cycles and energy and competing for the cache
line, whereas `atomic_wait()` lets the implementation block the thread (typically via a
futex-like OS wait) so the core is free for other work. A corresponding
`notify_one`/`notify_all` wakes the waiter when the value changes, avoiding wasted
polling. Polling can still win for very short, predictable waits where the wake-up latency
of blocking dominates.

#### Q206

**What are `atomic_notify_one()` and `atomic_notify_all()`?**

They are the wake side of the C++20 atomic wait/notify protocol, available as member
functions `atomic.notify_one()`/`notify_all()` and as free functions
`std::atomic_notify_one`/`std::atomic_notify_all`. After a thread modifies the atomic it
calls notify to wake one or all threads blocked in `wait()` on that object. `notify_one`
releases a single waiter, `notify_all` releases every waiter; a notify with no waiters is
simply a no-op.

#### Q207

**When can atomic waiting be more efficient than a condition variable?**

When the shared state is a single atomic and no additional mutex-protected invariant is
involved, `atomic.wait/notify` avoids the mutex acquire/release that a condition variable
requires on both the wait and the signal path, reducing overhead and contention. It is a
natural fit for a one-word flag or counter where the predicate is simply "value changed."
A condition variable remains preferable when waiters must evaluate a predicate over
several variables that need a lock held consistently.

#### Q208

**What are the synchronization risks when mixing atomic waits and ordinary mutexes?**

If a predicate depends both on an atomic (waited via `atomic_wait`) and on other state
guarded by a mutex, you can get lost wakeups or torn views: the notifier might change the
mutex-protected data and the atomic in an order that lets a waiter miss the signal or read
an inconsistent combination. The atomic wait only synchronizes on the atomic's value, not
on the mutex, so there is no single consistent critical section covering the whole
predicate. The safe rule is to keep all state behind one synchronization mechanism; if the
invariant spans multiple variables, use a mutex plus condition variable rather than
splitting it across an atomic wait and a lock.

#### Q209

**How can `std::stop_token` be used to cancel blocking work?**

A `std::stop_token` (from a `std::jthread` or a `std::stop_source`) lets a worker check
`stop_requested()` in its loop and return early. For blocking waits,
`std::condition_variable_any::wait` has overloads taking a `stop_token` and predicate that
return when a stop is requested, and you can register a `std::stop_callback` to wake a
custom wait (for example to call `notify_all`, close a socket, or release a semaphore).
This turns an otherwise indefinite block into cooperative, interruptible cancellation
without forcibly killing the thread.

## Coroutines and concurrency

#### Q210

**Are C++ coroutines automatically concurrent?**

No. A C++ coroutine is just a function that can suspend and resume; by itself it runs on
whatever thread calls `resume()` on its handle, so a program using only coroutines on one
thread is purely sequential. Concurrency comes from an executor/scheduler that resumes
coroutines on a thread pool or integrates them with asynchronous I/O. Coroutines are a
control-flow mechanism, not a threading mechanism.

#### Q211

**What is the difference between a coroutine and a thread?**

A thread is an OS-scheduled unit of execution with its own stack, preemptively scheduled
by the kernel and able to run truly in parallel on another core. A coroutine is
cooperative: it has a heap-allocated frame rather than a full stack, suspends only at
explicit `co_await`/`co_yield`/`co_return` points, and resumes on whichever thread drives
it. Coroutines are far cheaper to create and switch than threads but give no parallelism
on their own; you combine many coroutines with a small number of threads.

#### Q212

**How can a coroutine be suspended and resumed?**

A coroutine suspends at a `co_await`, `co_yield`, or `co_return`, at which point its state
is saved in the coroutine frame and control returns to the caller/resumer. The awaiter's
`await_ready`/`await_suspend`/`await_resume` protocol decides whether to suspend and what
to do with the `std::coroutine_handle`; later something calls `handle.resume()` (or
`handle()`), which continues execution right after the suspension point.
`handle.destroy()` frees the frame when the coroutine is finished or abandoned.

#### Q213

**What is an executor or scheduler?**

It is the component that decides where and when coroutines (or tasks) run, typically
owning one or more threads and a work queue onto which ready coroutine handles are posted
and then resumed. `co_await`-ing a scheduler is the idiom for "reschedule my continuation
onto this executor," which is how work is moved onto a thread pool or an I/O thread. The
executor is what actually turns suspendable coroutines into a concurrent system.

#### Q214

**How can coroutines be integrated with asynchronous I/O?**

An awaitable I/O operation starts the async request in `await_suspend`, stores the
coroutine handle, and returns without blocking; when the OS signals completion
(epoll/io_uring/IOCP/completion handler), the I/O reactor resumes the stored handle, and
`await_resume` yields the result. This lets code read like straight-line
`co_await socket.read()` while the underlying thread is never blocked and is free to drive
other coroutines. The key requirement is that the handle is resumed exactly once by the
I/O completion path.

#### Q215

**What lifetime problems can occur when a coroutine captures references?**

Because a coroutine suspends and may resume much later, any reference or pointer it holds,
including captured lambda references, `this`, and reference parameters, must outlive every
resumption, not just the initial call. A common bug is a coroutine capturing a local or a
temporary by reference; the referent is destroyed when the enclosing scope exits while the
coroutine is suspended, and the later resume touches a dangling reference (undefined
behavior). Prefer capturing by value / taking parameters by value so the data lives inside
the coroutine frame.

#### Q216

**Why does a coroutine frame require careful ownership management?**

The frame is heap-allocated and holds the coroutine's state, locals, and promise; it is
only freed when something calls `handle.destroy()` (or final suspension transfers
ownership appropriately). If no owner destroys it you leak the frame; if two owners
destroy it you get a double free, and resuming or destroying a completed/already-destroyed
handle is undefined behavior. RAII wrapper types (a task/awaitable that owns the handle
and destroys it in its destructor, with clear move semantics) are used to make exactly-one
ownership safe.

#### Q217

**How can cancellation be propagated through an asynchronous coroutine call chain?**

Thread a cancellation signal (such as a `std::stop_token` or a cancellation state carried
in the coroutine's promise/awaitable) down the chain so each `co_await` point can observe
a stop request and unwind, completing with a cancelled result rather than its normal
value. Awaiters register a stop callback so that an in-flight async operation (socket
read, timer) is aborted and its handle resumed promptly when cancellation fires. Each
level then propagates the cancelled outcome upward, running destructors/cleanup as the
coroutines complete.

#### Q218

**How do coroutines compare with a thread pool for high-concurrency workloads?**

For I/O-bound workloads with huge numbers of concurrent operations, coroutines scale far
better: a suspended coroutine is a small heap frame rather than a blocked thread holding a
full stack, so you can have millions outstanding while a few pool threads drive them,
avoiding thread-per-connection overhead and context-switch storms. A plain thread pool is
simpler and fine for CPU-bound tasks, but blocking calls tie up a whole worker and limit
concurrency to the pool size. In practice you combine them: coroutines for structured
async flow, running on a thread pool executor that supplies the actual parallelism.

## Code-based questions

#### C1

**Question 1: data race**

```cpp
int counter = 0;

void worker() {
    ++counter;
}
```

`++counter` is a read-modify-write on a plain `int`; when several threads run `worker()`
concurrently these accesses are unsynchronized, which is a data race and therefore
undefined behavior, not merely a lost-update problem. The fix is to make the operation
atomic (or guard it with a mutex). An atomic counter is the cheaper choice:

```cpp
#include <atomic>

std::atomic<int> counter{0};

void worker() {
    counter.fetch_add(1, std::memory_order_relaxed);
}
```

`memory_order_relaxed` is sufficient here because we only need atomicity of the increment,
not ordering relative to other data. A `std::mutex` with `std::lock_guard` around
`++counter` would also be correct but heavier.

#### C2

**Question 2: missing synchronization**

```cpp
bool ready = false;
int value = 0;

void producer() {
    value = 42;
    ready = true;
}

void consumer() {
    while (!ready) {
    }

    use(value);
}
```

Both `ready` and `value` are ordinary non-atomic variables accessed concurrently, so this
is a data race and undefined behavior. Even on hardware that "looks" fine there are three
distinct problems: visibility (the consumer may never observe `ready` becoming true
because the compiler can hoist the load out of the loop), ordering (the compiler or CPU
may reorder the two producer stores, so `ready` becomes visible before `value`), and the
race itself. The fix is to make `ready` atomic and pair a release store with an acquire
load so that `value = 42` is guaranteed visible once `ready` reads true:

```cpp
#include <atomic>

void use(int);

std::atomic<bool> ready{false};
int value = 0;   // now safely published through the release/acquire

void producer() {
    value = 42;
    ready.store(true, std::memory_order_release);
}

void consumer () {
    while (!ready.load(std::memory_order_acquire)) {

    }
    Use(value);
}
```

The release store "publishes" everything sequenced before it, and the acquire load
"receives" it, establishing happens-before so the non-atomic `value` is safe to read. A
condition variable (mutex + `cv.wait(lock, [&]{ return ready; })`) is the alternative when
the consumer should block instead of spin.

#### C3

**Question 3: deadlock**

```cpp
std::mutex first;
std::mutex second;

void task_a() {
    std::lock_guard lock1(first);
    std::lock_guard lock2(second);
}

void task_b() {
    std::lock_guard lock1(second);
    std::lock_guard lock2(first);
}
```

This is a classic AB-BA deadlock: `task_a` locks `first` then `second` while `task_b`
locks `second` then `first`, so they can each hold one mutex and wait forever for the
other. The fix is a consistent global lock order: every thread must acquire the mutexes in
the same order.

```cpp
void task_a() {
    std::lock_guard lockd(first);
    std::lock_guard lock2(second);
}
void task_b() {
    std::lock_guard lock(first);
    std::lock_guard lock2(second);    //same order as task a
}
```

Equivalently, acquire both at once with `std::scoped_lock(first, second)`, which applies a
deadlock-avoidance algorithm (see C4).

#### C4

**Question 4: safe locking of multiple mutexes**

```cpp
std::scoped_lock lock(first, second);
```

`std::scoped_lock` with multiple mutexes locks them all using a deadlock-avoidance
algorithm equivalent to `std::lock`, which uses a try-and-back-off strategy so the
acquisition order you write does not matter. Both `task_a` and `task_b` can therefore
write `std::scoped_lock lock(first, second)` and `std::scoped_lock lock(second, first)`
respectively and still never deadlock, because the primitive, not the source order,
decides how the locks are taken. It is also RAII, unlocking all mutexes on scope exit, and
is the modern replacement for manually calling `std::lock` followed by adopting lock
guards.

```cpp
void task_a() { std::scoped_lock lock(first, second); }
void task_b() { std::scoped_lock lock(first, second); }
```

#### C5

**Question 5: condition-variable bug**

```cpp
std::mutex mutex;
std::condition_variable cv;
bool ready = false;

void consumer() {
    std::unique_lock lock(mutex);
    cv.wait(lock);

    if (ready) {
        consume();
    }
}
```

There are two defects. First, `cv.wait(lock)` has no predicate, so it is vulnerable to a
spurious wakeup (the standard explicitly permits `wait` to return without a notification)
and the code would then proceed with `ready` still false. Second, it has a lost-wakeup
race: if the producer sets `ready` and calls `notify_one` before the consumer reaches
`wait`, the notification is missed and the consumer blocks forever. Both are fixed by the
predicate overload, which re-checks the condition in a loop and also handles the
already-ready case before ever waiting:

```cpp
void consumer() {
    std::unique_lock lock(mutex);
    cv.wait(lock, [&]{ return ready; });
    consume();
}
```

#### C6

**Question 6: lifetime bug**

```cpp
std::thread create_worker() {
    int value = 10;

    return std::thread([&] {
        use(value);
    });
}
```

The lambda captures the local `value` by reference, but `value` is destroyed when
`create_worker` returns, while the new thread may not have started yet. When the thread
later runs `use(value)` it reads a reference to a destroyed stack variable, which is a
dangling reference and undefined behavior. The fix is to capture by value (or otherwise
give the worker its own copy / shared owned storage):

```cpp
std::thread create_worker() {
    int value = 10;
    return std::thread([value] {   // own copy lives in thread
        use(value);
    });
}
```

#### C7

**Question 7: relaxed atomic**

```cpp
std::atomic<int> counter{0};

void worker() {
    counter.fetch_add(1, std::memory_order_relaxed);
}
```

For a pure counter this is sufficient: `fetch_add` is an atomic read-modify-write, so with
any number of threads using `relaxed` no increment is lost and the final total is exact;
relaxed still guarantees atomicity, it only drops ordering with respect to other
variables. Reading the final value is safe after you have joined the threads (the join
provides the happens-before), and even an inter-thread read is fine as long as you do not
rely on the counter to order other data. It is *not* sufficient for publishing related
non-atomic data: relaxed establishes no happens-before, so another thread observing the
new count has no guarantee of seeing the associated writes. For publishing you need
release on the store and acquire on the load (or `acq_rel`).

#### C8

**Question 8: vector and concurrent access**

```cpp
std::vector<int> values;

void producer() {
    values.push_back(1);
}

void consumer() {
    if (!values.empty()) {
        process(values.front());
    }
}
```

`std::vector` is not thread-safe for concurrent write/read: `push_back` can reallocate and
move the buffer, so a concurrent `empty()`/`front()` can read a stale or freed pointer, a
data race and undefined behavior. The whole producer operation and the whole consumer
check-then-use must be protected by the *same* mutex, and the empty-check and the
`front()` read must be inside one critical section so the element cannot disappear between
them:

```cpp
#include <mutex>
#include <vector>

void process(int);

std::mutex m;
std::vector<int> values;

void producer() {
    std::lock_guard lock(m);
    values.push_back(1);
}

void consumer() {
    std::lock_guard lock(m);
    if (!values.empty()) {
        process(values.front());
    }
}
```

If `process` is expensive, copy the element out under the lock and release the mutex
before calling `process`.

#### C9

**Question 9: condition-variable shutdown**

The worker loops waiting on the predicate `stopped || !queue.empty()`, drains all
remaining work, and exits only when `stopped && queue.empty()`. Shutdown sets the flag
under the lock and broadcasts so every blocked worker re-evaluates the predicate.

```cpp
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>

std::mutex m;
std::condition_variable cv;
std::queue<std::function<void()>> queue;
bool stopped = false;

void worker() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock lock(m);
            cv.wait(lock, [&]{ return stopped || !queue.empty(); });
            if (stopped && queue.empty()) {
                return;                       // only exit once fully drained
            }
            task = std::move(queue.front());
            queue.pop();
        }
        task();                               // run work outside the lock
    }
}

void shutdown() {
    {
        std::lock_guard lock(m);
        stopped = true;
    }
    cv.notify_all();                          // wake all workers to re-check
}
```

Running the task outside the lock keeps the critical section small; using `notify_all` on
shutdown ensures no worker is left blocked.

#### C10

**Question 10: atomic compare-and-exchange**

```cpp
#include <atomic>

int atomic_increment(std::atomic<int>& value) {
    int expected = value.load(std::memory_order_relaxed);
    while (!value.compare_exchange_weak(
              expected,              // expected: current snapshot
              expected + 1,          // desired: snapshot + 1
              std::memory_order_aca_rel,   // success ordering
              std::memory_order_relaxed)) {// failure ordering
        // On failure, compare_exchange_weak reloads `expected`
        // with the actual current value, so we retry with a fresh snapshot.
    }
    return expected + 1;
}
```

The *expected* value is a snapshot of the current contents; the *desired/update* value is
that snapshot plus one. We use the *weak* form because it may fail spuriously (report a
mismatch even when the values matched, cheaper on LL/SC architectures), which is harmless
inside a retry loop; the *strong* form is preferred only when there is no loop and a
spurious failure would be costly. The *failure* memory order must be no stronger than the
success order and must not be a release order (a failed CAS performs no store to release);
`relaxed` on failure is correct because the retry simply re-reads, while `acq_rel` on
success both acquires the prior writes and releases this update. For a plain counter
`fetch_add(1)` is simpler and faster; the CAS loop is the pattern when the new value is a
function of the old that no single RMW instruction provides.

## Design and discussion questions

#### Q219

**Design a thread-safe producer-consumer queue.**

- Internal `std::queue`/`std::deque` guarded by one `std::mutex`, plus a
  `std::condition_variable` for "not empty" (and a second for "not full" if bounded).
- `push`: lock, (if bounded) wait on not-full, enqueue, unlock, `notify_one` on not-empty.
- `pop`: lock, `cv.wait(lock, [&]{ return stopped || !q.empty(); })`, move the front out,
  unlock, return (use `std::optional`/status to signal shutdown).
- Shutdown: a `stopped` flag set under the lock plus `notify_all`, with pop returning
  empty once drained.
- Trade-off: single-mutex design is simple and correct; it becomes the contention
  bottleneck under high load, at which point consider sharding or a lock-free queue.

#### Q220

**Design an SPSC ring buffer for fixed-size packets.**

- Fixed-capacity array of packet slots; one `head` (consumer) and one `tail` (producer)
  atomic index.
- Exactly one producer and one consumer means no CAS is needed: producer writes the slot
  then `tail.store(release)`; consumer `tail.load(acquire)`, reads the slot, then
  `head.store(release)`.
- Full/empty distinguished by keeping one slot open or using monotonically increasing
  indices masked to capacity (power-of-two size for cheap masking).
- Put `head` and `tail` on separate cache lines (`alignas(64)`) to avoid false sharing
  between the two threads.
- Trade-off: extremely fast and allocation-free, but strictly one producer and one
  consumer; any more participants need a different design.

#### Q221

**Design an MPMC queue for a worker pool.**

- Simplest correct option: bounded buffer + mutex + two condition variables (not-full,
  not-empty), which trivially handles many producers and consumers.
- Scalable option: a lock-free bounded MPMC ring (Vyukov-style) with a per-slot sequence
  number and a CAS on a shared `enqueue`/`dequeue` ticket; each slot's sequence tells
  producers/consumers when it is their turn.
- Memory ordering: acquire on the slot sequence load, release on the store that publishes
  the slot.
- Cache-line-pad the enqueue and dequeue counters separately to cut false sharing.
- Trade-off: lock-free scales under contention but is much harder to get right and to
  reclaim/version correctly; start with the mutex version and only move to lock-free with
  benchmarks justifying it.

#### Q222

**Design a shutdown mechanism for a service with multiple worker threads.**

- A single atomic/flagged `stopped` state (or a `std::stop_source`) set once under the
  relevant lock.
- Wake every blocked worker with `notify_all` (or stop callbacks) so none waits forever.
- Define a drain policy: process remaining queued work before exit, or discard it; workers
  exit only when `stopped && queue.empty()` for graceful drain.
- `join()` all workers (use `std::jthread` so joining and stop-request happen
  automatically in the destructor).
- Make shutdown idempotent and ensure in-flight tasks finish or are cancelled cleanly;
  release external resources after all workers have joined.

#### Q223

**Design a per-thread statistics system that periodically aggregates counters.**

- Give each worker its own counter block, cache-line aligned (`alignas(64)`) to avoid
  false sharing; workers update their own block with cheap relaxed atomics or even plain
  writes if only the owner writes.
- Register each block in a global list (guarded once at thread start) so an aggregator can
  find them.
- An aggregator thread periodically reads all blocks and sums them; reads use
  acquire/relaxed and tolerate slightly stale values since exact instantaneous totals are
  rarely needed.
- Trade-off: near-zero contention on the hot path at the cost of approximate reads and
  extra memory; far better than one shared atomic that bounces a cache line between all
  threads.

#### Q224

**Design a concurrent cache with bounded memory.**

- Sharded map: partition keys across N shards each with its own mutex (or a reader-writer
  lock) to spread contention.
- Eviction policy (LRU/LFU/TTL) maintained per shard so bookkeeping stays local; a bounded
  size per shard caps total memory.
- Reads take the shard lock briefly (or a shared lock); writes/insertions update the
  recency structure and evict when over budget.
- Trade-off: sharding trades perfect global LRU accuracy for scalability; a reader-writer
  lock helps read-heavy loads but can starve writers, so choose based on the measured
  read/write ratio.

#### Q225

**How would you prevent a slow consumer from blocking all producers?**

- Use a bounded queue with a clear full-policy instead of unbounded buffering: block only
  that producer briefly, or drop/reject/overwrite depending on the domain.
- Give each consumer its own queue (sharding) so one slow consumer cannot stall producers
  feeding others.
- Apply backpressure signals so producers slow down rather than exhaust memory, or shed
  load by dropping lowest-priority items.
- For broadcast, decouple with per-subscriber bounded queues so a slow subscriber only
  drops its own messages.

#### Q226

**How would you handle overload in a high-throughput forwarding service?**

- Define the goal: for packet forwarding, bounded latency usually beats completeness, so
  prefer dropping over unbounded queuing.
- Use bounded queues with a drop/overwrite policy and admission control/rate limiting at
  ingress.
- Apply backpressure to upstream where the protocol allows; otherwise drop early and
  cheaply and record drop counters.
- Keep per-core sharding so overload on one flow does not degrade others; monitor queue
  depth and tail latency to trigger load shedding.

#### Q227

**How would you choose between blocking, dropping, batching, and backpressure?**

- Blocking: when no data may be lost and upstream can tolerate waiting (internal
  pipelines, bounded producers).
- Dropping/overwriting: when freshness matters more than completeness and latency must
  stay bounded (telemetry, media, packet forwarding).
- Batching: when per-item overhead dominates; amortize syscalls/locking at the cost of a
  little latency.
- Backpressure: when you can propagate slowness upstream to a controllable source (TCP
  flow control, credit-based schemes).
- The decision follows the correctness and latency contract of the system, not a universal
  default.

#### Q228

**How would you avoid sharing mutable state between worker threads?**

- Partition the data so each worker owns its slice (sharding by key/connection); workers
  touch only their own state.
- Use message passing / queues to hand off work and results instead of shared variables.
- Prefer immutable data and copy-on-write for data read by many threads.
- Keep per-thread accumulators and aggregate only at well-defined points. Less shared
  mutable state means fewer locks, less false sharing, and simpler reasoning.

#### Q229

**How would you test a concurrent data structure under heavy contention?**

- Stress tests with many more threads than cores, randomized operation mixes, and long run
  times to expose interleavings.
- Run under ThreadSanitizer to catch data races and under AddressSanitizer for memory
  errors; use Helgrind for lock-order issues.
- Verify correctness with invariants and a linearizability checker comparing observed
  histories against a sequential model.
- Inject randomized delays/yields to widen race windows, and pin/vary thread affinity;
  repeat across optimization levels and hardware.

#### Q230

**How would you detect rare timing-dependent bugs?**

- Increase the chance of hitting the window: randomized sleeps/`std::this_thread::yield`
  at suspected points, high thread counts, and long soak tests.
- Run ThreadSanitizer, which flags races even when the bad interleaving did not actually
  manifest.
- Use stress harnesses with seeded randomness so a failing schedule can be replayed;
  collect core/thread dumps on failure.
- Consider model checking / tools like `rr` for deterministic record-and-replay to
  reproduce the one-in-a-million schedule.

#### Q231

**Which tools can help detect data races and deadlocks?**

- ThreadSanitizer: dynamic data-race and some deadlock detection, the first-line tool.
- Helgrind / DRD (Valgrind): lock-order and race detection.
- AddressSanitizer: memory errors (use-after-free) that often accompany concurrency bugs.
- Static analysis and clang thread-safety annotations for compile-time lock checks.
- Core dumps and thread dumps to inspect a hung process's lock graph; performance counters
  plus stress/randomized testing to surface contention and rare schedules.

#### Q232

**How would you benchmark a concurrent queue fairly?**

- Fix and report the environment: CPU, core/thread pinning, frequency scaling off,
  isolated cores, warm-up runs discarded.
- Vary producer/consumer counts and item sizes; measure a realistic operation mix, not
  just one thread.
- Report distribution, not just mean: throughput plus p50/p99/p99.9 latency, and run long
  enough to reach steady state.
- Avoid pitfalls: dead-code elimination of results, allocation noise, coordinated omission
  in latency measurement; repeat runs and report variance.

#### Q233

**Which metrics would you measure?**

- Throughput (ops/sec) and latency including tail latency (p99/p99.9), which often matters
  most.
- CPU utilization and context-switch rate (reveals blocking/oversubscription).
- Lock contention time and cache misses / false-sharing indicators from hardware counters.
- Memory allocation rate, queue depth over time, and dropped/rejected work. Together these
  distinguish a structure that is fast on average from one that is predictable under load.

#### Q234

**Why can a concurrent benchmark produce misleading results?**

- Microbenchmarks omit real contention, cache pressure, and allocation, so they flatter
  lock-free designs that lose under realistic sharing.
- Coordinated omission hides tail latency when the harness stops the clock during stalls.
- Thread scheduling, frequency scaling, NUMA effects, and warm/cold caches add high
  run-to-run variance; a single run or reporting only the mean misleads.
- The compiler may optimize away unused results, and pinning/affinity choices can make one
  design look artificially good.

#### Q235

**How would you distinguish lock contention from cache contention?**

- Lock contention shows up as time blocked on mutexes and elevated context switches;
  profilers and `perf lock` / futex wait times point to it, and it eases when you shrink
  the critical section or shard the lock.
- Cache contention (including false sharing) shows high cache-miss / HITM counters and
  cross-core coherence traffic while CPUs stay busy spinning rather than blocked, often
  with little mutex wait time.
- Diagnostic: if removing the lock (or going lock-free) does not help but
  cache-aligning/padding the hot fields does, it was cache contention; if lock wait time
  dominates the profile, it was lock contention.

## Strong final interview questions

#### Q236

**Explain the difference between a mutex, semaphore, condition variable, latch, and
barrier.**

- Mutex: owned mutual-exclusion lock; the locker must unlock; protects a critical section.
- Semaphore (`counting_semaphore`/`binary_semaphore`): ownerless permit counter for
  limiting concurrency or signalling; any thread may release.
- Condition variable: used with a mutex to block until a predicate over shared state
  becomes true; needs a predicate loop to handle spurious and lost wakeups.
- Latch (`std::latch`): single-use countdown to a one-time rendezvous; not reusable.
- Barrier (`std::barrier`): reusable phase synchronization for a fixed thread group, with
  an optional completion function run exactly once per phase.

#### Q237

**Explain acquire-release synchronization with a producer-consumer example.**

The producer writes the payload and then does a release store on a flag; the consumer does
an acquire load of the flag and, once it sees the flag set, is guaranteed to see every
write the producer sequenced before the release. This establishes a synchronizes-with edge
(release store to acquire load that reads it) and hence happens-before, so the non-atomic
payload is safe to read.

```cpp
std::atomic<bool> ready{false};
int payload = 0;
// producer
payload = 42;
ready.store(true, std::memory_order_release);
// consumer
while (!ready.load(std::memory_order_acquire)) {}
use(payload);   // guaranteed to observe 42
```

Acquire-release is cheaper than `seq_cst` because it orders only this pairing, not a
single total order across all atomics.

#### Q238

**Explain why `volatile` is not a replacement for `std::atomic`.**

`volatile` only tells the compiler not to elide or reorder accesses to that object
relative to other volatile accesses; it was designed for memory-mapped I/O, not threads.
It provides no atomicity (a `volatile` read-modify-write can still tear), no inter-thread
ordering/visibility guarantees, and does not restrain CPU reordering or insert fences.
`std::atomic` guarantees indivisible operations and a defined memory-ordering model
(acquire/release/seq_cst) that establishes happens-before between threads. So concurrent
access via `volatile` is still a data race and undefined behavior.

#### Q239

**Describe a deadlock and at least three ways to prevent it.**

A deadlock is a cycle of threads each holding a resource and waiting for one another, so
none can proceed (the four Coffman conditions: mutual exclusion, hold-and-wait, no
preemption, circular wait). Prevention:

- Impose a global lock ordering so all threads acquire mutexes in the same order (breaks
  circular wait).
- Acquire all needed locks at once with `std::scoped_lock`/`std::lock`, which uses
  deadlock-avoidance.
- Avoid hold-and-wait: use `try_lock` with back-off, or restructure to hold only one lock
  at a time / copy data out and release early.
- Reduce the number of locks and use lock-free or single-owner designs where possible.

#### Q240

**Compare mutex-based, lock-free, and wait-free designs.**

- Mutex-based: simplest to write and reason about; a blocked thread makes no progress and
  a crash while holding the lock can stall everyone; contention and convoying hurt
  scalability.
- Lock-free: guarantees that *some* thread always makes progress (system-wide progress),
  avoids priority inversion and holder-death stalls, but individual threads can starve,
  and correctness/memory-reclamation (ABA, hazard pointers) is hard.
- Wait-free: every thread completes in a bounded number of steps regardless of others
  (strongest progress), valuable for real-time, but usually the most complex and sometimes
  slower in the common case.
- Stronger progress guarantees cost complexity and often average-case throughput; pick the
  weakest guarantee that meets the requirement.

#### Q241

**Explain false sharing and how it affects atomic counters.**

False sharing happens when two logically independent variables sit on the same cache line,
so a write by one core invalidates the whole line in other cores' caches even though they
touch a different variable. With per-thread atomic counters packed together, each
increment ping-pongs the shared line between cores via the coherence protocol, turning
what should be contention-free updates into serialized, high-latency traffic. The fix is
to put each hot counter on its own cache line with `alignas(64)` (padding), which restores
independent, scalable updates at the cost of extra memory.

#### Q242

**Design a cache-conscious multi-producer, multi-consumer queue.**

- Bounded ring buffer sized to a power of two for cheap index masking; preallocated, no
  per-op allocation.
- Vyukov-style per-slot sequence number so each producer/consumer CASes a shared ticket
  and then owns a specific slot, minimizing shared mutable state.
- Separate, cache-line-aligned (`alignas(64)`) enqueue and dequeue counters so producers
  and consumers do not false-share each other's index.
- Memory ordering: acquire when loading a slot's sequence, release when publishing it;
  relaxed on failed-CAS retries.
- Backpressure via the bounded size (full -> block/drop/reject by policy); keep per-slot
  data compact but avoid packing unrelated hot fields onto one line.

#### Q243

**Explain how you would safely reclaim memory in a lock-free linked structure.**

The hazard is that a thread may remove a node while another thread still holds a pointer
into it, so freeing immediately risks use-after-free; you need deferred reclamation that
frees a node only after no thread can reference it. Options:

- Hazard pointers: each thread publishes the nodes it is currently accessing; a removed
  node is freed only once it appears in no hazard list.
- Epoch-based reclamation: threads enter epochs; retired nodes are freed once all threads
  have advanced past the epoch in which they were retired.
- RCU: readers run lock-free; reclamation waits for a grace period after which all
  pre-existing readers have finished.
- Tagged pointers/version counters additionally guard against ABA so a reused address is
  not mistaken for the original.

#### Q244

**Describe a situation where a lock-free implementation performs worse than a mutex-based
implementation.**

Under high write contention on a single hot location, a lock-free CAS loop can livelock in
practice: many threads repeatedly fail and retry their CAS, burning CPU and generating
cache-coherence traffic, so useful throughput drops. A mutex instead serializes the
threads, lets one finish without interference, and can block the losers so cores are
freed, yielding higher real throughput and lower energy. Lock-free reclamation overhead
(hazard pointers/epochs) can also outweigh the benefit when contention is low or critical
sections are tiny. So lock-free is not automatically faster; measure before choosing it.

#### Q245

**Design concurrency for a high-throughput packet-forwarding service.**

- Worker ownership: shard by flow/RX queue so each worker core owns a set of connections
  and touches only its own state (shared-nothing), ideally pinned to cores (RSS/affinity).
- Queue structure: per-core bounded SPSC/MPSC ring buffers between RX, workers, and TX;
  power-of-two size, preallocated, no per-packet allocation.
- Backpressure: bounded queues with a drop/overwrite policy because latency beats
  completeness for packets; shed load early and count drops.
- Shutdown: a `stop_source`/flag plus `notify_all`, drain or discard per policy, then join
  all workers (`std::jthread`).
- Per-thread statistics: cache-line-aligned per-worker counters updated with relaxed
  atomics, aggregated periodically by a monitor thread.
- Memory ordering: release on publish into a ring, acquire on consume; relaxed for pure
  counters.
- Cache-line layout: `alignas(64)` to separate producer and consumer indices and per-core
  counters, eliminating false sharing.
- Error handling: never let one bad packet/worker crash the pipeline; isolate failures per
  shard, log and drop malformed input.
- Benchmarking and race detection: steady-state throughput and p99/p99.9 latency with
  coordinated-omission-aware measurement, pinned cores, and validation under
  ThreadSanitizer.
