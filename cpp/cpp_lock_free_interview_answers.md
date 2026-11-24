<!-- cpp/cpp_lock_free_interview_answers.md -->

# Lock-free concurrency interview answers

Answers to [`cpp_lock_free_interview.md`](cpp_lock_free_interview_answers.md): numbered questions
`Q1`-`Q58` - code code questions `C1`-`C4`, and and final questions `F1`-`F10`. Link to an answer by
its anchor, for example `#q1`.

Try the questions first, then compare.

## Fundamental concepts

#### Q1

**What does lock-free mean in C++?**

Lock-free is a progress guarantee: at any point, among all threads contending on the
structure, at least one makes forward progress in a bounded number of steps, regardless of
how the others are scheduled (even if some are suspended). The whole system cannot
livelock or deadlock. It does not require that every individual thread finish; one thread
may retry indefinitely while others succeed. It is a property of the algorithm, not merely
of using `std::atomic`.

*Red flag:* claiming lock-free means "no locks anywhere" or "faster".

#### Q2

**Does lock-free mean that the program never uses a mutex?**

No. Lock-free is about the progress guarantee of a specific operation/data structure, not
about the global absence of mutexes. A program can be lock-free on its hot path and still
use mutexes elsewhere (startup, configuration, cold paths). Conversely, avoiding
`std::mutex` does not by itself make code lock-free; a spinlock built from atomics is not
lock-free because a thread holding it can block all others.

#### Q3

**What is the difference between: obstruction-free; lock-free; wait-free?**

These are increasingly strong non-blocking progress guarantees. Obstruction-free: a thread
completes in bounded steps if it runs in isolation (no contention), but concurrent threads
may cause each other to abort indefinitely. Lock-free: system-wide progress is guaranteed;
some thread always completes in bounded steps, but individual threads may starve.
Wait-free: every thread completes its operation in a bounded number of its own steps
regardless of contention; strongest, hardest to build, often slower in the common case.
Wait-free implies lock-free implies obstruction-free.

#### Q4

**Does lock-free guarantee that every thread will eventually complete its operation?**

No. Lock-free only guarantees that *some* thread makes progress at each step. A particular
thread can be unlucky and fail its CAS repeatedly forever while other threads keep
succeeding, so it may never complete. The per-thread bounded-completion guarantee is
wait-freedom, which is strictly stronger.

#### Q5

**Does lock-free guarantee freedom from starvation?**

No. Lock-free guarantees system-wide progress but explicitly permits individual thread
starvation: a thread can be perpetually beaten in its compare-exchange by other threads.
Starvation-freedom (every thread eventually completes) is a fairness property that
lock-free does not provide; wait-freedom does.

#### Q6

**Does lock-free always mean faster than a mutex-based implementation?**

No. Under low or no contention a well-implemented mutex (often a futex that stays in user
space uncontended) can be as cheap or cheaper, Lock-free algorithms pay for CAS retries,
cache-line bouncing on shared atomics, strong memory-ordering fences, and expensive memory
reclamation (hazard pointers/epochs), Lock-free wins mainly when you need progress
guarantees under contention or must avoid priority inversion/blocking, not as a blanket
performance technique.

#### Q7

**What is the difference between: atomic; lock-free; wait-free?**

Atomic means an operation is indivisible and free of data races (correct), but it may be
implemented with an internal lock, Lock-free adds the system-wide progress guarantee that
some thread always advances. Wait-free adds per-thread bounded-step completion. So
atomicity is a correctness property; lock-free and wait-free are progress properties. An
`std::atomic<T>` can be atomic yet not lock-free 1f the platform uses a hidden lock
(`is_lock_free()` returns false).

#### Q8

**How can you check whether an atomic type is implemented without locks?**

```cpp
std::atomic<int> value;

if (value.is_lock_free()) {
    // The implementation provides a lock-free atomic representation.
}
```
Use the member `is_lock_free()` for a runtime check (it can depend on alignment/the
specific object), or `std::atomic<T>::is_always_lock_free` for a compile-time
`constexpr bool` constant usable in static_assert. Only `std::atomic_flag` is guaranteed
lock-free by the standard; for every other type it is implementation-defined. On x86-64
integral and pointer atomics up to the pointer width are lock-free, and 16-byte atomics
are lock-free only if the compiler/ISA provides `cmpxchg16b` (often requiring `-mcx16`).

*Follow-up:* prefer `is_always_lock_free` for static assertions since `is_lock_free()` is
not `constexpr`.

## Atomic operations

#### Q9

**What is the purpose of compare_exchange_weak()?**

It performs an atomic read-modify-write: compare the atomic's current value against
`expected`; if equal, store `desired` and return true; if not, load the current value into
`expected` and return false. It is the primitive for building lock-free updates where the
new value depends on the old one, letting a thread publish a change only if no one else
changed the value in between. `weak` maps directly onto LL/SC architectures (ARM, RISC-V)
and may fail spuriously.

#### Q10

**what is the difference between compare_exchange_weak() and compare_exchange_strong()?**

`strong` only returns false on a genuine value mismatch; `weak` may also fail *spuriously*
(return false even when the value matched), because it maps onto
load-linked/store-conditional loops where context switches or cache events can break the
reservation. `weak` is preferred inside a retry loop where you are looping anyway
(cheaper, no inner retry on LL/SC). `strong` is preferred for a single-shot test where a
spurious failure would force you to add your own loop; on x86 (which uses `cmpxchg`) the
two compile to essentially the same code.

#### Q11

**why is weak compare-exchange normally used inside a loop?**

Because `weak` can fail spuriously, a single call is not reliable; you must retry until it
succeeds or until the value genuinely changed in a way that changes your decision. Since
CAS-based updates already live in a loop (recompute `desired` from the refreshed
`expected` and retry on real contention), folding the spurious-failure handling into that
same loop costs nothing extra, which is exactly why `weak` is the right primitive there.

#### Q12

**What happens to the expected argument when compare-exchange fails?**

On failure, `expected` is overwritten with the atomic's *current* (actual) value as read
during the operation. This is by design: it hands you the up-to-date value so the next
loop iteration can recompute `desired` from it without a separate load. On success
`expected` is left unchanged, Forgetting this (e.g., not recomputing `desired`) is a
common lock-free bug.

#### Q13

**what is the difference between fetch_add() and a compare-exchange loop?**

`fetch_add` is a single atomic read-modify-write primitive that always succeeds in one
step and, on x86-64, lowers to a single `lock xadd` instruction, so for a plain add a
constant" it is wait-free, faster, and never spins, A CAS loop reads, computes, and
conditionally stores, retrying on contention, so it can spin and is only lock-free, Use
`fetch_add`/`fetch_sub`/`fetch_or` for simple arithmetic/bitwise updates; use a CAS loop
when the new value is a non-trivial function of the old (e.g., clamping, conditional
updates, pointer swings).

#### Q14

**How would you atomically update a value based on its current value?**

```cpp
std::atomic<int> value{0};

void increment() {
    int expected = value.load(std::memory_order_relaxed);
    do {
        int desired = expected + 1;
    } while (!value.compare_exchange_weak(
        expected,
        expected + 1,
        std::memory_order_relaxed,
        std::memory_order_relaxed));
}
```

Use a CAS 1oop: load the current value into `expected`, compute `desired` from it, and
`compare_exchange_weak(expected, desired, ...)`, relying on the fact that a failed CAS
refreshes `expected` so you recompute on the next iteration. The `desired` must be
recomputed each iteration (the shown code computes `expected + 1` in the CAS argument,
which works, but the local `desired` variable is unused and misleading). For a plain
increment, prefer value. fetch_add(1, std::memory_order_relaxed).

#### Q15

**What is the ABA problem?**

A thread reads a value A, is preempted, and meanwhile other threads change it A -> B-> A.
When the first thread runs its CAS comparing against A, it succeeds because the bits
match, even though the state actually changed and its assumptions (e.g., this node's
`next` is still valid") are now wrong. It is classic in pointer-based lock-free
stacks/queues where a node is popped, freed or reused, and reinserted at the same address,
so the CAS silently corrupts the structure.

#### Q16

**How can version counters or tagged pointers help with ABA?**

Attach a monotonically increasing version/tag to the value and CAS the {pointer, counter}
pair atomically, so an A -> B -> A cycle still changes the counter and the CAS fails.
Implementations: a double-width CAS over {pointer, uint64 counter} (x86-64 `cmpxchg16b`
via 16-byte `std::atomic`, needing 16-byte alignment and `-mcx16`), or packing a tag into
unused high/low pointer bits so an ordinary pointer-width CAS suffices. The counter must
be wide enough that wraparound within a window is implausible.

#### Q17

**What problems can occur when a pointer is atomically changed but the pointed-to object
is deleted?**

You get use-after-free and ABA. Swinging the shared pointer (e.g., `head`) atomically only
removes the node from the structure; other threads may still hold a previously loaded raw
pointer to it and dereference it after you `delete` it. Freeing also lets the allocator
hand the same address back, enabling ABA. This is the central reason lock-free structures
need a safe reclamation scheme (hazard pointers, epochs, RCU) rather than immediate
`delete`.

## Memory ordering

#### Q18

**Why is atomicity alone insufficient for a lock-free data structure?**

Atomicity makes a single variable®s RMw indivisible, but a data structure's invariants
span multiple memory locations (e.g., a node's fields plus the `head` pointer). Without
ordering constraints, the compiler and CPU may reorder the non-atomic writes relative to
the atomic publish, so another thread can observe the published pointer before the
pointed-to data is visible. You need acquire/release (or stronger) to establish a
happens-before relationship that makes the associated non-atomic writes visible, not just
the atomic store itself.

#### Q19

**What is the purpose of acquire and release ordering?**

They create a one-way happens-before edge between threads, A release store ensures all
memory writes sequenced before it in the storing thread become visible to any thread that
performs an acquire load observing that store; the acquire load prevents subsequent
reads/writes from being hoisted above it, Together they implement the publish/consume
handshake (writer releases, reader acquires) cheaply, without the full global ordering of
`seq_cst`. A release has no effect unless paired with a matching acquire that reads the
released value.

#### Q20

**when is memory_order relaxed sufficient?**

When you need atomicity but no ordering or visibility guarantees about other* memory,
i.e., no happens-before is being established, Typical cases: statistics/event counters
read later, reference-count increments (decrement-to-zero still needs acquire/release to
order destruction), or generating unique IDs. It is wrong whenever the atomic is used as a
flag to publish or consume other data, since relaxed permits arbitrary reordering of
surrounding accesses.

#### Q21

**Can relaxed atomics safely publish related non-atomic data?**

No. Relaxed operations impose no ordering on surrounding non-atomic accesses, so a reader
could see the flag set while the non-atomic data it "publishes" is still stale or
partially written; that is a data race and undefined behavior. To publish data you need at
minimum a release store on the writer paired with an acquire load on the reader (or a
`seq_cst` pair), which establishes the happens-before that makes the non-atomic writes
visible.

#### Q22

**What is the difference between an atomic operation and a memory barrier?**

An atomic operation is an indivisible access to a specific atomic object and may carry an
ordering parameter. A memory barrier/fence (`std::atomic_thread_fence`) is a standalone
ordering constraint on *all* preceding/following memory accesses of the thread; it is not
tied to a particular object and performs no load or store itself. A fence is more general
and can be coarser; fence-fence and fence-atomic synchronization have their own rules. In
practice the ordering argument on an atomic op (acquire/release) is usually preferable to
a separate fence because it is more precise and often cheaper.

#### Q23

**What is the usual memory-ordering strategy for publishing a newly initialized object?**

```cpp
data = 42;
ready.store(true, std::memory_order_release);
```

Writer side: fully initialize the object, then publish the visibility flag/pointer with a
release store. Reader side:

```cpp
if (ready.load(std::memory_order_acquire)) {
    use(data);
}
```

The acquire load that observes the release store establishes happens-before, guaranteeing
the reader sees the initialized `data`. This release/acquire handshake is the standard,
cheap publication pattern; `seq_cst` also works but is stronger than needed.

#### Q24

**Why can using memory_order_seq_cst simplify reasoning?**

`seq_st` gives a single total order over all sequentially-consistent operations that is
consistent with each thread's program order, so you can reason as if all threads
interleave on one global timeline, avoiding subtle acquire/release and fence analysis. It
is the default for `std::atomic` operations precisely because it is the easiest to get
right. The cost is stronger fences (e.g., `mfence` or `lock`-prefixed/`dmb ish` on ARM),
so it is slower; the usual advice is to start with `seq_st` for correctness and relax
only where profiling and careful proof justify it.

#### Q25

**What are the performance costs of unnecessarily strong memory ordering?**

Stronger ordering inserts more/heavier fences that inhibit compiler and CPU reordering and
store-buffer forwarding. On x86-64 a *seq_st* store needs a full barrier
(`xchg`/`mfence`) whereas release is nearly free (plain `mov`), so overusing `seq_cst`
adds store-buffer-drain latency and prevents useful reorderings. On weakly-ordered
ARM/AArch64 the gap is larger: `seq_st` emits `dmb`/`ldar`/`stir` barriers that stall the
pipeline and serialize the memory subsystem. The result is reduced ILP, more
cache-coherence traffic, and worse scalability.

## Lock-free data structures

#### Q26

**How would you implement a lock-free stack?**

A Treiber stack over an atomic `head`, Push; allocate a node, set
`node->next = head.load(acquire)`, then CAS `head` from that old value to `node` with
release on success, retrying on failure (refreshing `next` from the updated `expected`).
Pop: load `head` (acquire), read `head->next`, CAS `head` to `next` with acarel, retrying
on failure; return the popped node. The hard part is reclamation (ABA and use-after-free
on pop), which needs tagged pointers/hazard pointers/epochs, not plain `delete`.

#### Q27

**What is the Treiber stack?**

The canonical lock-free LIFO stack (R. K. Treiber, 1986); a singly linked list with an
atomic `head` pointer, where push and pop are single CAS operations swinging `head`. It is
simple and lock-free but suffers from the ABA problem on pop and from
safe-memory-reclamation difficulties, which is why real implementations add version-tagged
pointers or hazard pointers/epoch reclamation.

#### Q28

**Where is the linearization point in a lock-free stack push or pop?**

It is the successful CAS on `head`. For push, the operation appears to take effect
atomically at the instant the CAS swings `head` to the new node; for pop, at the instant
the CAS swings `head` to `old_head->next`. Before that CAS succeeds nothing is observable;
after it, the effect is fully visible. The reads/retries before it are not observable to
other threads, so the single successful atomic is the logical "moment" the operation
happens.

#### Q29

**What is linearizability?**

A correctness condition for concurrent objects: every operation appears to take effect
instantaneously at some single point (its linearization point between its invocation and
its response, and that global ordering is consistent with a legal sequential history of
the object. It makes a concurrent object behave as if operations executed one at a time in
some real-time-respecting order, which lets you reason about it with its sequential
specification. It is stronger than sequential consistency because it also respects
real-time ordering of non-overlapping operations.

#### Q30

**How would you implement a lock-free queue?**

For MPMC, the Michael-Scott queue: a singly linked list with atomic `head` and `tail` and
a dummy sentinel node. Enqueue: CAS the last node's `next` from null to the new node, then
try to swing `tail` forward (helping if another thread's `tail` lagged). Dequeue: read
`head`, `head->next`; if non-null, CAS `head` to `next` and return the value, advancing a
lagging `tail` first if needed. It needs safe reclamation (hazard pointers/epochs) and
careful acquire/release ordering. For the common single-producer/single-consumer case a
bounded ring buffer is far simpler and faster.

#### Q31

**What is the difference between an SPSC, MPSC, SPMC, and MPMC queue?**

The prefixes state how many concurrent producers and consumers are allowed: SPSC = single
producer, single consumer; MPSC = many producers, one consumer; SPMC = one producer, many
consumers; MPMC = many of both. Fewer concurrent participants on an end means the
index/pointer for that end is owned by one thread and needs no CAS, enabling much simpler,
faster designs (SPSC needs no CAS at all). MPM requires CAS and helping on both ends and
is the hardest to make correct and scalable.

#### Q32

**Why is an SPSC ring buffer easier to implement than an MPMC queue?**

In SPSC exactly one thread owns the head index and one owns the tail index, so each index
has a single writer; you never need CAS, only a release store by the writer of an index
and an acquire load by the reader, which eliminates retries, ABA, and most reclamation
concerns. MPMC has multiple writers to each index/pointer, so every update needs CAS
loops, helping protocols for lagging pointers, and safe memory reclamation, plus it
suffers cache-line contention on the shared indices. SPSC is therefore both simpler and
dramatically faster.

#### Q33

**How do you distinguish an empty ring buffer from a full ring buffer?**

With plain head/tail indices modulo capacity, `head == tail` is ambiguous (both empty and
full look identical). Common fixes: (a) keep one slot always empty so full is
`(tail + 1) % N == head` and empty is `head == tail`, wasting one slot; (b) use
free-running (non-wrapped) 64-bit indices and compute size as `tail - head`, where empty
is `tail == head` and full is `tail - head == capacity`, masking only when indexing; (c)
maintain a separate element count, The monotonic-index approach is popular because it also
avoids an extra modulo and is wraparound-safe for practical runtimes.

#### Q34

**What does the producer index represent?**

The producer (write/tail) index is the position where the next element will be written; it
counts how many elements have been enqueued. It is advanced only by the producer after the
element's data has been stored, published with a release store so the consumer, on an
acquire load, sees both the new index and the written payload, The consumer reads it
(acquire) to know how far it may safely consume.

#### Q35

**What does the consumer index represent?**

The consumer (read/head) index is the position of the next element to be read; it counts
how many elements have been dequeued/consumed. It is advanced only by the consumer after
it has finished reading a slot, published with a release store so the producer can reclaim
that slot. The producer reads it (acquire) to know how much free space is available and
whether the buffer is full.

#### Q36

**How do you handle index wraparound safely?**

Two safe approaches. With capacity a power of two, use free-running unsigned indices and
map to a slot with a bitmask (`index & (N-1)`); unsigned overflow is well-defined modular
arithmetic, and `tail - head` still gives the correct occupancy as long as capacity \<=
index range (64-bit indices make real wraparound irrelevant). Alternatively keep indices
in `[0, N)` and increment with `(i + 1) % N` (or a conditional subtract). Avoid signed
index overflow (UB) and avoid non-power-of-two masking.

#### Q37

**How can a bounded ring buffer handle a full condition?**

Possible policies include:

- block the producer;
- reject the item;
- drop the newest item;
- drop the oldest item;
- overwrite existing data;apply backpressure.

A bounded queue must pick a policy when full: block/wait for space (turns it blocking),
reject/return failure to the caller, drop the newest incoming item, drop or overwrite the
oldest (useful for telemetry where freshest data matters), or apply backpressure upstream
(Flow control). The choice is a product decision trading latency, loss, and liveness;
lock-free designs usually return failure or overwrite-oldest rather than block.

#### Q38

**Why should a lock-free queue usually avoid dynamic allocation on its hot path?**

`new`/`delete` typically take internal locks and can block, which breaks the lock-free
progress guarantee and introduces unbounded latency and priority inversion. Allocation
also causes cache misses, fragmentation, and, when nodes are freed, the safe-reclamation
(ABA/use-after-free) problem. Preferring a preallocated bounded ring buffer or a pool
keeps the hot path allocation-free, deterministic, and genuinely non-blocking.

#### Q39

**Can a ring buffer be lock-free while still having bounded capacity?**

Yes. Bounded capacity and lock-freedom are orthogonal. A fixed-size array with atomic
indices is lock-free (SPSC is even wait-free): operations complete in a bounded number of
steps using only atomics, and being full just means enqueue fails or applies a policy,
which is a return condition, not a loss of the progress guarantee. Boundedness actually
helps, since it avoids hot-path allocation and the associated reclamation problems.

#### Q40

**What is the difference between a lock-free queue and a wait-free queue?**

A lock-free queue guarantees some thread always makes progress, but a given thread may
retry its CAS indefinitely under contention (possible starvation). A wait-free queue
guarantees every thread completes its operation in a bounded number of its own steps
regardless of contention, typically via helping mechanisms or per-thread slots. Wait-free
is stronger and valuable for hard real-time, but usually more complex and slower in the
uncontended common case, (An SPSC ring buffer is naturally wait-free.)

## Memory reclamation

#### Q41

**Why is memory reclamation one of the hardest parts of lock-free programming?**

Removing a node from a shared structure (a CAS) does not mean no one is looking at it:
other threads may hold a raw pointer loaded before the removal and could dereference it at
any time. You cannot take a lock to coordinate deletion without defeating lock-freedom, so
you need a scheme to prove no thread can still reach the node before freeing it, all while
remaining non-blocking. Getting this wrong gives use-after-free and ABA; getting it right
(hazard pointers, epochs, RCU) is intricate and often dominates performance.

#### Q42

**Why is this unsafe?**

```cpp
Node* node = head.load();

if (node != nullptr) {
    head.compare_exchange_strong(node, node->next);
    delete node;
}
```

Multiple bugs: (1) it reads `node->next` outside the CAS, and between removal and `delete`
another thread may still hold and dereference `node` (use-after-free); (2) the CAS may
fail (another thread changed `head`), yet the code deletes `node` unconditionally,
deleting a node still in the structure; (3) freeing `node` enables ABA. Safe removal must
check the CAS result and defer deletion via hazard pointers/epochs/CU until no thread can
reference the node.

#### Q43

**What are hazard pointers?**

A per-thread, single-writer reclamation scheme where each thread publishes, in a globally
visible slot, the pointers it is currently dereferencing ("hazards"). Before freeing a
retired node, a thread scans all hazard pointers; if any thread has published that node,
reclamation is deferred until it is no longer hazardous. This bounds the amount of
unreclaimed memory per thread and tolerates stalled threads (a paused thread only pins the
few nodes it published). C++26 standardizes `std::hazard_pointer`.

#### Q44

**How do hazard pointers protect a node from deletion?**

A reader publishes the node pointer into its hazard slot and then re-validates that the
node is still reachable (reloads `head` and confirms it matches) before dereferencing;
this load-publish-recheck closes the race where the node is removed between load and
protect. A thread that retires a node adds it to a private retire list and only frees it
after a scan confirms no hazard slot references it. Thus a node observed by any reader
cannot be freed out from under it.

#### Q45

**What is epoch-based reclamation?**

Threads operate within a global epoch counter; a thread entering a critical section
announces the current epoch and increments it periodically. A retired node is tagged with
the epoch in which it was removed and freed only once every active thread has advanced
past that epoch (guaranteeing no thread could still hold a reference). It is cheaper
per-operation than hazard pointers (no per-pointer publishing, just an epoch announce on
entry/exit) but its reclamation is coarser and can be delayed arbitrarily by a single
stalled thread. RCU is a closely related idea.

#### Q46

**What is read-copy-update, or RCU?**

RCU splits updates into a removal phase and a reclamation phase. Readers run in
lightweight (often zero-cost) read-side critical sections and never block; a writer makes
a change (typically copy-modify-publish via an atomic pointer swap) and then waits for a
"grace period" during which every pre-existing reader has finished before freeing the old
data. It is ideal for read-mostly structures. C++26 adds
`std::rcu_domain/std::rcu_synchronize` support.

#### Q47

**What is deferred reclamation?**

The general technique of not freeing removed memory immediately but placing it on a
retire/limbo list and reclaiming it later, once a safety condition proves no thread can
still reference it. Hazard pointers (scan-based), epoch-based reclamation
(epoch-advance-based), and RCU (grace-period-based) are all concrete deferred-reclamation
strategies. The goal is to decouple logical removal from physical deletion so lock-freedom
and memory safety coexist.

#### Q48

**How do hazard pointers compare with epoch-based reclamation?**

Hazard pointers give tight, bounded memory usage and are robust to stalled threads (a
blocked thread only pins the handful of pointers it published), but cost a store plus
recheck per protected pointer and a scan on reclaim. Epoch-based reclamation has very Low
read-side overhead (announce an epoch, no per-pointer stores) and simpler reader code, but
a single thread that stalls inside a critical section prevents the epoch from advancing.
so unreclaimed memory can grow unboundedly, In short: hazard pointers trade per-access
cost for bounded memory and stall tolerance; EBR trades those for cheaper reads.

#### Q49

**What happens if a thread pauses indefinitely while holding a hazard pointer or
participating in an epoch?**

With hazard pointers, only the specific nodes that thread has published remain
unreclaimable; everything else is freed normally, so the leak is bounded and the system
keeps making progress. with epoch-based reclamation/RCU, a thread stalled Inside a
read-side critical section prevents the global epoch/grace period from advancing, so "aLL*
nodes retired after it entered pile up unreclaimed, giving unbounded memory growth until
it resumes. This stall-tolerance difference is the main practical reason to choose hazard
pointers for adversarial or preemptible environments.

#### Q50

**Why can memory reclamation consume a significant portion of lock-free queue
nonformance)**

Reclamation adds work on every operation: hazard-pointer stores plus rechecks and periodic
(threads x retired) scans, or epoch announcements and deferred-free bookkeeping. These
touch shared metadata (hazard arrays, epoch counters) causing extra cache-coherence
traffic and atomics, and deferred frees hurt locality and allocator behavior. In
well-optimized lock-free queues the actual enqueue/dequeue is a couple of CAS operations,
so the reclamation machinery can easily rival or exceed the cost of the core operation.

## Lock-free versus mutex-based design

#### Q51

**When is a mutex-based design preferable to a lock-free design?**

When correctness and maintainability matter more than the last bit of scalability, when
the structure has complex multi-location invariants, when contention is low (an
uncontended futex is cheap), or when you need straightforward exception safety and memory
reclamation. Also when the critical section is short and bounded and you do not need
non-blocking progress guarantees (no hard real-time/priority-inversion constraints). The
default should be a mutex; reach for lock-free only with a demonstrated need and
measurements.

#### Q52

**What are the advantages of a mutex?**

```
- Simpler reasoning;
- easier invariant protection;
- easier memory reclamation;
- clearer exception handling;
- often good performance under low contention.
```

A mutex lets you protect arbitrarily complex multi-variable invariants in a single
critical section, makes reasoning sequential (and composes with RAII/`std::lock_guard` for
exception safety), sidesteps the ABA and safe-reclamation problems entirely (you can
`delete` while holding the lock), and under low contention a futex stays in user space and
is very cheap. It is the correct default for most synchronization.

#### Q53

**What are the advantages of a lock-free design?**

```
- One stalled thread does not necessarily block all other threads;
- potentially better progress under contention;
- useful for carefully designed queues and counters;
- can reduce scheduler involvement in some workloads.
```

Lock-free guarantees system-wide progress, so a thread preempted or killed mid-operation
cannot freeze the others (no deadlock, no priority inversion, no convoying). Under high
contention or for simple hot structures (counters, SPS/MPSC queues) it can scale better
and avoids kernel scheduler involvement (no blocking/wakeups). It shines in real-time and
interrupt/signal contexts where blocking is unacceptable.

#### Q54

**What are the disadvantages of lock-free programming?**

```
- Complex correctness proofs;
- difficult memory reclamation;
- ABA problems;
- subtle memory-ordering bugs;
- possible starvation;
- high retry and cache-coherence costs;
- difficult debugging and testing.
```

Lock-free code is hard to design and prove correct (linearizability, memory-order
reasoning), it forces you to solve safe reclamation (hazard pointers/epochs) and guard
against ABA, and bugs are subtle, non-deterministic, and hard to reproduce/test. At
runtime it can starve individual threads and incur heavy CAS-retry and cache-11ne-bouncing
costs. The engineering and maintenance burden is substantial.

#### Q55

**Why can a lock-free algorithm be slower than a mutex?**

```
Expected points:

- repeated compare-exchange failures;
- cache-line bouncing:
- contention on shared atomics;
- expensive memory ordering;
- poor locality;
- memory reclamation overhead;
- starvation or unfairness;
- a well-implemented uncontended mutex may be very cheap.
```

Under contention, threads waste cycles on failing CAS retries while the shared atomic's
cache line ping-pongs between cores (coherence traffic), strong fences serialize the
pipeline, and reclamation (hazard scans/epochs) adds overhead; locality is often worse
than a compact lock-protected structure. Meanwhile an uncontended mutex is just an atomic
flip in user space. So the "no locks - faster" assumption frequently fails; always
measure.

#### Q56

**Does removing a mutex necessarily remove synchronization overhead?**

No. The synchronization cost moves, it does not vanish. Lock-free code still performs
atomic RM operations, memory fences, and CAS retries, all of which generate
cache-coherence traffic and inter-core communication, the same physical costs a mutex
incurs. You have traded blocking for spinning/retrying plus reclamation work. The real
overhead is contention on shared cache lines, which both designs pay.

#### Q57

**How can lock-free code cause cache-line contention?**

Every atomic RMN (CAS, `fetch_add`) on a shared variable acquires the cache line in
exclusive/modified state, invalidating every other core's copy; when many cores hammer the
same `head`/`tail`/counter, that line bounces between cores (the coherence protocol's MESI
invalidations), serializing them. False sharing makes it worse when logically independent
atomics sit on one 64-byte line. Mitigations: per-core/per-thread sharding,
`alignas(std::hardware_destructive_interference_size)` padding, and reducing the frequency
of shared-atomic writes.

#### Q58

**What is the relationship between compare-exchange retries and contention?**

They are tightly coupled and self-reinforcing: higher contention means more threads racing
on the same atomic, so more CAS failures and retries; each retry re-reads the contended
line, adding coherence traffic that slows everyone and causes still more failures. This
can degrade throughput super-linearly and starve unlucky threads. Backoff
(exponential/random), reducing shared-atomic traffic (sharding, batching), and choosing
`fetch_add`-style primitives where possible are standard mitigations.

## Code-based questions

#### C1

**Question 1: CAS retry loop**

```cpp
std::atomic<int> counter{0};

void increment() {
    int expected = counter.load();

    while (!counter.compare_exchange_weak(
        expected,
        expected + 1)) {
    }
}
```

Logically correct: on failure `compare_exchange_weak` refreshes `expected` with the
current value and `expected + 1` is re-evaluated each iteration, so it converges. The
memory ordering defaults to `seq_st` for both success and failure (the `load` is also
`seq_st`), which is stronger and slower than needed here. It is lock-free but not
wait-free, and under heavy contention it suffers repeated failures and cache-line
bouncing. A simpler, wait-free, and faster version is:

```cpp
counter.fetch_add(1, std::memory_order_relaxed);
```

If the counter is only used for statistics, relaxed ordering is sufficient.

#### C2

**Question 2: unsafe reclamation**

```cpp
Node* old_head = head.load(std::memory_order_acquire);

if (old_head != nullptr &&
    head.compare_exchange_strong(
        old_head,
        old_head->next,
        std::memory_order_acq_rel)) {
    delete old_head;
}
```

The CAS correctly removes the node (and short-circuits so `delete` only runs on success),
but deletion is still unsafe: another thread may have loaded `old_head` before the swing
and can dereference it after `delete`, a use-after-free; the freed address can also be
recycled, enabling ABA on the next CAS. This single-attempt code also silently does
nothing under contention (no retry loop). Fix by deferring reclamation: protect `old_head`
with a hazard pointer before dereferencing and retire-then-scan before freeing, or use
epoch-based reclamation/RU; add a retry loop for the CAS.

```cpp
// Sketch with retry + deferred reclamation
Node* old_head = head.load(std::memory_order_acquire);

while (old_head &&
       !head.compare_exchange_weak(old_head, old_head->next,
                                   std::memory_order_aca_rel,
                                   std::memory_order_acquire)) {

}
if (old_head) retire(old_head); // freed later by HP/epoch/RCU, not delete
```

The sketch shows the shape, not a full hazard-pointer protocol: reading `old_head->next`
is only safe once `old_head` is protected. With hazard pointers you publish `old_head`,
then re-load `head` and retry if it changed, and only then read `next`. With an epoch or
RCU scheme, the whole pop runs inside a read-side critical section.

#### C3

**Question 3: false sharing**

```cpp
std::atomic<std::size_t> head{0};
std::atomic<std::size_t> tail{0}:
```

`head` and `tail` are typically written by different threads (consumer vs producer) yet
sit within the same 64-byte cache line, so each write invalidates the other core's copy,
i.e., false sharing, causing the line to bounce and crippling throughput even though the
two indices are logically independent. Separating them onto distinct cache lines removes
the contention. The cost of padding is wasted memory (roughly a cache line per index) and
worse single-threaded density, so validate against the target. A fix:

```cpp
struct alignas(64) Index {
    std::atomic<std::size_t> value{0};
};

struct Queue {
    Index head;
    Index tail;
};
```

Prefer `alignas(std::hardware_destructive_interference_size)`; note the true line size is
platform-dependent (64 on mainstream x86/ARM, 128 on Apple M-series), so this should be
validated for the target.

#### C4

**Question 4: lock-free does not mean wait-free**

```cpp
while (!state.compare_exchange_weak(expected, desired)) {
    // Retry indefinitely.
}
```

Yes, one thread can theoretically retry forever: if other threads keep succeeding and
changing `state` first, this thread's CAS keeps falling, The structure still makes
system-wide progress (someone always succeeds), so it is lock-free, but this particular
thread may starve, so it is not wait-free, Demonstrating this distinction (plus the need
for `desired` to be recomputed from the refreshed `expected` each Iteration, which the
snippet omits) is the point of the question.

## Strong final interview questions

#### F1

**Explain why lock-free does not mean wait-free,**

Lock-free is a system-wide* guarantee: at least one thread always completes in a bounded
number of steps, so the program as a whole never stalls. It says nothing about any
individual thread, which can lose every CAS race and retry forever while others succeed,
that is starvation. Wait-free is the *per-thread* guarantee that every thread finishes in
a bounded number of its own steps regardless of contention, typically achieved with
helping mechanisms. So wait-free is strictly stronger; lock-free permits starvation,
wait-free forbids it.

#### F2

**Design an SPSC lock-free ring buffer for fixed-size packets.**

- Fixed array of `N` slots (N a power of two) holding fixed-size packets; two indices!
  `head` (consumer-owned) and `tail` (producer-owned), each `std::atomic<size_t>`,
  free-running and masked with `& (N-1)` for indexing.
- Each index has exactly one writer, so no CAS is needed; it is naturally wait-free.
- Producer `push`: load `tail` relaxed (own), load `head` acquire; if `tail - head == N`
  the buffer is full - fail/policy; else write the packet into slot `tail & (N-1)`, then
  `tail.store(tail+1, release)` so the consumer sees data-before-index.
- Consumer `pop`: load `head` relaxed (own), load `tail` acquire; if `head == tail` empty
  -> fail; else read slot `head & (N-1)`, then `head.store(head+1, release)` to free the
  slot.
- Pad `head` and `tail` to separate cache lines
  (`alignas(std::hardware_destructive_interference_size)`) to avoid false sharing between
  producer and consumer.

The release/acquire pairing on each index is exactly what publishes the payload and
reclaimed-slot visibility; no allocation on the hot path.

#### F3

**Explain why an MPMC queue requires more coordination than an SPSC queue.**

In SPSC each index has a single writer, so updates are plain release stores with no
possibility of two threads racing, hence no CAS, no ABA, no reclamation issues, and it is
wait-free. In MPM multiple producers contend for the tail and multiple consumers for the
head, so every advance must be a CAS loop, often with a "helping" protocol to fix a
lagging tail another thread left behind, plus per-slot sequence numbers or safe
reclamation to avoid ABA/use-after-free. That extra coordination adds retries, fences, and
cache-line contention that SPSC simply does not have.

#### F4

**Explain the ABA problem using a lock-free stack.**

Thread 1 reads `head = A` and `A->next = B`, intending to pop by CAS(head, A -> B). It is
preempted. Thread 2 pops A, pops B, frees/reuses them, then pushes A back, so `head = A`
again but now `A->next = C` (B is gone). Thread 1 resumes; its CAS(head, A -> B)
*succeeds* because `head` still equals A by bit value, but it wrongly sets `head = B`, a
freed/invalid node, corrupting the stack. The remedy is a version-tagged head
(double-width CAS or tagged pointer) so the A -> ... -> A cycle changes the tag and the
CAS fails, plus safe reclamation so A is not reused prematurely.

#### F5

**Explain why removing a node from a lock-free structure is easier than safely deleting
it.**

Removal is a single atomic step: one successful CAS unlinks the node from the shared
structure so new operations can no longer reach it, that is the linearization point and it
is straightforward. Deletion is hard because unlinking does not guarantee no thread still
holds a pointer loaded before the unlink; freeing it risks use-after-free, and recycling
the address risks ABA. You cannot lock to coordinate without breaking lock-freedom, so you
must defer the free until a reclamation scheme (hazard pointers, epochs, RCU) proves no
thread can reference the node, which is the genuinely difficult part.

#### F6

**Compare hazard pointers, epoch-based reclamation, and RCU.**

- **Hazard pointers:** each thread publishes the few pointers it is actively using; a
  retiring thread scans those and frees only unreferenced nodes. Bounded memory, robust to
  stalled threads, but per-access store+recheck cost and periodic scans. (C++26:
  `std::hazard pointer`.)
- **Epoch-based reclamation (EBR):** threads announce a global epoch on entry; a retired
  node is freed once all threads pass its epoch, Very cheap reads, simple code, but a
  single stalled thread in a critical section blocks epoch advance -> unbounded memory
  growth.
- **RCU:**zero/near-zero-cost read-side sections; writers publish via pointer swap and
  wait a grace period before freeing. Excellent for read-mostly data; writer must walt out
  all pre-existing readers, (C++26 adds RCU support.)

Rule of thumb: hazard pointers for bounded memory and stall tolerance, EBR for lowest read
overhead when stalls are unlikely, Rcu for read-mostly structures.

#### F7

**Explain the role of acquire and release operations in a lock-free queue,**

They create the happens-before edges that make a producer's writes visible to the consumer
and vice versa, which atomicity alone does not provide. The producer writes the payload
(or links a node) and then publishes the index/pointer with a release* store; the
consumer does an *acquire load of that index/pointer, and because the acquire observes
the release, all the producer's prior writes are guaranteed visible, so the consumer never
reads a slot before its data is written. Synmetrically the consumer releases the freed
slot and the producer acquires it to know space is available. Getting these pairings wrong
yields torn/stale reads and data races.

#### F8

**Describe a case where a mutex is better than a lock-free algorithm.**

A moderately contended data structure with complex multi-field invariants, say an
in-memory index that must atomically update a map, a free list, and a counter together. A
mutex lets you perform the whole multi-location update as one critical section with
simple, obviously-correct code, RAII exception safety, and trivial memory reclamation
(`delete` under the lock), while an uncontended or lightly contended futex is very cheap.
A lock-free version would need intricate helping, ABA handling, and deferred reclamation
for little or no throughput gain, and far higher risk of subtle bugs. Here the mutex wins
on correctness, maintainability, and often performance.

#### F9

**How would you benchmark a lock-free queue fairly?**

Use realistic producer/consumer thread counts and message sizes matching production, with
representative arrival patterns (not a tight unthrottled loop) and steady-state
measurement after warm-up. Pin threads to cores, control CPU frequency scaling/turbo, and
run many iterations to get distributions, not single numbers. Compare against a well-tuned
mutex-based baseline on the same hardware and compiler flags, and measure both ends under
balanced and imbalanced load. Capture tail latency and hardware counters (cache misses,
coherence traffic), not just mean throughput, and include the reclamation cost in the
measurement.

#### F10

**Which metrics would you measure?**

```
-  Throughput;
-  median and tail latency;
-  CAS failure rate;
-  CPU utilization;
-  cache misses;
-  cache-line bouncing;
-  memory-reclamation cost;
-  queue depth;
-  dropped items;
-  starvation or fairness.
```

Measure throughput (ops/s) and the full latency distribution (median plus p99/p99.9 tail,
since lock-free tails blow up under contention), the CAS failure/retry rate and
cache-miss/coherence (HITM) counters as contention indicators, CPU utilization (spinning
can inflate it), reclamation overhead, queue depth and dropped items (policy effects), and
per-thread fairness/starvation. The guiding principle: lock-free programming is primarily
a progress guarantee and a memory-reclamation challenge, not simply a technique for making
code faster.
