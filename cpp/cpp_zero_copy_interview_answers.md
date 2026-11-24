<!-- cpp/cpp_zero_copy_interview_answers.md -->

# Zero-copy interview answers

Answers to the example questions in
[`cpp_zero_copy_interview.md`](cpp_zero_copy_interview.md) (`Q1`-`Q15`). Link to an answer
by its anchor, for example `#q1`.

Try the questions first, then compare.

## Zero-copy interview questions

#### Q1

**What problem does zero-copy solve?**

Zero-copy eliminates redundant copies of data as it moves between buffers or between user
space and the kernel, which wastes CU cycles, memory bandwidth, and cache. A traditional
`read()`-then-`write()` of a file to a socket copies the data twice through user space
(kernel->user, user->kernel); zero-copy keeps the data on a kernel/DMA path so the CPU
never touches the payload. The payoff is higher throughput and lower CPU and cache
pressure, especially for large transfers.

#### Q2

**Does move semantics guarantee zero-copy?**

No. `std::move` only casts to an rvalue; a move happens only if a move
constructor/assignment is actually selected, and even then "move" means transferring
ownership of a resource (e.g. a heap pointer), not a guarantee that no bytes are copied.
Types with small-buffer optimization (short `std::string`) or inline storage
(`std::array`, trivially-copyable structs) are physically copied on a move. Move avoids
deep copies of owned buffers; it does not avoid all physical copying.

#### Q3

**What is the difference between avoiding a user-space copy and avoiding all copies?**

Avoiding a user-space copy means the payload is not bounced through an application buffer
(e.g. `sendfile` keeps it kernel-side), but the kernel or DMA engine may still move it
between page cache, socket buffers, and the NIC. Avoiding all copies means the exact same
physical bytes are handed all the way to the device, typically via DMA from pinned pages.
Most "zero-copy" APIs only remove the user-space copy and some kernel copies, not
literally every byte movement.

#### Q4

**How do `sendfile()`, `splice()`, and `MSG_ZEROCOPY` differ?**

`sendfile()` copies from a file descriptor (page cache) to a socket entirely inside the
kernel, with no user buffer, but is limited to file->socket. `splice()` moves data between
two file descriptors through a kernel pipe, so it is more general (e.g. pipel->socket,
filek->pipe) but still needs a pipe as the conduit. `MSG_ZEROCOPY` is a `send()` flag
that lets the kernel DMA directly from pinned user pages; the send returns before the data
is consumed, so the kernel posts a completion notification on the socket's error queue
(`MSG_ERRQUEUE`), and it is generally only worth it for payloads above roughly 10 KB
because of the pinning/notification overhead.

#### Q5

**What are the buffer-lifetime requirements for asynchronous zero-copy I/O?**

The buffer must remain valid and unmodified from submission until the kernel/device
signals it is done reading it, not merely until the call returns. With `MSG_ZEROCOPY` that
means waiting for the completion notification on the error queue before reusing or freeing
the buffer; with `io_uring` it means waiting for the completion queue entry (or using
registered buffers). Reusing the buffer early is a data-corruption bug because you may
overwrite bytes the NIC has not yet transmitted.

##### Q6

**why can zero-copy increase memory-management complexity?**

Because ownership and lifetime become asynchronous: the producer can no longer assume the
buffer is free when the submit call returns, so it needs completion tracking, reference
counting, or buffer pools to know when reuse is safe, Shared buffers across multiple
in-flight operations or consumers require careful coordination and often immutability
after publication. This replaces a simple copy-and-forget model with explicit lifetime and
concurrency management.

#### Q7

**How can zero-copy affect CPU caches and memory bandwidth?**

Avoiding copies means fewer CPU load/store operations and far less data pulled through the
cache hierarchy, reducing cache pollution and memory-bandwidth pressure, which usually
improves throughput. On the other hand, DMA into memory can invalidate cache lines, and
sharing buffers across cores can introduce cache-line contention or false sharing.
Alignment and NUMA placement of the buffer also matter for the actual benefit.

#### Q8

**What is the role of DMA in zero-copy networking?**

DMA (direct memory access) lets the NIC read the payload directly from RAM without the CPU
copying it, which is what makes true zero-copy transmit possible. The pages holding the
data must stay resident (pinned) and the NIC is given their physical addresses, often via
scatter/gather descriptors. This offloads the data movement from the CPU to the device's
DMA engine, freeing CPUcycles and bandwidth.

#### Q9

**Why does zero-copy from GPU memory require special hardware or driver support?**

Normal Linux zero-copy APIs operate on host RAM pages; GPU VRAM is a separate address
space that the NIC cannot DMA from without an explicit path. Sending GPU data to the NIC
without staging through host memory needs technology like GPUDirect RDMA, where the NIC
and GPU, with driver/framework cooperation, expose and DMA across each other's memory.
Without that hardware/driver support the data must first be copied GPU->host, which
defeats the zero-copy goal.

#### Q10

**When can a normal copy outperform a zero-copy design?**

For small payloads, the fixed overheads of zero-copy (page pinning, setting up DMA
descriptors, completion notifications, syscalls) often exceed the cost of a quick `memcpy`
that stays hot in cache; this is why `MSG_ZEROCOPY` is typically only worthwhile above ~10
KB. A plain copy also avoids the lifetime/synchronization complexity and keeps the source
buffer immediately reusable. If buffers are tiny, frequent, or reused instantly, copying
is simpler and can be faster.

#### Q11

**How would you design a reusable buffer pool for zero-copy packet forwarding?**

Prealocate a fixed set of aligned, page-pinned (or hugepage/NUMA-local) buffers and
recycle them instead of allocating per packet, so the hot path does no allocation. Each
buffer carries state (free / in-flight/ready) and is only returned to the free list
after the completion signal confirms the NIC/async op is done with it. Make the pool
lock-free or per-core to avoid contention, size it for the maximum in-flight depth, and
use reference counts if a buffer can be forwarded to multiple outputs.

#### Q12

**How would multiple consumers safely share a zero-copy buffer?**

Treat the buffer as immutable after publication and give it a reference count (or an
epoch/hazard-pointer scheme) so each consumer holds a reference while reading and the
buffer is reclaimed only when the count drops to zero. No consumer may mutate shared data;
if one needs to modify, it should copy-on-write its own region. Reclamation must be
deferred until every consumer and every in-flight DMA has finished, which is where atomics
and careful ordering come in.

#### Q13

**How would you detect when a NIC or asynchronous I/O operation has finished using a
buffer?**

Use the API's completion mechanism rather than guessing: `MSG_ZEROCOPY` posts a completion
with a sequence number on the socket's error queue (`MSG_ERRQUEUE`), `io_uring` posts a
completion queue entry (CQE), and DPDK-style drivers let you reclaim descriptors after the
NIC advances its TX ring. Only after the matching completion is observed may the buffer be
reused or freed. Tying each completion back to its buffer (via a token/user-data field) is
how the pool knows which buffer became free.

#### Q14

**What are the effects of NuMA locality on zero-copy networking?**

Zero-copy keeps data in place, so where that place is matters: if the buffer, the NIC's
DMA, and the processing thread are on different NUMA nodes, every access crosses the
interconnect and adds latency and cross-node bandwidth pressure, Best practice is to
allocate buffers on the NUMA node local to the NIC and pin the handling thread/IRQs to
that node's cores. Getting NUMA placement wrong can erase the gains of avoiding the copy.

#### Q15

**How would you benchmark a zero-copy path fairly against a copying path?**

Compare them under identical, realistic workloads (same payload sizes, message rates,
concurrency, and hardware) and measure not just throughput but also CPU utilization, cache
misses, and tail latency, since zero-copy often wins on CPU/bandwidth more than on raw
latency. Sweep the payload size to find the crossover point where zero-copy starts paying
off (copying usually wins for small messages). Warm up, pin threads and interrupts,
control NUMA placement, run long enough to be steady-state, and account for the zero-copy
completion-handling cost so the comparison is apples-to-apples.
