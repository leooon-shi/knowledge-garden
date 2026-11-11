<!-- interview/zero_copy_small_packets.md -->

# Zero-Copy in Linux: sendfile, splice, MSG_ZEROCOPY - and Why Small Packets Break the Model

The central caveat of zero-copy: **zero-copy is a throughput optimization for large
transfers, and it can actively *hurt* with many small packets.** Understanding when each
mechanism pays off - and when it becomes a net loss - is the real skill.

## What we're actually eliminating

The baseline `read()`/`write()` path for sending a file or buffer over a socket does
multiple copies and context switches:

1. `read()`: kernel copies from page → user buffer (copy 1, plus a syscall).
2. `write()`/`send()`: kernel copies user buffer socket buffer/kernel (copy 2, plus a
   syscall).

So the naive path is **two CPU copies and two syscalls per chunk**, plus the user buffer
occupies cache and memory bandwidth. At 10-100 Gbps that bandwidth is the bottleneck.
Zero-copy exists to remove those CPU copies and, ideally, syscalls.

Critical framing: **zero-copy saves *per-byte* CPU/bandwidth cost. It does nothing to
reduce *per-operation* overhead** (syscall, setup, bookkeeping). That asymmetry is exactly
why small packets defeat it - small packets have few bytes to save but pay the full
per-operation cost.

## 1. sendfile - file → socket, no user space

`sendfile(out_fd, in_fd, offset, count)` sends data directly from a file descriptor
(backed by page cache) to a socket, never entering user space.

- **Copies:** eliminates the two user-space copies. With a **scatter-gather capable NIC**
  (checksum offload + DMA gather), data goes page cache + NIC via DMA with **zero CPU
  copies** - the kernel hands the NIC pointers to the page-cache pages. Without SG
  support, there's still one in-kernel copy to the socket buffer, but user space is
  bypassed.
- **Syscalls:** one syscall moves potentially megabytes.
- **Constraints:** source must be a file (mmap-able / page-cache-backed), destination a
  socket. You cannot transform the data (no user-space encryption/compression) - the bytes
  never come to you. This is why TLS historically defeated `sendfile` until **kTLS**
  (kernel TLS) let the kernel encrypt in-place and preserve the zero-copy path.
- **Sweet spot:** static file serving (nginx `sendfile on;`), large media, backups.
  Large, sequential, untransformed transfers.

## 2. splice - zero-copy between fds via a pipe

`splice(fd_in, fd_out, ...)` moves data between two fds where **at least one is a pipe**,
by moving **page references** through the pipe buffer rather than copying bytes.
`vmsplice` and `tee` are relatives.

- **Mechanism:** the pipe acts as a kernel-internal conduit of page pointers. Data is
  "moved" by reference-counting pages, not mempy. You can build socket → pipe → socket
  (proxying) or file-pipe›socket pipelines with no CPU copy.
- **More general than sendfile:** works between arbitrary fd pairs as long as a pipe is
  the intermediary. This makes it the tool for **zero-copy proxying** in a gateway:
  `splice` from the client socket into a pipe, then `splice` from the pipe into the
  backend socket. The gateway forwards bytes it never touches.
- **Constraints:** the pipe intermediary adds bookkeeping; you still can't inspect/modify
  the payload (unless you `vmsplice` your own pages in). Correct handling of partial
  splices and pipe capacity (default 64 KB, tunable via `fcntl(F_SETPIPE_SZ)`) is fiddly.
- **Sweet spot:** L4 proxies/tunnels, forwarding large streams between sockets without
  payload inspection.

## 3. MSG_ZEROCOPY - zero-copy for ordinary socket sends

`send (fd, buf, len, MSG_ZEROCOPY)` (Linux 4.14+) makes `send()` on a **user buffer*
zero-copy: instead of copying the buffer into the kernel, the kernel **pins the user
pages** and MAs directly from them.

- **The catch - asynchronous completion.** Because the kernel now shares your pages, you
  **cannot reuse or free the buffer until the kernel is done transmitting** (including
  possible TCP retransmits), Completion is signaled asynchronously via the socket's
  **error queue** (`MSG_ERRQUEUE`), which you must `recvmsg()` to reap. This adds a
  notification/lifecycle burden: track in-flight buffers and drain a second event source.
- **Copies:** eliminates the user→kernel copy for large sends.
- **Constraints & cost:** page pinning, page-table manipulation, and the
  completion-notification round trip have **real fixed overhead**, The kernel
  documentation states MSG_ZEROCOPY is beneficial only for **writes over ~10 KB** roughly;
  below that, pinning + notification cost exceeds the copy it saves.
- **Sweet spot:** applications sending large buffers they generate in user space (not from
  a f1le) - e.g., a storage server streaming large objects, RPC of big payloads.

## The small-packet determination - the core of the question

Why many small packets make zero-copy a bad or losing trade, and how to decide.

Zero-copy's benefit is proportional to bytes saved from copying; its cost is largely
**fixed per operation**, For a small packet:

- **The per-byte savings are tiny** - copying 280 bytes stays in L1 and 1s a handful of
  nanoseconds.
- **The fixed costs dominate and can *exceed* the copy:**
- **Page pinning / unpinning and page-table walks** (MSG_ZEROCOPY) cost more than
  copying a few hundred bytes.
- **The completion notification** via the error queue is an *extra* per-send bookkeeping
  event - you've turned one cheap copy into an async lifecycle with a second syscall to
  reap it.
- **Page-reference management** (splice) and **SG descriptor setup** (sendfile) have
  setup cost amortized only over large payloads.
- **Cache effects:** pinning and metadata touch more cache lines than a small memcpy
  would.

So for small packets you get **negative return**: you pay pinning + notification overhead
to avoid a copy that was nearly free. This is why the kernel sets practical thresholds
(~10 KB for MSG_ZEROCOPY) and why nginx applies "sendfile to files but not to tiny
responses.

A second, deeper small-packet problem is really about **per-packet overhead**:

- **Syscall-per-packet dominates.** With many small sends, the syscall count - not the
  copy - is the bottleneck. Zero-copy doesn't reduce syscalls; it may *increase* them
  (completion reaping). The right lever for small packets is the opposite discipline:
  **batch and coalesce**, not zero-copy.
  - **Nagle's algorithm (TCP_NODELAY off)** coalesces small writes into fewer segments -
    but adds latency.
  - **`TCP_CORK`/`MSG_MORE`** deliberately hold small writes to build full segments
    before transmit - great for combining a header + body.
  - **`sendmmsg`/`recvmsg`** send/receive multiple* datagrams in one syscall - the
    correct tool for many small UP packets.
  - **GSO/GRO** and **TSO** let the stack/NIC handle segmentation so the CPU processes
    fewer, larger units.
  - **io_uring** batches many small operations into one submission, attacking the per-op
    syscall cost zero-copy ignores.
 
The determination is a two-axis decision:

|                       | Large payloads                           | Many small packets                                      |
| --------------------- | ---------------------------------------- | ------------------------------------------------------- |
| **Dominant cost**     | per-byte copy + memory bandwidth         | per-operation syscall + fixed overhead                  |
| **Right tool**        | zero-copy (sendfile/splice/MSG_ZEROCOPY) | batching/coalescing (TCP_CORK, sendnasg, GSO, io_uring) |
| **Zero-copy verdict** | big win                                  | neutral-to-harmful                                      |

## How to actually decide in production

1. **Measure the payload size distribution.** If p50 payload is a few hundred bytes,
   zero-copy is the wrong optimization -attack syscall count and coalescing instead.
2. **Set a size threshold.** Use a copy-based `send` below ~10-16 KB and switch to
   zero-copy above it (a common hybrid). The threshold is empirical - benchmark on your
   hardware; it depends on memory bandwidth, NIC offload, and CPU.
3. **Profile the real bottleneck.** `perf stat` for cache/bandwidth (zero-copy helps if
   memory-bound), syscall count via `strace -c` or `perf trace` (batching helps if
   syscall-bound).
4. **Watch the hidden costs of MSG ZEROCOPY:** buffer lifetime bugs (reusing a buffer
   still in flight is data corruption), error-queue draining, and complexity. Adopt only
   when payloads justify it.
5. **Prefer the simplest mechanism that fits:** file-socket → `sendfile`; socket→socket
   proxy → `splice`; user-generated large buffers → `MSG_ZEROCOPY`; many small messages →
   `sendmmsg`/`io_uring`/cork, *not* zero-copy.

## Interview Q&A

**Q: What exactly does "zero-copy" eliminate, and what does it not touch?** A: It
eliminates the per-byte CPU copies (page cache → user buffer → socket buffer) and the
memory-bandwidth waste they cause. It does nothing for per-operation overhead - the
syscall transition, setup, and bookkeeping are unchanged. That asymmetry is the whole
story: zero-copy wins when bytes dominate and loses when operations dominate. *Follow-up:*
So why can it make small-packet workloads slower? Because there are almost no bytes to
save, yet you still pay the fixed per-op cost, and mechanisms like `MSG_ZEROCOPY` even add
a completion-notification operation. *Red flag:* Believing zero-copy reduces syscall count
or helps regardless of payload size.

**Q: Walk me through `sendfile` and when it truly achieves zero copies.** A:
`sendfile(out_fd, in_fd, offset, count)` sends page-cache-backed file data directly to a
socket without entering user space. With a scatter-gather-capable NIC (checksum offload +
DMA gather), the kernel hands the NIC pointers to the page-cache pages and there are zero
CPU copies; without 5G it still does one in-kernel copy but bypasses user space. The
constraint is you can't transform the bytes, which is why TLS defeated It until kTLS.
*Follow-up:* How did kTL restore the zero-copy path for HTTPS? The kernel encrypts in
place on the send path, so sendfile data can be encrypted without ever being copted to
user space, *Red flag;* Claiming " sendfile can compress or encrypt the data in user
space.

**Q: Your gateway proxies bytes between two sockets without inspecting them, Which
zero-copy primitive fits and why?** A: `splice`, because it moves page references through
a pipe intermediary rather than copying bytes, and unlike `sendfile` it works between
arbitrary fd pairs as long as a pipe is involved, You build socket → pipe → socket,
forwarding bytes you never touch, Watch pipe capacity (default 64 KB, tunable via
`fcnt1(F_SETPIPE_SZ)`) and handle partial splices. *Follow-up:* What breaks if the gateway
needs to inspect or rewrite the payload? `splice` no longer applies since you never see
Follow-upthe bytes; you'd have to copy into user space (on `vmsplice` your own pages), losing the
zero-copy benefit. *Red flag:* Reaching for `sendfile` for socket-to-socket forwarding -
it requires a file source.

**Q: What's the hidden lifecycle hazard of `MSG_ZEROCOPY` ?** A: The kernel pins and DMAS
directly from your user pages, so you must not reuse or free the buffer until the kernel
finishes transmitting - including TCP retransmits. Completion is signaled asynchronously
on the socket error queue, which you reap with `recvmg(.., MSG_ERRQUEUE)`. Reusing a
buffer still in flight is silent data corruption, and forgetting to drain the error queue
leaks tracking state. *Follow-up:* What's the rough payload threshold where it pays off?
The kernel docs put it around writes over ~10 KB; below that, pinning plus notifleatson
cost exceeds the copy saved. *Red flag:* Treating "MSG_ZEROCOPY" as a synchronous drop-in
for `send` where the buffer is free to reuse immediately.

**Q: A service sends mostly a few-hundred-byte messages and someone proposes
`MSG_ZEROCOPY` to speed it up. How do you respond, and how do you confirm the real
bottleneck?** A: Push back-small packets are operation-bound, not bandwidth-bound, so
zero-copy is neutral-to-harmful here; the copy is nearly free while pinning and
error-queue reaping add cost. The right levers are batching/coalescing:
`TCP_CORK`/`MSG_MORE`, `sendmmsg`, GSO/TSO, or `io_uring` Confirm by profiling:
`perf stat` for cache/bandwidth (zero-copy only helps if memory-bound) and `strace -c` or
`perf trace` for syscall counts (batching helps if syscall-bound). Measure the
payload-size distribution first. *Follow-up:* What single tool best amortizes per-op cost
for many small UDP datagrams? `sendmmsg`/`recvmmsg` - multiple datagrams per syscall;
`io_uring` is the more general batching answer. *Red flag:* Optimizing copies before
checking whether the workload is operation-bound or bandwidth-bound.

**Q: Design-wise, how do you decide between a copy-based send and zero-copy in one
codebase?** A: Make it a two-axis, empirical decision: measure the payload distribution,
then set a size threshold - copy-based `send` below roughly 10-16 KB, zero-copy above -
because the crossover depends on memory bandwidth, NIC offload, and CPU. Prefer the
simplest mechanism that fits the source: file-socket uses `sendfile`, socket-socket proxy
uses `splice`, large user-generated buffers use `MSG_ZEROCOPY`, many small messages use
`send`/`io_uring`/cork. *Follow-up:* Why is the threshold hardware-specific rather
than a fixed constant? Because it's the point where fixed per-op cost equals per-byte copy
cost, which shifts with memory bandwidth and NIC capabilities - so benchmark on the target
box. *Red flag:* Picking one mechanism globally without measuring payload sizes or
benchmarking the crossover.

## Synthesis

The three mechanisms attack the same enemy - CUcopies and memory-bandwidth waste on the
data path: **`sendfile`** for file-socket, **`splice`** for zero-copy movement between fds
via a pipe (the proxy tool), and **`MSG_ZEROCOPY`** for large user-buffer sends at the
price of asynchronous, error-queue-based completion and page pinning. But all three
optimize the *per-byte* cost and leave *per-operation* cost untouched - some even add it.
With many small packets there are almost no bytes to save and the fixed
pinning/notification/setup overhead dominates, so zero-copy turns neutral or negative. The
correct instinct for small packets is the mirror image: **stop optimizing copies and start
amortizing operations** - coalesce with `TCP_CORK`/`MSG_MORE`, batch with
`sendmmsg`/`io_uring`, and offload segmentation with GSO/TSO. The architect's rule of
thumb: **zero-copy is a large-transfer, bandwidth-bound optimization; small-packet
workloads are operation-bound and want batching, not zero-copy. Measure the payload
distribution first, then choose the axis.**
