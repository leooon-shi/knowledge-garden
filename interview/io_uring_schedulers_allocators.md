<!-- interview/io_uring_schedulers_allocators.md -->

# epoll Implicit Costs, io_uring, Scheduling Layers, and Allocator Internals (jemalloc TS vs tomalloc)

This topic packs several distinct subjects: (1) epoll's hidden/implicit costs, (2) what
`io_uring` brings and the costs it eliminates, (3) the scheduling layers involved in I/O,
and (4) allocator fast paths - jemalloc's thread-local (TLS) caches vs tomalloc's
restartable sequences (RSEQ).

## 1. epoll's implicit costs (the hidden overheads)

epoll is efficient compared to select/poll, but it is not free. The implicit costs
that people forget:

1. **Per-operation syscall overhead.** Even with a ready fd, you still call `epoll_wait`
   and then a separate `read`/`write`/`recv`/`send` syscall per fd. Each syscall is a
   user↔kernel transition: register save/restore, a possible pipeline flush, and - since
   Spectre/Meltdown mitigations (KPTI, retpolines) - a page-table switch and speculation
   barriers that made syscalls markedly more expensive than they used to be. At millions
   of ops/sec, this transition cost dominates.

2. **Two-step readiness model,** epoll is a *readiness* interface, not a *completion*
   interface. It tells you "the fd is ready," then you must issue a second syscall to
   actually move the data. That's inherently at least two syscalls per I/O. This is the
   structural limitation `io_uring` removes.

3. **Data copy across the boundary.** `read`/`write` copy bytes between kernel socket
   buffers and user buffers. That copy costs memory bandwidth and pollutes cache.

4. **`epoll_ctl` registration churn.** Adding/modifying/removing fds each cost a syscall
   and a manipulation of the kernel's interest set (an RB-tree internally). Workloads with
   short-lived connections pay this add/del cost constantly.

5. **Thundering herd / wakeup amplification.** Multiple threads or processes sharing an
   epoll fd (or an fd in multiple epolls) can all wake for one event. `EPOL LEXCLUSIVE`
   mitigates it, but the coordination and spurious wakeups are a real, often-overlooked
   cost.

Plus the ET-specific cost: ET shifts the drain-to-`EAGAIN` obligation to you, trading
correctness risk for fewer wakeups. The key point: the "LT vs ET" axis is only *one* of
these dimensions - syscalls, copies, registration, and wakeups all matter independently,
so the trade is *not* just level-vs-edge.

## 2. What io_uring brings, and the costs it saves

`io_uring` is a fundamentally different model: **shared-memory ring buffers between user
space and kernel** (a submission queue S0 and completion queue ce), and it's a
*completion* interface, not a readiness one.

What it saves relative to epoll:

- **Syscall amortization / elimination.** You batch many operations into the SQ and submit
  them with a single `io_uring_enter` - or, in **polled mode (`SQPOLL`)**, a dedicated
  kernel thread drains the SQ so you make *zero* syscalls on the hot path; you just write
  to the ring and the kernel picks it up. This directly removes cost #1 and #2 above.
- **Completion, not readiness.** You submit the actual `read`/`accept`/`send`; the kernel
  performs it and posts the result to the CQ. No separate "ready then read" round trip -
  the two-step model collapses to one.
- **Fixed buffers and fixed files.** Pre-register buffers (`IORING_REGISTER_BUFFERS`) and
  fds so the kernel skips per-op setup (page pinning, fd table lookups) each time -
  reduces cost #4.
- **Zero-copy paths.** `IORING_OP_SEND_ZC` and zero-copy receive avoid the user↔kernel
  data copy (cost #3), saving memory bandwidth.
- **Linked and batched ops.** Chain dependent operations (e.g., accept → read → write) in
  the kernel without returning to user space between them.
- **Universality.** It works for storage *and* network (and more), unlike epoll which is
  readiness for sockets/pipes; true async disk I/O was never really possible with epoll.

The costs/trade-offs `io_uring`" introduces (the honest other side):

- **Complexity** - ring management, submission/completion accounting, lifetime of buffers
  in flight, and harder debugging.
- **Security surface** - it's been a notable source of kernel CVEs; some environments
  (certain container/gVisor/hardened setups) disable it.
- **Kernel version dependence** - features arrived incrementally (5.1 through 6.x); you
  must feature-detect,
- **SQPOLL burns a core** - the polling kernel thread trades a CPU for syscall
  elimination, only worth it at high I/O rates.

Net: `io_uring` converts "N events × 2 syscalls × 1 copy" into "batched/zero-syscall
submission completion, optionally zero-copy," at the price of complexity and a larger
trust surface.

## 3. How many scheduling layers are involved

I/O throughput isn't governed by one scheduler; there's a stack of them:

1. **CPU / task scheduler (CFS, or EEVD on newer kernels)** - decides which thread runs
   on which core, and handles wakeups when I/O completes. Context switches and migrations
   here directly cost cache warmth.
2. **Softirq/NAPI(network receive) layer** - the NIC driver's interrupt handling and
   polling. Under load NAPI switches from interrupt-per-packet to polling to avoid
   interrupt storms; effectively a scheduler for packet processing.
3. **Block I/O scheduler** (for storage: `mq-deadline`, `bfq`, `none`/`noop` for NVMe) -
   orders and merges disk requests.
4. **The `io_uring` SQPOLL kernel thread**, when enabled - its own scheduling entity
   draining submissions.
5. **Your application's event loop / user-space scheduler** - how you dispatch ready
   events or completions to worker logic (and any green-thread/coroutine runtime on top).

A single network request can pass through NIC interrupt → softirq/NAPI → wakeup via the
CPU scheduler → your event loop → possibly a coroutine scheduler. The point: optimizing
only the epoll-vs-io_uring layer ignores four other schedulers that can be the real
bottleneck (e.g., IRQ affinity and NAPI tuning, or CFS migrating your thread off its warm
core). This is why NUMA/IRQ pinning and `SO_REUSEPORT` (which lets the kernel shard
accepts across per-core listeners, sidestepping a shared-queue scheduler) matter so much.

## 4. Allocators: jemalloc TS caches vs temalloc RSEQ per-CPU caches

Allocators solve the same "keep the hot path free of shared state" problem for memory. In
short:

- **jemalloc**binds each thread to an arena and a **per-thread tcache** via TLS, so the
  fast path is a lock-free, atomic-free thread-local pop/push. Caches scale with *thread*
  count.
- **tcmalloc** uses **per-CPU caches** made safe by **restartable sequences (RSEQ)**: the
  kernel aborts and restarts the critical section if the thread is preempted or migrated,
  so the fast path is plain non-atomic memory ops. Caches scale with *core* count.
  Requires kernel REQ (4.18+); note glibe itself registers REQ since **2.35**, which
  tomalloc must coexist with.

Assembly-level fast paths, slow paths, memory return to the os, and the "which is faster"
verdict are covered in `tcmalloc-rseq-vs-jemalloc-tls.md`.

## The through-Line

Every item here is the same architectural theme viewed from different layers: **eliminate
the per-operation cost at the boundary.** epoll's implicit costs are syscalls, copies, the
two-step readiness model, registration churn, and wakeup amplification. `io_uring`" removes
the syscall/round-trip/copy costs by moving to shared-memory completion rings, at the
price of complexity and trust surface. Between the NIC and your logic sit five schedulers,
any of which can be the real limiter. And the allocators solve the *same* problem for
memory: jemalloc via TLS-isolated per-thread caches, tcmalloc via RSEQ-protected per-CPU
caches - both malcing the fast path lock-free and atomic-free. The unifying principle
across I/O and memory alike: **push work off the hot path, and where you can't, make the
hot path touch nothing shared.**

______________________________________________________________________

## Interview Q&A

**Q: epoll is "efficient," so what are its hidden costs?w* A: Even with a ready fd, epoll
is a *readiness* interface: you call `epoll wait` and then a separate* read / write
syscall per fd, so it's inherently at least two syscalls per I/O, each a userekernel
transition made markedly more expensive by KPTI/retpoline mitigations. Add the per-byte
copy across the boundary, `epoll_ctl` registration churn on short-lived connections, and
thundering-herd wakeups when multiple threads share an epoll fd. The LT-vs-ET axis is only
one of these dimensions. *Follow-up:* Which of those costs does `io_uring` structurally
remove? The two-step readiness model and the syscall-per-op - it's a completion interface
with shared-memory rings, so submission can be batched or (with S0POLL) zero-syscall. *Red
flag:* Treating epoll as "free once the fd is ready."

**Q: What does `io_uring` fundamentally change versus epoll, and what does it cost you?e*
A: It's a completion model built on shared-menory submission and completion ring buffers:
you submit the actual operation and the kernel posts the result, collapsing "ready then
read" into one step. It amortizes or eliminates syscalls (batched `io_uring_enter`, or
zero on the hot path with SQPOLL), supports fixed buffers/files, zero-copy sends, and
1inked ops, and works for storage and* network. The costs are real: ring-management
complexity, a larger kernel attack surface (a notable source of CVEs, disabled in some
hardened/ container environments), kernel-version feature dependence, and SQPOLL burning a
core. *Follow-up:* When is SQPOLL actually worth dedicating a core? Only at high sustained
I/O rates where eliminating the submit syscall outweighs losing a CPU to the polling
kernel thread. *Red flag:* Presenting `io_uring` as strictly better with no security or
complexity trade-off.

**Q: A single network request touches how many schedulers before your handler runs?** A:
Several - optimizing only epoll-vs-lo_uring ignores the rest. The stack is: NIC
softirg/NAPI (polling vs interrupt-per-packet), the CPU task scheduler (CFS, or EEVOF on
newer kernels) handling the wakeup and possibly migrating your thread off its warm core,
optionally the `io_uring` SQPOLL kernel thread, the block I/O scheduler for storage, and
finally your application event loop and any coroutine runtime. Any of these can be the
real bottleneck, *Follow-up:* Give a concrete tuning lever at the NAPI/IRQ Layer. IRQ
affinity pinning plus `SO_REUSEPORT` to shard accepts across per-core listeners, avoiding
a shared-queue scheduler and keeping processing on the core that took the interrupt. *Red
flag:* Assuming I/O throughput is governed by one scheduler.

**Q: Compare jemalloc's TS caches with tomalloc's REQ per-PU caches at the hot-path
level,"* A: jemalloc binds each thread to an arena and a per-thread tache via TLS, so the
fast path is a lock-free, atomic-free thread-local pop/push - caches scale with thread
count, tomalloc uses per-PU caches made safe by restartable sequences (RSEQ): the kernel
aborts and restarts the critical section on preemption or migration, so the commit is a
plain non-atomic store instead of a `lock cmpchg` - caches scale with core count. Neither
uses atomics or barriers on the uncontended fast path. *Follow-up:* What kernel/glibc
acts matter for REQ? REQ needs kernel 24.18, and glibc registers REQ itself since
2.35, which tomalloc must coexist with rather than register independently. *Red flag:*
claiming RSEQ makes each allocation dramatically cheaper in raw cycles - the real win is
bounded footprint and cache behavior at scale (see `tcmalloc-rseq-vs-jemalloc-tis.md`).

**Q: You're handling ml11ions of small I/O ops/sec on an epoll gateway and the CPU is
pegged in kernel mode. What do you check and what do you try?** A: Measure the syscall
boundary first: `strace -c -p <pid>` or `perf trace` to count `epoll_wait`/`read`/`write`
per request, and `perf stat` for the userkernel transition cost and cache effects. If
syscalls dominate, the structural fix is to batch - move to `io_uring` to amortize
submission, or at minimum coalesce ops - and to shard with `SO_REUSEPORT` Plus IRQ
affinity so each core handles its own connections without cross-core wakeups. Verify the
scheduler isn't migrating threads with "perf sched. *Follow-up:* Why won't adding more
worker threads help here? The bottleneck is per-op syscall/transition cost and possibly a
saturated softirg path, not CPU-bound user work - more threads add context-switch and
cache-pollution overhead. *Red flag:* Jumping to `io_uring` without first confirming the
syscall boundary is actually the limiter.

**Q: Why does the doc frame epoll costs, iouring, schedulers, and allocators as the same
problem?** A: They're all "eliminate the per-operation cost at the boundary," viewed at
different layers. epoll's costs are syscalls, copies, the two-step model, registration
churn, and wakeup amplification; `io_uring` moves to shared-memory completion rings to
remove the syscall/round-trip/copy; five schedulers sit between NIC and logic; and
allocators solve the identical problem for memory - jemalloc via TLS-isolated per-thread
caches, tomalloc via REQ-protected per-CPU caches. The unifying principle is push work off
the hot path, and where you can't, make the hot path touch nothing shared.
*Follow-up:* State the one-line through-line. Push work off the hot path; where you can't,
make the hot path touch nothing shared. *Red flag:* Treating these as four unrelated
topics instead of one boundary-cost theme.
