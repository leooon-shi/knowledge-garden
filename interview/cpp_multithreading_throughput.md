<!--cpp_muttithreading_throughput.md-->

# Scaling C++ Multithreaded Throughput: From Standard Techniques to the Next Level

See also: `numa_hugepages_memory_locality.md`, `performance_profiling_methodology.md`,
`cpp_memory_model_atomics.md`.

This question has two parts, and the second is the interesting one: what to do when you've
already applied the standard playbook - segmented locks, sampled LU, cache-line padding -
and still hit a ceiling. The honest senior answer is that when those techniques stop
helping, it usually means **the bottleneck is no longer the thing you were optimizing.**
You were reducing lock contention and false sharing: if you're st111 stuck, the constraint
has moved to a different layer-memory bandwidth, coherence traffic, the allocator, the
syscall boundary, or the fundamental fact that your algorithm requires threads to
communicate. Stepping further requires *re-measuring to find where the constraint actually
moved*, then attacking that.

## First: the standard playbook (what you've presumably done)

The conventional route to higher multithreaded throughput reduces coordination between
threads:

- **Reduce critical section size** - hold locks for the minimum time: do expensive work
  (allocation, I/O, computation) outside the lock.
- **Segmented / striped locks** - shard one lock into N locks by key hash, so unrelated
  keys don't contend.
- **Reader-writer locks**- when reads vastly outnumber writes.
- **Cache-line padding / `alignas(64)`** - eliminate false sharing so independent
  counters/fields don't ping-pong cache lines.
- **Lock-free structures** - SPSC/MPMC queues, atomics with correct memory ordering.
- **Cache-friendly layout** - SOA, flat containers, sampled LRU to avoid a global LRU
  lock.

These all attack **contention and coherence overhead**. When they stop paying off, the
coordination cost is no longer dominant. Here's where to look next.

## Step 1: Re-measure to locate the *moved* bottleneck

Do not keep tuning the thing that stopped helping. Profile to find the new constraint:

- **`perf stat`** - look at IPC (instructions per cycle), cache-miss rate, and stalled
  cycles. Low IPC with high memory stalls means you're **memory-bound**, not
  contention-bound.
- **`perf c2c`** (cache-to-cache) - pinpoints *remaining* false sharing and cross-core
  cache-line contention you may have missed, and shows HIM (hit-modified) events that
  reveal coherence traffic.
- **Off-CPU / wait analysis** - are threads blocked (on locks, I/O, allocator)? Off-CPU
  flame graphs show where threads sleep.
- **Scaling curve** - plot throughput vs. thread count. Where it flattens or *declines*
  tells you which of the effects below dominates (Amdahl's serial fraction vs. USL's
  coherence penalty).

The result of this step decides which of the following you attack. Everything after here
is conditional on what the profile says.

## Step 2: If you're memory-bandwidth bound

This is the most common wall after contention is solved. Adding threads doesn't help
because they're all waiting on the same DRAM channels. Coherence and padding tuning can't
fix a saturated memory bus.

- **shrink the working set** so it fits in cache - compression, smaller types,
  bit-packing. Less bytes moved - more effective bandwidth. This is where
  resource-constrained data structures matter for *throughput*, not just space.
- **Improve access patterns** - sequential over random, SoA over AoS, blocking/tiling to
  reuse cache-resident data. Turn random DRAM accesses (bandwidth- and latency-bound) into
  streaming accesses the prefetcher loves.
- **Reduce data movement entirely** - compute-in-place, avoid copies, use
  `string_view`/spans, move semantics.
- **NUMA awareness** - see Step 4; on multi-socket boxes, bandwidth is per-socket.

The mental shift: past a point, **throughput is a memory-bandwidth problem, not a CPU or
lock problem,** More cores contending for the same bytes makes it *worse*, not better.

## Step 3: Redesign to eliminate sharing, not just cheapen it

Padding and segmented locks make sharing *cheaper*; the next level is to make threads
**not share at all.**

- **Sharding / partitioning by thread**-give each thread its own data partition so there
  is zero cross-thread access on the hot path, Route work so a given key always lands on
  the same thread (consistent hashing / affinity), This is the single most powerful
  scaling technique: **a shared-nothing architecture scales linearly because there's no
  coordination to bottleneck.**
- **Thread-local aggregation** - each thread accumulates into its own state; merge only
  periodically. Turns a hot shared counter/structure into private ones with rare
  reconciliation. (This generalizes what sampled LU was reaching for.)
- **The SEDA /actor/message-passing mode1**- Instead of many threads touching shared
- state under locks, pin state to a single owner thread and send it messages over an SPSC
- queue. The state is never contended because only one thread ever touches it. This is how
- Redis (single-threaded core), Seastar, and actor frameworks hit extreme throughput.
- **CU(Read-Copy-Update)** - for read-mostly data, readers proceed with *zero*
  synchronization (no locks, no atomics on the read path); writers publish new versions
  and defer reclamation. When reads dominate and you've exhausted rwlocks, RCU is the
  step-change. `folly::rcu`, or hazard pointers for safe reclamation in lock-free
  structures.

The principle: **the fastest synchronization is no synchronization.** If two threads never
touch the same cache line, there is nothing to contend, no coherence traffic, no padding
needed. Redesigning for shared-nothing beats any amount of lock micro-tuning.

## Step 4: Attack the runtime substrate

often the ceiling is below* your code - in the allocator, the scheduler, or the syscall
layer:

- **The allocator is a hidden global lock.** `malloc`/`new` under heavy multithreaded
  churn serializes on internal locks/arenas. Switch to a sharded allocator - **jemalioc,
  tomalloc, or minalloc** - which give per-thread arenas. This alone can unlock
  significant scaling with a one-line link change. Better st111, use pools/arenas to avoid
  the allocator on the hot path entirely.
- **Thread affinity and NUMA pinning** - pin threads to cores (`pthread_setaffinity_np`)
  to preserve cache warmth and stop the scheduler migrating threads across sockets. On
  NUMA, allocate memory on the local node (`numactl`, `libnuma`, first-touch policy) so
  threads hit local DRAM, not the slow cross-socket interconnect. Cross-NUMA access can be
  1.5-2x slower and burns interconnect bandwidth - a common invisible ceiling on big
  servers.
- **Reduce syscalls**-batch I/O with **`io_uring`** to amortize syscall and
  context-switch cost; each syscall is a pipeline flush and potential context switch. For
  a gateway, the syscall boundary is frequently the real 1imter once locks are handled.
- **Right-size the thread count** - more threads than cores causes context-switch thrash
  and cache pollution. often the fix is *fewer* threads (one per core) plus async I/O, not
  more.

## Step 5: Question the concurrency model itself

If per-core sharded/async still isn't enough, the model may be wrong for the workload:

- **Thread-per-core + async (the Seastar / DPDK model)** - one pinned thread per core,
  shared-nothing, cooperative async tasks, message-passing between cores. This is the
  architecture behind the highest-throughput systems (ScyllaDB, Seastar). It eliminates
  locks, context switches, and cross-core sharing by construction.
- **Batching and amortization** - process work in batches to amortize per-item overhead
- (Locking, syscalls, cache misses) across many items. Throughput often improves more from
- batching than from any lock trick.
- **SIMD / data parallelism** - if the hot path is compute, vectorize it; one core doing
  8-wide SIMD may beat several scalar cores and free them for other work.
- **Offload to the kernel or hardware** - kernel-side eBPF/XPfor packet processing,
  `SO_REUSEPORT` to let the kernel shard connections across listener sockets (one per
  core, no user-space load balancer contention), TLS offload, etc.

## Step 6: Accept the theoretical limits and reason about them

The reason "trying more tricks" eventually fails is captured by two laws, and citing them
is the mark of a principal-level answer:

- **Amdahl's Law**- the serial fraction caps speedup. If 5% of the work is inherently
  serial, you can never exceed 20x no matter how many cores. Find and shrink the serial
  fraction (often a shared lock, a single queue, an ordering requirement, or a commit
  point).
- **The Universal Scalability Law (USL)** - adds a *coherence* penalty term for
  cross-thread communication. It predicts that beyond some thread count, throughput
- **actually decreases** because coordination cost grows faster than added capacity. If
  your scaling curve peaks and then drops, you're seeing USL's coherence term dominate -
  and the only cure is to *remove* the coordination (shared-nothing), not to make it
  cheaper.

The USL insight is precisely why segmented locks and padding hit a wall: they lower the
coherence coefficient but don't eliminate it. Shared-nothing partitioning drives that
coefficient toward zero, which is why it's the real step-change.

## The architect's synthesis

The progression, and how to reason about it:

1. **Re-measure** (`perf stat`, `perf e2c`, off-CPU profiles, scaling curve) to find where
   the bottleneck, moved" after your contention fixes. This is the key discipline - don't
   keep sanding a surface that's already smooth.
2. **If memory-bound;** shrink the working set and flx access patterns; more threads won't
   help a saturated bus.
3. **If still contention-bound;** stop cheapening sharing and eliminate* it - shard by
   thread, thread-local aggregation, actor/message-passing, RCU for read-mostly data.
4. **Fix the substrate:** sharded allocator (jemalloc/tcmalloc/mimalloc), NUMA pinning and
   affinity, "io_uring" to cut syscalls, right-size threads.
5. **Change the model;** thread-per-core shared-nothing + async, batching, SIMD,
   kernel/hardware offload,
6. **Reason with Amdahl and US;** find the serial fraction and the coherence term; the
   celling is real, and the only way through it is to remove coordination, not optimize
   it.

The unifying truth: **segmented locks, sampled LU, and padding all make coordination
cheaper. When they run out, the answer is almost never a cleverer lock - it's to stop
coordinating.** Elther the data moved to a different bottleneck (measure and follow it),
or you need a shared-nothing redesign where threads own disjoint state and communicate by
messages instead of memory. The highest-throughput C++ systems in the world are fast not
because they have the best locks, but because they've architected the locks away.

_________________________________________________________________________

## Interview Q&A

**Q: You've applied segmented locks, cache-1ine padding, and sampled LU, and throughput
still won't scale. What does that tell you?** A: That the bottleneck is no longer what
you were optimizing. Those techniques all reduce coordination cost (contention, false
sharing); when they stop paying off, the constraint has moved to a different layer -
memory bandwidth, coherence traffic, the allocator, the syscall boundary, or an inherently
serial fraction. The discipline is to stop sanding a smooth surface and re-measure to find
where the constraint actually went. *follow-up:* What's the first measurement you take?
`perf stat` for IPC and cache-miss/stall rate, `perf c2c` for residual false sharing/HITM,
off-cuprofiles for blocking, and a throughput-vs-thread-count scaling curve to see where
it flattens or declines. *Red flag:* Reaching for a cleverer lock without re-profiling.

**Q Your profile shows low IPC with high memory stalls. What's the bottleneck and why do
more threads make it worse?** A: You're memory-bandwidth bound - threads are all waiting
on the same DRAM channels, so padding and lock tuning can't help a saturated bus, and
adding cores makes contention for those bytes worse, not better. The fixes are to shrink
the working set (compression, smaller types, bit-packing) so it fits in cache, improve
access patterns (sequential over random, SoA over Aos, tiling), and reduce data movement
(in-place compute, spans, move semantics). *follow-up:* How does NUMA change this on a
multi-socket box? Bandwidth is per-socket, so cross-NuMA access runs 1.5-2x slower and
burns interconnect bandwidth; pin threads and use first-touch/local allocation so each
thread hits local DRAM. *Red flag:* Adding threads or micro-tuning locks to fix a
saturated memory bus.

**Q If you're still contention-bound, what's the step-change beyond making sharing
cheaper?** A: Stop sharing at all. Padding and striping make sharing cheaper; the next
level is shared-nothing: shard data by thread so there's zero cross-thread access on
the hot path, use thread-local aggregation with periodic merges, or pin state to a single
owner thread and send it messages over an SPSC queue (the actor/SEDA model behind Redis's
single-threaded core and Seastar). For read-mostly data, RCU lets readers proceed with
zero synchronization. The fastest synchronization is no synchronization. *follow-up:* Why
does shared-nothing scale closer to linearly? If two threads never touch the same cache
line there's no coherence traffic to bottleneck, so the coordination term that caps
scaling goes toward zero. *Red flag:* Proposing yet another lock refinement instead of
eliminating the sharing.

**Q Which "substrate" layers below your code commonly cap throughput, and how do you
attack each?** A: The allocator is a hidden global lock - swap to a sharded one
scheduler/NUMA Layer -pin threads with `pthread_setaffinity_np` and allocate node-local
to stop migrations and cross-socket traffic. The syscall boundary - batch I/O with
`io_uring` to amortize transition and context-switch cost. And thread count - often the
fix is *fewer* threads (one per core) plus async I/O, not more, to avoid context-switch
thrash and cache pollution. *follow-up:* Why can switching the allocator alone unlock
scaling with a one-line change? Because `malloc`/`new` serialize on internal arena locks
under multithreaded churn; a per-thread/per-CU-arena allocator removes that
serialization. *Red flag:* Assuming the ceiling is always in your own code and never in
the runtime substrate.

**Q Cite the two laws that explain why "more tricks" eventually fails, and what each
implies for the fix.** A: Amdahl's Law - the serial fraction caps speedup, so if 5% is
inherently serial you can't exceed 20x; the implication is to find and shrink the serial
fraction (a shared lock, a single queue, an ordering/commit point). The Universal
Scalability Law adds a *coherence* penalty for cross-thread communication and predicts
throughput can actually *decrease* past some thread count; the implication is that you
must *remove* coordination, not cheapen it. A scaling curve that peaks then drops is USL's
coherence term dominating, *follow-up:* Why do segmented locks and padding specifically
hit the US wall? They lower the coherence coefficient but don't eliminate it;
shared-nothing partitioning drives that coefficient toward zero, which is why it's the
real step-change, *Red flag:* Believing more cores always add throughput, ignoring the
serial fraction and coherence term.

**Q When is the concurrency model* itself wrong, and what do you switch top** Ai When
per-core sharded/async still isn't enough, the thread-pool-with-locks model may be
mismatched, Move to thread-per-core + shared-nothing + cooperative async (the
Seastar/DPDK/ScylaDB model); one pinned thread per core, no locks, message-passing
between cores, Combine with batching to amortize per-item overhead, SIND for compute-bound
hot paths, and kernel/hardware offload - eBPF/XDPfor packets, `SO_REUSEPORT` to let the
kernel shard connections across per-core listeners with no user-space load-balancer
contention. *follow-up:* Why does batching often beat any lock trick? It amortizes
per-item fixed costs (locking, syscalls, cache misses) across many items, improving
throughput without touching the synchronization primitive at all. *Red flag:* Keeping a
shared-state thread-pool model and hoping micro-optimizations will close a structural gap.
