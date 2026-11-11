<!-- interview/resource_constrained_cpp_designs.md -->

# Data Structures and Design Patterns for Resource-Constrained C++ Systems

This is a broad and important area for anyone building high-throughput services on remote
Linux servers, where memory bandwidth, cache footprint, and per-connection overhead decide
whether you handle 10k or 10M connections on the same box. The unifying theme is: **trade
a general-purpose abstraction for a specialized structure whose memory layout and access
pattern match the hardware and the problem's constraints.**

## The organizing principle

Every technique below is an answer to one of three scarcities:

- **Memory / space** - fit more state in RAM, or fit the working set in cache.
- **Time/ CPU** - turn O(n) or O(log n) work into O(1), or eliminate allocation and
  syscalls from the hot path.
- **Allocation churn / fragmentation** - avoid the general allocator, which is slow,
  non-deterministic, and fragments over a long-running server's 1ifetime.

A senior engineer picks the structure by first naming which scarcity dominates. Bitmaps
and timing wheels sit in different families: bitmaps attack space, timing wheels attack
time for a specific access pattern.

## 1. Bit-packing and bitmaps (space)

The idea is to use individual bits instead of bytes/words to represent state.

- **Bitmap / bitset** - one bit per element for set membership or boolean flags.
  `std::vector<bool>>` (a specialized bit-packed container) or `std::bitset<N>` for
  compile-time sizes. Tracking which of 1M fds are active costs 128 KB instead of 1M
  bytes, and fits in L2.
- **Bit fields in structs** - pack several small integer fields into one word:
  `struct { uint32_t type:4; uint32_t flags:8; uint32_t len:20; };`. Shrinks per-object
  footprint, critical when you have millions of objects.
- **Roaring bitmaps**-compressed bitmaps that adapt between array, bitmap, and
  run-length representations per chunk. The standard choice when the set is large but
  sparse or clustered; used heavily in databases and search engines.
- **Bloom filters** - probabilistic set membership with no false negatives and tunable
  false positives, in a fraction of the space of storing the elements. Ideal as a cheap
  re-filter to avoid expensive lookups (disk, network, a big hash table)
- **Succinct / rank-select structures** - store data near its information-theoretic
  minimum while still supporting fast queries. Advanced, used in text indexes and huge
  static dataset.

Bitmaps trade CPU (bit manipulation, popcount) for dramatic space savings, and the space
savings usually pay back as cache residency.

## 2. Timing wheels and timer management (time, for scheduled events)

The problem: a server with millions of connections needs a per-connection timeout (idle
reaper, retransmit, keepalive). A naive min-heap of timers is o(log n) per insert/expire
and scatters memory.

- **Timing wheel** - a circular array of buckets, each holding the timers due in that
  tick. Advancing the clock is O(1): you just move to the next bucket and fire its list.
  Insertion is O(1). This is how the Linux kernel, nginx, and most high-scale event loops
  manage timeouts.
- **Hierarchical timing wheels** - multiple wheels at different resolutions (like clock
  hands: seconds, minutes, hours) to cover a large time range without a huge single array.
  O(1) amortized across a wide range of timeout values.
- **When a heap still wins:** if you have relatively few timers or need exact ordering and
  arbitrary far-future times, a `std::priority_queue`-based timer is simpler and fine. The
  wheel's payoff is at high timer counts with coarse resolution tolerances - exactly the
  gateway case.

The key insight: a timing wheel trades a little precision (bucket granularity) and some
space (the bucket array) for O(1) operations and cache-friendly batch expiry.

# 3. Custom allocators and memory pools (allocation churn)

The general allocator (`malloc`/`new`) is a hidden enemy in long-running servers: it takes
locks, has unpredictable latency, and fragments over time. The fixes:

- **Object pool/ free list** - pre-allocate a slab of fixed-size objects, hand them out
  and return them to a free list, O(1) alloc/free, zero fragmentation, no syscalls on the
  hot path, Perfect for connection objects, buffers, request contexts.
- **Arena / bump allocator** - allocateby advancing a pointer; free everything at once by
  resetting the pointer, Ideal for per-request scratch memory where all allocations share
  a lifetime, `std::pmr::monotonic_buffer_resource` gives this in standard C++.
- **slab allocator** - pools segregated by object size class (the kernel's approach).
  Reduces fragmentation and improves cache locality by keeping same-type objects together.
- **`std::pmr` (polymorphic memory resources, C++17)** - lets you plug arena/pool
  allocators into standard containers without changing their type. The modern, idiomatic
  way to do custom allocation in C++.
- **Small-buffer optimization (SBO)** - store small payloads inline in the object to avoid
  heap allocation entirely (how most `std::string` 1mplementatlons handle short strings;
  `std::function` does similar). You can build your own SO types for small
  vectors/strings.

These trade some up-front memory reservation and code complexity for deterministic latency
and no fragmentation - essential for tail-latency-sensitive services.

## 4. Ring buffers / circular buffers (bounded, allocation-free queues)

A fixed-size buffer with head/tail indices that wrap around. Central to high-performance
I/O:

- **SPSC(single-producer, single-consumer) lock-free ring** - the fastest inter-thread
  handoff, using atomics with acquire/release ordering, no locks. The backbone
  of low-latency pipelines.
- **MPMC rings** and the **LMAX Disruptor** pattern - batched, cache-line-aware ring
  buffers for very high throughput.
- **`io_uring`** submission/completion queues are ring buffers shared with the kernel -
  the modern Linux async I/O interface, eliminating per-op syscalls.

Trades a fixed capacity (you must handle "full") for zero allocation, predictable memory,
and excellent cache behavior. Pad head/tail to separate cache lines to avoid false
sharing.

## 5. Cache-conscious and compact data structures (cache/space)

When the bottleneck is memory bandwidth and cache misses rather than raw compute:

- **Structure-of-Arrays (SoA) vs Array-of-Structures (Aos)** - store each field in its own
  array so that traversing one field streams contiguously and vectorizes. The single
  biggest data-layout lever in hot loops.
- **Flat / open-addressing hash maps** (e.g., the design behind `absi::flat_hash map`,
  `robin_hood`, `ankerl::unordered_dense`) - store entries inline in one array instead of
  node-per-entry like `std::unordered map`. Far fewer cache misses and allocations; often
  2-5x faster. The default recommendation over `std::unordered map` in
  performance-sensitive code.
- **B-trees /Bt-trees in memory** memory** `absl::btree_map` - higher fan-out than
  red-black trees means shallower trees and fewer cache misses than `std::map`.
- **Judy arrays / radix trees / tries** - cache-efficient associative structures for
  integer or string keys.
- **Intrusive containers** (Boost.Intrusive) - the linkage pointers live inside the
  element, so no separate node allocation and better locality. Great for objects that live
  in multiple lists.
- **Cache-oblivious / van Emde Boas layouts** - advanced tree layouts that are
  cache-efficient at every level of the hierarchy without tuning.

These trade generality and sometimes iterator/reference stability for locality and speed.

# 6. Probabilistic and approximate structures (space, when exactness is negotiable)

When you can tolerate bounded error for a large space win:

- **HyperLogLog** - cardinality (distinct count) estimation of billions of items in
  kilobytes.
- **Count-Min Sketch** - approximate frequency counts in sublinear space; used for
  heavy-hitter detection and rate limiting.
- **Bloom / Cuckoo / Quotient filters** - approximate membership (Cuckoo and Quotient also
  support deletion, unlike a classic Bloom).

The trade is explicit and tunable: a small, controllable error rate for
orders-of-magnitude less memory. Perfect for telemetry, rate limiting, and caching
decisions in a gateway.

## 7. Integer/index handles instead of pointers (space + safety)

On a 64-bit system, pointers are 8 bytes and prevent relocation. Replacing them with small
integer indices into an array:

- Shrinks structures (a `uint32_t` index vs an 8-byte pointer - halves the size of
  link-heavy structures).
- Enables the whole structure to be relocated, serialized, or placed in shared memory /
  `mmap`.
- Improves locality since everything lives in one contiguous array.
- Supports **generational indices** (index + generation counter) for safe handles that
  detect use-after-free - the standard pattern in game engines and ECS systems.

Trades pointer convenience and direct dereference for compactress, relocatability, and
safety.

## 8. Compression and encoding on the wire and at rest (space/bandwidth)

- **Varint/LEB128** - variable-length integer encoding (used by Protobuf), small numbers
  take few bytes.
- **Delta + frame-of-reference encoding** - store differences rather than absolute values
  for sorted or slowly-changing sequences.
- **Bit-packing columnar data** - pack values to the minimum bit width needed.

Trades CPU (encode/decode) for reduced memory and network bandwidth - often a net win
because bandwidth is scarcer than CPU.

## How to choose - the architect's decision framework

1. **Name the dominant scarcity.** Space, time, or allocation churn? Measure first; don't
   guess, `perf stat` for cache misses, a heap profiler for allocation, flame graphs for
   CPU.
2. **Match the access pattern.** Sequential scan - SoA + flat arrays. Point lookups → flat
   hash map. Scheduled expiry + tlming wheel, Membership tests → bitmap/Bloom.
   Producer/consumer handoff → ring buffer.
3. **Prefer contiguous, allocation-free, cache-resident designs by default.** On modern
   hardware, a cache miss (~100+ ns) dwarfs instruction cost, so a "more work but
   cache-friendly" structure usually beats a "less work but pointer-chasing" one.
4. **Trade exactness only when the problem allows it.** Probabilistic structures are
   enormous wins where a bounded error is acceptable - but be explicit about the error
   budget.
5. **Make the trade-off legible.** Every one of these buys a resource by spending another
   (CPU for space, precision for time, memory reservation for determinism). Document which
   scarcity you optimized and what you paid, so the next engineer understands the
   constraint.

The meta-point for a resource-constrained C++ system: **the standard library's
general-purpose containers (`std::map`, `std::unordered_map`, node-based lists,
new/delete) are optimized for correctness and generality, not for the memory hierarchy
or long-running determinism.** The craft of resource-constrained design is knowing when
the general tool's hidden costs - allocation, pointer chasing, fragmentation, cache misses
- dominate, and swapping in a specialized structure whose layout is deliberately shaped to
the hardware and the workload.

_________________________________________________________________________

## Interview Q&A

**Q: How do you decide which specialized data structure to reach for in a
resource-constrained design?** A: First name the dominant scarcity - space, time/CPU, or
allocation churn - and measure it rather than guessing (`perf stat` for cache misses, a
heap profiler for allocation, flame graphs for CPU). Then match the access pattern:
sequential scan → SOA + flat arrays, point lookups → flat hash map, scheduled expiry →
timing wheel, membership tests → bitmap/Bloom, producer/consumer handoff → ring buffer.
Each technique buys one resource by spending another, so make the trade legible.
*Follow-up:* Why name the scarcity before picking a structure? Because bitmaps attack
space while timing wheels attack time for a specific access pattern - optimizing the wrong
scarcity wastes effort and can make things worse. *Red flag:* Jumping to a "fast"
structure without measuring where the actual constraint is.

**Q: Why is a timing wheel preferable to a min-heap for per-connection timeouts at
scale?** A: A min-heap of timers is O(log n) per insert/expire and scatters memory via
pointer chasing. A timing wheel is a circular array of buckets where each tick fires one
bucket's list, making both insertion and advance O(1) and the expiry cache-friendly batch
work - which is why the Linux kernel, nginx, and high-scale event loops use it.
Hierarchical wheels extend this to a wide range of timeout values with multiple
resolutions. *Follow-up:* When does a heap still win? When you have relatively few timers
or need exact ordering and arbitrary far-future times; the wheel's payoff is high timer
counts with coarse-resolution tolerance. *Red flag:* Insisting the heap is always better
because of its clean O(log n) bound, ignoring constant factors and cache behavior.

**Q: Why avoid the general allocator on a long-running server's hot path, and what
replaces it?** A: `malloc`/`new` take locks, have unpredictable latency, and fragment over
a long-running process's lifetime - poison for tail latency. Replace it per lifetime
pattern: an object pool/free list for fixed-size objects (O(1) alloc/free, zero
fragmentation), an arena/bump allocator for per-request scratch that's freed all at once
(`std::pmr::monotonic_buffer_resource`), and a slab allocator for size-segregated objects.
`std::pmr` lets you plug these into standard containers without changing their types.
*Follow-up:* What's the trade-off you're accepting? Up-front memory reservation and some
code complexity in exchange for deterministic latency and no fragmentation. *Red flag:*
Reaching for a custom allocator everywhere without evidence that the general allocator is
actually the bottleneck.

**Q: On modern hardware, why does `absl::flat_hash_map` usually beat `std::unordered map`
in hot code?** A: `std::unordered_map` is node-based - each entry is a separate heap
allocation reached by pointer chasing - so lookups incur cache misses and the map
fragments the heap. Open-addressing flat maps store entries inline in one contiguous
array, so probing streams through cache lines with far fewer misses and allocations,
commonly 2-5x faster. The cost is weaker iterator/reference stability, since inserts can
rehash and relocate entries. *Follow-up:* Why does a cache miss dominate the decision
here? A miss is ~100+ ns versus a few cycles for the extra probe work, so a more work but
cache-resident" structure beats a "less work but pointer-chasing" one. *Red flag:*
Defending `std::unordered_map` purely on O(1) average complexity while ignoring its
per-node allocation and cache-miss cost.

**Q: Walk me through choosing a probabilistic structure, and when it's the wrong call.**
A: Use them when a bounded, tunable error buys orders-of-magnitude less memory:
HyperLoglog for distinct-count estimation of billions of items in kilobytes, Count-Min
Sketch for approximate heavy-hitter/rate-limiting frequencies, and Bloom/Cuckoo/Quotient
filters for approximate membership (Cuckoo and Quotient also support deletion, unlike a
classic Bloom). The error budget must be explicit. They're the wrong call when the problem
demands exactness or when the dataset is small enough that an exact structure already fits
in cache, *Follow-up:* Where does a Bloom filter pay off most in a gateway? As a cheap
pre-filter in front of an expensive lookup (disk, network, a big hash table) - no false
negatives means a "definitely not present" answer safely skips the costly path. *Red
flag:* Using a probabilistic structure where false positives are unacceptable, or not
stating the error rate.

**Q: Why replace pointers with integer indices in link-heavy structures, and what safety
pattern comes with it?** A: On 64-bit, a pointer is 8 bytes and pins the object in place.
A *uint32_t* index into an array halves the size of 11nk-heavy structures, improves
locality by keeping everything contiguous, and - crucially - lets the whole structure be
relocated, serialized, or placed in shared memory/`mmap`, The safety pattern is
generational Indices (index + generation counter), which detect use-after-free when a slot
is reused - the standard approach in game engines and ECS systems, *Follow-up:* What do
you give up by using indices? Direct dereference convenience and a layer of indirection
through the base array -you trade pointer ergonomics for compactness, relocatability, and
safety. *Red flag;* Storing raw pointers in a structure you intend to serialize on map
into another process's address space.

**Q: The standard 11brary containers are optimized for correctness and generality - when
do their hidden costs justify a specialized structure?** A: When allocation, pointer
chasing, fragmentation, or cache misses from `std::map`/`std::unordered_map`/node
lists/`new`/`delete` start dominating a hot path or a long-running server's determinism.
The craft is recognizing that a general tool's hidden costs - not its interface - are the
problem, then swapping in a structure whose layout 1s deliberately shaped to the hardware
and workload, and documenting which scarcity you optimized and what you paid. *Follow-up:*
How do you avoid over-engineering this? Measure first; only specialize the structure the
profile proves is hot, and feep the general container everywhere the hidden costs don't
dominate. *Red flag:* Replacing standard containers wholesale on instinct, adding
complexity with no measured win.
