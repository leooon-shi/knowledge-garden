dr
Koperformance/network/c16k_c10_probLem.ma -
#The C10K and C10M Problems
How to serve very large numbers of concurrent connections, and where the bottleneck moves as concurrency grows. This note is the counterpart to
*network_io model.md"- read that first for the event-notification and worker-model fundamentals.
#* C10K: 10,000 concurrent connections
The "CLeK" problem (Dan Kegel, 1999) asks how one server handles 10,000 simultaneous connections. At this scale the limiting factors are the connection model and the per-connection cost of the kernel's 1/0 interfaces, not raw bandwidth.
##* Thread-per-connection vs, event loop
-  **Thread-(or process-) per-connection. * One thread blocks on each connection's I/0. Simple to write, but 10,000 threads mean large memory (each thread stack is typically 1-8 MB of virtual address space) and heavy scheduler/context-switch overhead. This model does not scale to C10K on commodity hardware of the era, though modern machines and small stacks push the celling higher.
-   **Event loop (single thread, many connections).** One thread uses non-blocking sockets plus an I/0 multiplexer to drive thousands of connections. This is the foundation of the CLK solution.
as The readiness APIs: select → poll → epoll
-   ***select"** - 0(n) scan, fixed •FD_SETSIZE" (1024) bitmap, copies the full FD set into the kernel on every call. Does not reach Clek cleanly.
-  **"poll'**- removes the 1024 cap but is still 0(n) per call with full-set copying.
-  ***epoll*** (Linux 2.6+) - the kernel keeps the interest set in a red-black tree; "epollwait) returns only the ready FDs, so cost scales with the number of active* connections, not the total. This is what makes C10K practical. See network_io model.md for LT VS. ET semantics.
### Reactor and Proactor patterns
- **Reactor**-synchronous eventdemultiplexing.The event loop waits for
*readiness* (eg. "epoll wait"), then the handler performs the (non-blocking)
I/0 itself. This is the classic epoll-based model (Nginx, Redis, Netty).
Variants: single-reactor single-thread, single-reactor multi-thread, and multi-reactor (main reactor accepts, sub-reactors handle I/O).
**Proactor** - asynchronous completion. The application starts an operation and is notified on *completion*, with the data already moved. On Linux this maps naturally to **io_uring ** (and historically to Windows IOCP); POSIX AIO was a weak fit for sockets.
## SO_REUSEPORT
*SO_REUSEPORT (Linux 3.9+) lets multiple processes/threads bind the same
"IP:port. The kernel hashes each incoming connection's 4-tuple to one listener, providing in-kernel load balancing with no shared accept lock and no thundering herd. This is the standard way to scale accept across cores (used by Nginx, Envoy, HAProxy. Combine with per-core pinned workers for near-linear scaling.
#un Putting C10k together
A modern C10k server is: non-blocking sockets+ epoll (often edge-triggered) +
*  SO_REUSEPORT per-core workers, each running a reactor event loop, with connection state kept small, Nginx and Redis are canonical examples.
*  *COM: 10,000,000 concurrent connections / ~10 Mpps
At roughly ten million connections or millions of packets per second, the
**kernel network stack itself** becomes the bottleneck: per-packet interrupt and softirq cost, skb allocation, the socket layer, locks, and cross-core cache traffic, The guiding idea of C1eM (Robert Graham) is to stop fighting the kernel and instead **bypass or restructure it**, and to **shard everything per core**.
### Kernel bypass: DPDK
**DPDK (Data Plane Development Kit)** takes the NIC away from the kernel with a poll-mode user-space driver, Packets are DA'd straight into user-space memory and processed by a busy-polling thread - no interrupts, no kernel stack, no per-packet syscalls, Paired with:
- **Huge pages** to reduce TLB misses,
*  *CU pinning / isolation** (isolate cores from the scheduler).
*  *Lock-free, per-core ring buffers** and NUMA-aware memory.
- A **user-space TCP/IP stack** (e.g. mTCP, F-Stack, VPP) because the kernel's stack is bypassed.
Trade-off: a core is burned 100% polling, and you lose the kernel's mature stack and tooling.
#** In-kernel fast path: DP / AF_XDP
**XDP (eXpress Data Path)** runs an eBPF program at the earliest point in the


driver's RX path, before skb allocation. It can "DROP, "TX (reflect),
*  REDIRECT, or "PASS packets. This gives DPDK-11ke speed for drop/forward workloads (ODoS scrubbing, load balancing - e.g. Clium, Katran) while staying in-kernel and cooperating with the normal stack for traffic that is passed up.
*  *AF_Dp** is a socket type that "REDIRECT's packets from XP into a user-space zero-copy ring, giving kernel-bypass-style throughput without a separate driver, while keeping the kernel managing the device.
# io_uring
For connection- and syscall-heavy workloads that stlll use the kernel stack,
*io_uring**(kernel5.1+, mature 5.10+) amortizes syscall cost with shared submission/completion rings and supports asynchronous socket operations (accept, send/rec, multishot accept/rec), plus zero-copy send. It narrows El gap between a kernel-stack server and a bypass stack for many real workloads. a User-space stacks and per-core sharding
The common thread at COM is **per-core sharding (share-nothing)**: each core owns Its own NIC queue (RSS/multi-queue), its own memory pools, its own connection table, and ideally its own listener (SO_REUSEPORT or a user-space equivalent). Eliminating shared state removes locks and cross-core cache-1ine bouncing, which is what actually limits scaling at this level.
#* Decision guide
1 Scale | Bottleneck | Approach |
CIeK|connection model, per-Fcost|non-blocking+epoll + SO_REUSEPORT, reactor loops | C180К-С1М | syscalls, softirq, locks | epoll/io_uring, RSS multi-queue, RPS/RFS, per-core workers | 1C1eM / Mpps |kernelstack itself|XDP/AF_XPfor drop/forward;DPDK+user-spacestack for full bypass; per-core
share-nothing 1
## Tuning knobs relevant to high concurrency
| Knob | Purpose | Trade-off |
net.core.somaxconn (default 4096 on 5.4+) | accept-queue cap | too low → accept drops under burst |
"net.ipv4.tcp_max_syn_backlog" | SYN-queue size | interacts with SYN cookies (see syn_attack.md) |
1 fs.file-max ,
"fs.nr_open®, "LimitNOFILE" | FD 1imits | each connection needs an FD |
I -net.ipv4.ip_local_port_range" | ephemeral ports (client side) | bounds outbound connection count per dst |
1 NIC multi-queue + RSS, RPS/RFS | spread RX across cores | needs IQ affinity planning |
# Troubleshooting workflow
1. Confirm the wall: FD limit (EMFILE*), accept-queue overflow (nstat ... ListenOverflows®), or softirg saturation (mpstat -P ALL*,
*%soft®).
1.    Check per-core balance: are all RX queues/cores used (ethtool -1', /proc/interrupts"), or is one core pegged?
2.    Measure syscalls per request (strace -c', "perf stat); if syscall-bound, consider io_uring.
3.    If packet-rate-bound (small packets, Mpps), consider DP/AF_XDP or DPDK.
4.    Remove shared state (locks, global connection tables) before adding cores.
## Pitfalls
- Adding cores without sharding just moves
the bottleneck to lock contention and
cache-line bouncing.
-   Edge-triggered epoll without full draining stalls connections (see"network_io_model,md).
-   DPDK/XDP bypass means you give up the kernel's TCP stack, netfilter, and most standard tooling - a large operational cost.
-   Running out of ephemeral ports on the client side caps outbound concurrencyLong before server limits,
#* Likely interview questions
-  why doesn't thread-per-connection scaleto C10K, and what replaces it?
-  why is epoll o(active) while poll is o(total)?
-   Reactor vs, Proactor -which Linux APIs Implement each?
-  What exactly becomes the bottleneck at CeM, and how do DPDK and XP each address it?
-   XDP VS, AF XOP VS, DPDK - when would you choose each?
-  Why is per-core share-nothing the key to scaling past 1M connections?
-   Where does io_uring help relative to epo11?
## References
-  Dan Kegel, "The C10k problem": <http://www.kegel.com/c10k.html>
-   Robert Graham, "The C10M problem" (Shmoocon 2013).
-   Kernel XDP/AF_XDP docs:<https://ww.kernel.org/doc/html/1atest/networking/af_xdp.html›
-   DPDK: <https://www.dpdk.org/>
-   "io_uring overview: <https://kernel.dk/1o_uring.pdf>
- 
