<1-- performance/network/network_interview_answers.md -->
# Network - Interview Question Answers
Answers to the "Likely interview questions" in the files under
•performance/network/, grouped by source file.
# Network - Interview Question Answers (Part A)
Answers to the "Likely interview questions" in "linux_network_stack.md", packet_transition.md*, "buffer.md", and "network_io_model.md", grouped by source file.
• convert _poispy
## linux_network_stack.md
**Trace a packet from the wire to recumsg,naming the IRQ/softirq/NAPI boundaries,** A frame arrives at the NIC, which DMAs it into a buffer referenced by an RX ring descriptor without CPU involvement. The NIC then raises a **hardware IRQ**. The driver's hard-IRQ handler runs with interrupts disabled, so it does almost nothing: it acknowledges the device, masks further RX interrupts on that queue, and calls _napi_schedule() › which raises the "NET_RX* **softirq**. That is the first boundary - the hard IRQ ends and deferred work is queued. The softirq runs either on
•net_rx_action
return from interrupt or, on a busy host, in the per-cu ksoftirqd/N' thread. Inside
the driver's **NAPI poll** drains the ring up to a budget, wrapping
each DMA d buffer in an "sk_buff via •buildskb)*. GRO coalesces adjacent segments of the same flow; netif_receive_skb (optionally steering to another CPU's backlog via RPS) dispatches by EtherType to ip_rcv. IP runs netfilter PREROUTING and contrack, makes the routing decision (local vs forward), then runs INPUT for local delivery. Transport (tcp_v4_rcv/ udp_rcv*) demuxes to a socket by the 4-/5-tuple and enqueues the payload into the socket receive buffer. Finally the application's recmsg() syscall copies the data from the socket buffer into user space - that copy is the one unavoidable data copy in the common (non-zero-copy) path.
I
**What problem does NAPI solve and how?**
At high packet rates, taking one hardware interrupt per packet causes **interrupt livelock**: the CPU spends all its time in IRQ context entering and leaving the handler and never makes forward progress on actual packet processing. NAPI (New API) solves this by switching the driver from pure interrupt mode to **polling** under load. The first packet raises an IRQ; the handler then masks that queue's RX interrupt and schedules a softirq poll. The kernel polls the ring in softirq context, draining up to netdev_budget packets (default 300) per poll cycle, bounded also by netdev_budget_usecs*. When the ring is drained below budget, NAPI re-enables the hardware IRQ. This is interrupt coalescing in software: one IRQ can amortize the processing of many packets, which is why it interacts with hardware-level coalescing
tuned via
*  ethtool -C*. If polls repeatedly exhaust the budget with work remaining,
-  time_squeeze* in
*  /proc/net/softnet_stat' grows.
*  *Difference between RSS, RPS, and RFS? When would you use RPS over RSS?** RSS (Receive Side Scaling) is a **hardware** feature: the NIC computes a Toeplitz hash over the packet's 4-tuple and uses it to select one of several R queues, each with its own IRQ that can be pinned to a different CPU, spreading RX load across cores (ethtool -1/-L'). RPS (Receive Packet Steering) is the **software** emulation of RSS: in "netif receive_skb* the kernel hashes the flow and steers the isk_buff® onto a remote CPU's backlog queue for protocol processing, configured via a CPU mask in /sys/class/net/eth/queues/rx-N/rps_cpus*. RFS (Receive Flow Steering) adds flow-to-CPU affinity on top of RPS so a flow is processed on the same CPU where its application thread runs, improving cache locality ('net.core.rps_sock_flow_entries" plus per-queue rps_flow_cnt*). You use RPS over RSS when the NIC has only one RX queue (or fewer queues than cores), or when the hardware hashing distributes poorly:
RPS lets you still fan out softirq processing across cPUs at the cost of some cross-CU cache traffic, When the NIC supports it, aRFS pushes RFS's app-affinity decision down into hardware steering.
**Name five distinct places a packet can be dropped and the counter for each.**
-   **NIC RX ring full** (ring too small or softirq can't keep up): "ethtool -S ethX" shows rx_dropped / rx_missed errors Irx_fifo_errors; ip -s link shows"missed"/ dropped,
-   **Per-CPU softirq backlog fu11** (exceeds "net.core, netdev_max_backlog"): column 2 (dropped) of /proc/net/softnet_stat*.
-   **IP layer** (bad checksum/header, no route, TTL expired): 'nstat" counters"IpInHdrErrors,"IpInAddrErors,
*IpInNoRoutes .
-   **TCP SYN/accept queue overflow** (application not accepting fast enough): counters TcpExtListenOverflows" and TcpExtListenDrops'.
-   **Socket receive buffer full** (slow reader, especially UDP): "nstat"UdpRcvbufErrors* / UdpinErrors, and 'ss -u Recv-Q.
Other valid answers include contrack table full (dmesg "nf_contrack: table full") and the TX disc (tc -s qdisc show dev eth → dropped / overlimits').
**Explain the three leading columns of /proc/net/softnet_stat*,**
The file has one row per CPU, with values in hexadecimal. Column 1 ("processed") is the number of packets this CPU's softirq has processed. Column 2 (dropped') is packets dropped because the per-CUbacklog queue was full - the fix is raising
*net.core.netdev_max_back1og
or offloading work (RPS/RSS) because the CPU 1s
overloaded. Column 3 (time_squeeze") counts the number of times a NAPI poll ran out of its budget or time window with work still pending; a non-zero and growing value means the CPU can't drain the ring fast enough, so you raise 'net.core.netdev_budget" and/or netdev_budget_usecs, or spread load. The key diagnostic distinction is that conflate the Note the eacean fate column, such as cucust not
and received_rps, varies by kernel version.)


**Why does BBR need 'fq?**
BBR is a rate-based, model-based congestion control: instead of reacting to loss, it estimates the bottleneck bandwidth and minimum RT and paces packets out at its computed sending rate. To honor that rate, BBR must space packets evenly in time (pacing) rather than send them in bursts. The fa (fair queue) disc implements this
**per-flow pacing** in the kernel, reading the rate BBR sets on the socket and releasing packets accordingly. Without a pacing-capable disc, BBR would emit bursts that defeat its bandwidth/RTT model, causing queue buildup and loss. (Precise correction to the source's framing: on modern kernels TCP has an internal pacing fallback via the TCP pacing mechanism, 50 BBR can technically work without fa, but
•faoffloads pacing more efficiently and is the recommended and historically required pairing - 'net.core.default_qdisc=fq".)
## packet_transition.md
**Walk through the RX path from NIC to application; where is the hard IRQ vs softirq boundary and why keep the hard IRQ short?**
The NIC DMAs the incoming frame into a buffer referenced by the RX ring, then raises a **hardware interrupt**. The driver's hard-IRQ handler is the pre-boundary work: it acknowledges the NIC, masks the queue's interrupt, and schedules the "NET_RX" softirq via *_napi_schedule() • Everything after that - NAPI polling the ring, building "sk_buff's (modern drivers use build_skb() on DMA'd memory rather than copying), GRO, and the walk up link → IP → transport layers - runs in **softirq** context (accounted to "ksoftirqd/N' on a busy host). At the transport layer the
4-tuple {src IP, src port, dst IP, dst port) demuxes to the owning socket and the payload lands in the socket receive buffer; the application then reads it via recumsg read. The hard IRQ must stay short because it runs with interrupts disabled on that CPU: any work done there blocks other interrupts and risks livelock at high packet rates. Deferring the heavy protocol processing to softirqlets the system batch work (NAPI) and keep interrupt latency low. (Correction to the source: modern drivers do not copy the frame into an "sk_buff in the IRQ handler; they wrap the DMA*d buffer with build_skb(), so the "associate with sk_buff" step avoids a copy except on legacy copy-break paths for small frames.)
**What is the difference between bandwidth, throughput, and utilization?** Bandwidth is the link's **maximum** transmission rate - a capacity ceiling, usually expressed in bits per second (b/s). Throughput is the data **actually** transferred successfully per unit time (b/s or B/s); it is always bounded by bandwidth and is reduced by protocol overhead, loss, retransmission, and contention. Utilization is the ratio throughput / bandwidth*
- how much of the available capacity is actually
in use. A link can have high bandwidth but low throughput (e.g. a small TCP window on a high-BDP path), so utilization tells you how close you are to the ceiling, while latency and loss explain why you may not be reaching it.
*  *Why is PPS, not bandwidth, the right metric for a forwarding/router workload?** A router's cost is dominated by **per-packet** work - descriptor handling, route lookup, netfilter/contrack, and the softirq trip through the stack - which is largely independent of packet size. So the forwarding capacity ceiling is packets per second, not bytes per second. With small packets (e.g. 64-byte), a link can be far from its bit-rate ceiling yet already PPS-saturated because each tiny packet still costs a full round of per-packet CPU; with large packets you hit the bandwidth ceiling at a much lower PPS. Hardware switches can forward at or near line-rate PPS regardless of size, whereas Linux-based forwarding is very sensitive to packet size because each packet consumes CPU. This is why you measure forwarding gear in Mpps and why small-packet tests are the stress case.
*  *where does segmentation happen, and what changes with TSO/GSO enabled?** Without offload, the transport layer segments the stream into MTU-sized pieces during the normal top-to-bottom TX processing: TCP builds each segment, adds the TCP header, IP adds the IP header, and each MTU-sized packet traverses the stack and qdisc individually. With **GSO (Generic Segmentation Offload)** the kernel keeps one super-sized "sk_buff through most of the stack and defers segmentation to the last moment - in software just before handing to the driver - so fewer trips through the protocol layers, routing, and disc are needed per byte, With **TSO (TCP Segmentation Offload)** the segmentation is pushed into the **NIC hardware**: the stack hands the NIC one large buffer plus a template header, and the card produces the MTU-sized TCP segments on the wire (UFO/GO_UDP is the UDP analogue). The net effect is far lower per-byte CPU cost and higher throughput; the trade-offs are that offloads can complicate packet capture (tcpdump sees giant frames) and can interact badly with forwarding/bridging, which is where GRO (the RX-side counterpart) 1s preferred over hardware LRO.
## buffer.md
**Name the three main buffers on the Linux network path and which component owns each.**
-   **Ring buffer (RX/T descriptor rings):** owned by the **NIC driver** and shared with the NIC hardware via DMA. It is a fixed-size ring of descriptors, each pointing to a DMA-mapped region holding a frame. Sized/inspected with ethtool -g/ -6.
-   ***sk_buff buffers:** owned by the **kernel network stack** (allocated from the slab cache "skbuff_head_cache*). Each "sk_buff describes one frame and is threaded through the layers; frames are held on doubly linked lists.
-   **Socket buffers (send/receive):** owned per **socket** and sit at the boundary between the application and the protocol stack. Sending writes into the send buffer; receiving reads from the receive buffer; sizes are bounded by
*SO_SNDBUF/SO_RVBUF"
and the net.ipv4.tcp_mem/tcp_rmem and net.core. wmem_max/rmemmax sysctl.

**Why doesn't passing a packet up the stack require copying the payload?**
Inside the kernel a packet is represented by a single 'sk_buff that points at one underlying data buffer. Moving between layers only adjusts pointers within that buffer: skb->data moves as headers are pushed on TX or pulled on RX, and the per-layer header offsets (mac_header,
•network_header, transport_header") are set
accordingly. Because each layer simply adds or strips its own header by repointing rather than relocating the payload, no bytes are copied between layers. The only mandatory payload copies in the common path are the DMA transfer at the NIC boundary and the copy between the socket buffer and user space in sendmsg / recmsg (zero-copy paths like "sendfile,
•MSG_ZEROCOPY, and page-frag scatter-gather 1n
*   skb_shared_info* can remove even those).
*  *Where would you look to confirm NIC ring-buffer drops vs softirq-backlog drops?**
NIC ring-buffer drops show up at the device: ethtool -s eth reports rx_missed_errors /'rx_fifo_errors rx_dropped, and ip -s link" shows
"missed"/ dropped on the RX side. These mean the ring filled because the NIC received faster than the driver/softirq could drain it; the fix is a larger ring (ethtool -G) and/or faster softirgdraining. Softirq-backlog drops are a
**per-CPU** phenomenon read from column 2 ("dropped) of /proc/net/softnet_stat*: they mean the backlog queue exceeded net. core. netdev max_backlog' or the CPU is overloaded, fixed by raising the backlog, spreading load with RPS/RSS, or increasing
"netdev_budget (whose exhaustion shows as time_squeeze, column 3). Confusing the two leads to the wrong fix - a bigger ring won't help a backlog drop and vice versa.
**What actually limits the number of connections a server can accept?**
Not the 16-bit port number. A server listens on a single fixed port, and each connection is identified by the full 4-tuple '{src IP, src port, dst IP, dst port)®: with the server IP and port fixed, the number of distinct remote (client IP, client port) pairs is astronomically large, so the port field is not the ceiling. The real limits are **resources per connection**: kernel memory for sk_buff's and socket send/receive buffers, contrack entries if NAT/firewalling is involved, and above all
**file descriptors** - each connection is an open fd bounded by the per-process
"ulimit -n* (RLIMIT_NOFILE") and the system-wide fs. file-max". The accept/SYN queue sizes (somaxconn, backlog) bound the rate of accepting rather than the total. The often-quoted "65535 connections" is only the per-(client IP, dst port) ephemeral-port ceiling on the **client** side, not a server limit.
*  *How is "sk _buff/conntrack memory accounted, and how do you inspect it?** Both are **slab** allocations, not page-cache/"Buffer" memory. In memory accounting, the term *Buffer* refers specifically to block-device buffers; network allocations (sk_buff pool, socket structures, contrack entries) fall under slab-managed
*  Cache*. You inspect them via /proc/slabinfo, filtering for the relevant caches (cat /proc/slabinfo | grep -E "skbuff|TCP|nf_conntrack|sock"°), or more readably with sudo slabtop. This matters operationally because a contrack-table explosion or an *sk_buff leak shows up as growing slab memory rather than as process RSS, so it can look like an unexplained kernel memory leak until you check slab accounting; contrack specifically is bounded by "net.netfilter.nf_conntrack_max".
## network io model.md
**Compare select, poll, and epoll in time complexity and memory model.**
"select and "po11" are both **0(n)** per call: the kernel scans the entire set of monitored FDs to find which are ready, and the application passes the full FD set into the kernel and gets it back on every call, so each call copies the set across the user/kernel boundary.
select additionally uses a fixed-length bitmap bounded by
" FD_SETSIZE* (1024 on typical builds), which is both a hard FD limit and a memory-corruption hazard if exceeded; "poll uses a caller-supplied array so it removes the
hard cap but is still 0(n) and still copies.
*epoll keeps the interest set **in the
kernel** in a red-black tree ("epol1_ctl" adds/removes FDs once), so no full set is copied per wait, It is event-driven: ready FDs are moved onto a ready list by callbacks, and "epoll wait returns only those, giving **0(1)** (amortized, 0(number of ready events)) readiness retrieval. The trade-off is more API surface and, in edge-triggered mode, more care to drain FDs fully.
**LT vS, ET - correctness requirements and why Nginx chose ET.**
Level-triggered (LT) notifies you **as long as** the FD is ready - read buffer non-empty or write buffer has room - so you can read/write a bit now and be notified again next time; it is forgiving and the default, Edge-triggered (ET) notifies you
**only on a state change** (read buffer empty-non-empty, write buffer full-has-room), exactly once per edge, The correctness requirement for ET is that you must **drain the FD completely on each event** - loop reading/writing until you get "EAGAIN' - because if you stop early, no further notification arrives and the connection stalls; you also need non-blocking FDs and must handle "EAGAIN explicitly, Nginx chose ET to minimize wakeups and epoll wait round-trips: under high connection counts, ET reports each readiness transition once instead of repeatedly re-reporting a
I
still-ready FD, reducing syscalls and spurious wakeups. Redis, by contrast, uses LT for simpler, less error-prone handling.
**What is the thundering-herd problem and how do accept_mutex,
"EPOLLEXCLUSIVE", and "SO_REUSEPORT each address it?**
Thundering herd is when a single incoming event wakes **many** processes/threads that are all blocked on the same accept or
epol1_wait(), but only one can handle the
event; the rest wake, find nothing, and go back to sleep, wasting CPU and causing scheduler churn. The three mechanisms:
** accept_mutex'** (Nginx application-level lock): only the worker holding the lock adds the listening socket to its epoll set, so only one worker is armed to wake for a new connection at a time. It serializes acceptance to avoid the herd but adds lock
overhead and latency-
***EPOLLEXCLUSIVE*** (kernel, Linux 4.5+): a flag on "epoll_ctlthat tells the kernel to wake only **one** (or a few) of the waiters blocked on a shared FD per


event, fixing the epoll_wait herd without an application lock.
- ** SO_REUSEPORT ** (kernel, Linux 3.9+): each process opens its **own** listening socket on the same port, and the kernel hashes each incoming connection's 4-tuple to exactly one socket, waking exactly one process. No herd, and it also load-balances across listeners.
On modern kernels "S0_REUSEPORT" (or "EPOLLEXCLUSIVE") makes accept_mutex unnecessary. (Historical note: the bare accept() herd was fixed in the kernel in
2.6; "EPOLLEXCLUSIVE addressed the epoll wait" herd in 4.5.)
**Where does io_uring fit relative to epoll and POSIX AIO?**
'epoll* is a **readiness** interface: it tells you *when* an FD can be operated on, and you still issue the actual read/write /accept syscalls yourself one or more syscalls per operation). POSIX AIO (librt") is a **completion** interface for files but was historically limited and effectively thread-pool-backed, and it never covered sockets well, so it was rarely used for networking.
io_uring" (kernel 5.1, mature
from ~5.10+) is a true asynchronous **completion** interface that does* cover socket operations (accept,
connect,
*send/rec
sendmsg/recmsg, plus multishot
variants): you submit operations and reap completions through shared submission and completion ring buffers in memory, batching many operations per syscall (and with SOPOLL, potentially zero syscalls in the hot path). So io_uring both replaces much epoll boilerplate- you submit the actual I/0 rather than just waiting for readiness and then calling I/O separately - and supersedes POSIX AIO for new high-concurrency, syscall-sensitive designs.
**Why can a server hold far more connections than a single-IP client?** Linux identifies a connection by the **5-tuple** •(protocol, sre IP, sre port, dst IP, dst port}*. A client with a single source IP connecting to a fixed server IP:port varies only in its **source port**, which is a 16-bit field further restricted to the ephemeral range (net.ipv4.ip_local_port_range, in practice ~28k-64k ports), so one client-IP/server-endpoint pair is capped near the ephemeral-port count. A server, by contrast, holds its own IP and port fixed, so every distinct **(client IP, client port) ** pair is a different 5-tuple against the same listening socket; the space of distinct clients is effectively unbounded. Hence the server's limit is not the tuple space but resources - file descriptors (ulimit -n, "fs.file-max"), socket/sk_buff memory, and contrack entries - rather than ports. (A client can escape its own cap by adding source IPs or targeting more destination endpoints, which adds entropy to the tuple.)
## c10k_c10M problem.md
**Why doesn't thread-per-connection scale to C10K, and what replaces it?**
Thread-per-connection ties each connection to a dedicated kernel thread that blocks on its I/O. The cost is twofold.
First, memory: each thread reserves a stack (typically 1-8 MB of virtual address space, with a smaller resident set but still meaningful RSS), so 10,000 threads commit gigabytes of address space and non-trivial physical memory. Second, the scheduler: thousands of runnable threads mean heavy context-switch overhead, and context switches thrash CPU caches and the TLB, so throughput collapses even though most threads are merely parked on I/O. The replacement is the event-loop model: a small number of threads (often one per core) drive non-blocking sockets through an I/0 multiplexer. One thread watches thousands of file descriptors, does work only on the ones that are ready, and never blocks on a single connection.
In modern practice this is epoll (often edge-triggered) plus "SO_REUSEPORT per-core workers, each running a reactor loop. It is worth noting that modern machines with small thread stacks and cheap futex-based scheduling can push thread-per-connection much higher than the 1999 ceiling, and runtimes with user-space (green) threads - Go goroutines, Java virtual threads - recover the simple blocking programming model while multiplexing onto epoll underneath, so "threads vs events" is today more about who does the multiplexing than about an absolute scaling wall.
*  *Why is epoll 0(active) while poll is o(total)?**
*  po11* (and "select) are stateless across calls: on every call the application passes the entire set of file descriptors it cares about into the kernel, the kernel scans all of them to see which are ready, and it copies status back out. Cost is therefore proportional to the total number of watched FDs on every single call, regardless of how many are actually active.
"epoll splits the work into two phases. You register interest once with 'epoll_ctl, and the kernel stores the interest set in a long-lived data structure (historically a red-black tree keyed by FD). As events occur, the kernel's socket callbacks link the ready FDs onto a separate ready list.
•epoll wait then just hands back the contents of that
ready list, so its cost is proportional to the number of *ready* (active) FDs, not the total watched set, and there is no per-call copy of the whole interest set, That is why epoll stays flat as idle connections pile up - the common case at C10K, where most of the 10,000 connections are idle at any instant.
**Reactor vs. Proactor - which Linux APIs implement each?**
The Reactor pattern is built on *readiness* notification; the event loop blocks until the kernel says a socket is readable or writable, then the application performs the (non-blocking) I/0 itself in the handler. On Linux this is select/poll'/
*  epoll; it is the model behind Nginx, Redis, and Netty, Reactor comes in variants: single-reactor/single-thread, single-reactor with a worker thread pool for handlers, and multi-reactor (a main reactor accepts and hands connections to per-core sub-reactors that run their own loops), The Proactor pattern is built on *completion* notification: the application submits an operation and the kernel performs the whole thing - including moving the data into or out of the application's buffer - then notifies the application that it 1s already done, On Linux this maps naturally to io_uring submit accept/recv/send, get completions off the completion ring); historically Windows IOCP was the canonical Proactor, and POSIX AIO was a weak fit for sockets. The key distinction: with Reactor the handler still issues the read after being told "you can read now"; with Proactor the read is already finished when you are notified.
*  *What exactly becomes the bottleneck at 10M, and how do DPDK and XP each address it?**
At roughly ten million connections or millions of packets per second, the kernel network stack itself is the limit, not bandwidth or the connection model. The dominant costs are per-packet: interrupt and softirq handling,
sk_buff allocation
and freeing, traversal of the socket and protocol layers, lock acquisition, and cross-core cache-line bouncing when state is shared between cores. DPDK addresses this by removing the kernel from the datapath entirely: a poll-mode user-space driver DAs packets straight into user-space memory and a busy-polling thread processes them, so there are no interrupts, no per-packet syscalls, no skb allocation, and no kernel stack. It pairs with huge pages (fewer TLB misses), isolated/ pinned cores, lock-free per-core rings, NUMA-aware pools, and a user-space TCP/IP stack (mCP, F-Stack, VPP) to replace the bypassed kernel one. XP takes a different tack: it runs an ePF program at the earliest point in the driver RX path, before skb allocation, and can DROP, TX (reflect), REDIRECT, or PASS each packet. For drop/forward-heavy work (DDoS scrubbing, L4 load balancing) it gets DPDK-like speed while staying in-kernel and cooperating with the normal stack for

сТоК стоМ_problel
the expensive stack machinery runs.
packets it passes up. In short, DPDK bypasses the kernel wholesale; XP accelerates a fast path inside the kernel before
**XDP vs. AF_XP VS. DPDK - when would you choose each?**
Choose plain XP (eBPF at the driver RX hook, with maps for state) when the work can be done in the kernel on the packet itself: dropping, reflecting, or redirecting - Dos filtering, L4 load balancing (Katran, C111um). You keep the kernel managing the device, you keep tooling, and traffic you PASS still flows up the normal stack. Choose AF_XDPwhen you want user-space processing with near-bypass throughput but still want the kernel to own the NIC: an XP program REDIRECTS selected packets into a zero-copy user-space ring, so you get kernel-bypass-style speed without a dedicated user-space driver and without surrendering the whole device. Choose full DPDK when you need maximum, deterministic packet rate and are willing to pay the operational cost: the NIC is bound to a user-space poll-mode driver, a core is burned 100% polling, and you must supply your own TCP/IP stack. DPDK gives the highest ceiling but loses netfilter, the kernel TCP stack, and standard observability; AFDPis the pragmatic middle; XP is the lowest-friction choice when in-kernel drop/forward suffices.
**Why is per-core share-nothing the key to scaling past ~1M connections?**
Once per-packet cost is under control, the remaining scaling limit is contention on shared state. Any mutable structure touched by multiple cores - a global connection table, an accept lock, a shared memory pool, a single R queue - forces lock acquisition and, worse, forces the cache line holding that state to bounce between cores caches. Cache-line bouncing and lock contention do not get better as you add cores; they get worse, so naively adding cores moves the bottleneck from the stack to synchronization. Share-nothing removes the shared state: each core gets its own NIC RX/T queue (via RSS/ multi-queue with matched IRQ affinity), its own memory pool, its own connection table, and ideally its own listening socket ('SO_REUSEPORT, or a user-space equivalent, with the kernel hashing each 4-tuple to one listener). With no cross-core coordination on the hot path, scaling becomes close to linear in cores. This is why the decision guide pairs RSS multi-queue, RPS/RFS, and per-core pinned workers at the C100K-C1M tier and makes share-nothing the organizing principle at C10M.
**Where does io_uring help relative to epoll?**
epoll removes the per-D scan but it is still a readiness interface: for each ready socket you make a separate non-blocking syscall (recv,
•send, "accept) to actually move data, soasyscall-heavy or connection-heavy workload
pays a syscall per operation plus the user/kernel transition cost. io_uring is a completion interface with shared submission and completion rings in memory: you batch many operations into the submission queue and reap many completions at once, amortizing or eliminating per-operation syscall overhead, and the operations themselves (accept, recv, send, and multishot variants that keep producing completions without resubmission) run asynchronously with the data already moved on completion. It also supports zero-copy send and registered buffers/files. So io_uring helps most where epoll's residual cost dominates: very high operation rates, lots of small I/os, accept-heavy servers, or workloads where syscall count per request is the measured bottleneck (confirm with perf stat/strace -c"). For a modest-rate server that is not syscall-bound, epoll is simpler and the gain is marginal. io_uring narrows the gap between a kernel-stack server and a full bypass stack while keeping the kernel's TCP stack and tooling.
## network_configuration.md
**Difference between the SYN queue and the accept queue; how to detect overflow.**
The two queues correspond to the two stages of the TCP three-way handshake on a listening socket. The SYN queue (the
"half-open" or incomplete-connection queue) holds connections for which the server has received the client's SYN and replied SYN+ACK but has not yet seen the final ACk. Its size relates to net.ipv4. tcp_max_syn_backlog, and when it fills
- classically under a SYN flood - the kernel either drops new SYNs or, if "net.ipv4.tcp_syncookies" is enabled, stops queuing and encodes connection state into the SYN+ACK sequence number so the handshake can complete without a queue slot.
The accept queue (the "completed" or full-connection queue) holds connections whose handshake has finished (final ACK received); they sit here until the application calls accept). Its effective cap is min(backlog, net.core.somaxconn) =
where "backlog" is the argument to •listen() . To detect overflow: on a listening socket,
ss -Itn* shows "Recv-Q' as the
current accept-queue depth and Send-"as its maximum - "Recv-Q" approaching "Send-Qis the early warning. Hard overflow events are counted globally and are best read as deltas with
'nstat": "TcpExtListenOverflows" increments when the accept
queue is full (application accepting too slowly), and TcpExtListenDrops" increments when a connection is dropped at either queue.
*TcpExtSyncookiesSent rising indicates SYN-queue pressure. Fix accept-queue overflow by raising
'somaxconn"
and the listen() backlog and, more fundamentally, by accepting faster (more acceptor threads, "SO_REUSEPORT").
**What do Recv-Q/Send-Q mean on listening vs. established sockets?**
The column names are the same but the meaning is completely different depending on socket state, which is a classic source of confusion. On an *established* socket they are byte counts: "Recv-Q" is the number of bytes received into the socket's receive buffer that the application has not yet read), and Send-' is the number of bytes the application has sent that the peer has not yet ACked (data still in flight / awaiting acknowledgment). A persistently high "Recv-Q means the application is not reading fast enough; a high
"Send-Q' points at the network or a slow/stalled peer. On a *Listening*
socket they describe the accept queue, not data:
"Recv-D is the current number of completed connections waiting to be
"accept) ed, and Send-Q" is the maximum accept-queue length (min(backlog, somaxconn)"). So a nonzero "Recv-Q" on a
listener is accept-queue backlog - connections the app hasn't picked up - not unread payload.
**How do you compute interface utilization and TCP retransmit ratio?**
Interface utilization compares achieved throughput to link bandwidth, and the formula depends on duplex.
'sar -n DEV 1
reports
*%ifutil using half-duplex * (rxkB/s + txkB/s) / bandwidth and full-duplex *maxrxkB/s, txkB/s) / bandwidth. Get
the link bandwidth from ethtool ethe | grep Speed (e.g.
* 1080Mb/s°), and watch the units: a lowercase "b" means bits, so
you must convert the KB/s rates to bits x8 and the appropriate power-of-ten/1024 factor before dividing - mixing bytes and bits is the common error, On full-duplex links treat
*%ifutil with suspicion and prefer looking at each direction's
rate against bandwidth separately, since a single blended number hides a saturated direction. The TCP retransmit ratio is
*cpRetransSegs / TcOutSegs* - retransmitted segments over total segments sent - read from 'netstat-s or, better, as a
delta from
"nstat" (nstat -az | grep -iE 'RetransSegs|OutSegs"), A rising ratio indicates loss or congestion; a few tenths of a percent is normal on the open internet, while sustained whole-percent values on a LAN point to a real problem.
**Why prefer 'ss'/'nstat° over 'netstat'?**
•ss and 'nstat
come from the modern "iproute2" family, while 'netstat'/ Ifconfig' come from the deprecated "net-tools.
Three practical reasons to prefer the new tools. First, performance and mechanism: netstat walks /proc/net/* and
re-parses text, which is slow and can be inconsistent on a host with hundreds of thousands of sockets, whereas ss queries the kernel directly over the sock_diag netlink interface and is far faster and more detailed. Second,
visibility: "net-tools can miss things the modern stack exposes - it is weak on network namespaces, newer address flags, and multiqueue details - while
* ss/ ip handle them correctly. Third, and specific to nstat: during an incident you
care about *what changed*, and nstat" computes deltas since its last run (and keeps a per-user baseline), so you see the counters that moved during the event instead of lifetime totals that netstat-s reports.
ss" also exposes the kernel's
per-socket TCP_INFO* (ss -ti*: srtt, cnd, retrans, delivery_rate) that netstat cannot show at all.


**What does a nonzero "ethtool -S" r_missed errors" indicate?**
rx_missed_errors is a NIC/driver counter for packets the hardware received but could not hand to the host because there was no room - the RX ring (and the host's ability to drain it) could not keep up, so the NIC dropped the frames. In other words it is a receive-side overrun caused by the host falling behind, not a wire error like a bad CRC (those show under rx_crc_errors / rx_errors). It typically maps to the missed /'overrun column in ip -s link. Causes are softirq/NAPI not getting enough CPU to poll the ring in time, RX rings that are too small for the burst rate, or all interrupts landing on one overloaded core. Remedies: enlarge the RX ring with ethtool -G, spread receive processing across cores with RSS/ multi-queue plus matched IQ affinity (and RPS/RFS if hardware queues are limited), and check/proc/net/softnet_stat* for backlog drops and NAPI budget squeezes. It is the authoritative, driver-level signal - ip-s link aggregates counters and can obscure the specific cause, 50 ethtool-S is where you confirm a receive-ring drop.
## network_delay.md
**Break down the components of end-to-end latency for an HTTPS request.**
End-to-end latency is the sum of several largely independent components, and attributing user-visible delay means identifying which one dominates. In rough order of when they occur for a fresh HTTPS request: DNS resolution - a cold lookup costs one or more RTTs to the resolver before any connection starts (mitigated by a local caching resolver); the TCP three-way handshake - one RTT before data can flow (TCP Fast Open can save it on repeat connects); the TLS handshake - about 2 RTTs for TS 1.2, about 1 RTT for TLS 1.3, and 0-RTT for resumption; then the request/response itself, which carries propagation delay (distance + signal speed, ~5 us/km in fiber, fixed by geography - tens of ms across continents), serialization/transmission delay (frame_bits / link_bandwidth, significant only for large frames on slow 11nks), queuing delay (time in NIC ring, qdisc, and switch buffers - the bufferbloat offender, tens to hundreds of ms under load), and processing delay (route lookup, firewall/contrack, crypto, interrupt/softirq work per hop). Layered on top are protocol effects: TCP slow start keeps early transfers RTT-bound while cund ramps, and a Nagle/delayed-ACK interaction can inject ~40 ms stalls on small request/response exchanges. The practical way to break this down for a real request is curl -w with 'time_namelookup, "time_connect, "time_appconnect" (TLS done), "time_starttransfer" (TTFB), and •time_total, which isolates DNS vs TCP connect vs TLS vs server think time vs transfer.
**Explain the Nagle + delayed-ACK interaction and how you'd diagnose/fix it.**
Nagle's algorithm reduces tiny-packet overhead by holding a small write when there is already unacknowledged data outstanding, coalescing it with later data until an ACK arrives or a full segment accumulates. Delayed ACK, on the receiver, holds back the ACK for a short interval (up to TCP_DELACK_MAX", ~40 ms on Linux) hoping to piggyback it on a response or batch it with another ACK. The pathology appears in request/response flows with small messages: the sender's Nagle is waiting for an ACK before sending the rest of a small message, while the receiver's delayed-ACK is sitting on that ACK waiting for data to piggyback on - a standoff broken only when the ~40 ms timer fires, so each exchange eats up to 40 ms of pure stall. You diagnose it by the signature: request/response latency clustered near a 40 ms multiple while the network RTT is tiny; confirm with a topdump capture showing the gap between the request and the delayed ACK, or with strace /application timing showing writes that return promptly but responses arriving ~40 ms late. The fix is to disable Nagle on latency-sensitive sockets with "TCP_NODELAY", and avoid dribbling tiny writes (build the full message and write it once, or use TCP_CORK" to deliberately batch and then uncork).
TCP_QUICKACK* on the receiver can suppress delayed ACK
for a time, but it is not sticky, so TCP_NODELAY" on the sender is the durable fix.
**How do you tell real packet loss from ICMP de-prioritization in "mtr'?**
"mtr" builds its per-hop view by eliciting responses from intermediate routers (via TTL-limited probes) and from the final target. Intermediate routers generate those responses in their slow/control path and routinely rate-limit or de-prioritize ICMP (and ICMP TTL-exceeded) traffic, so a middle hop can show apparent loss simply because it throttled responses to the probes, not because it dropped transit traffic. The tell is propagation down the path: real loss at a hop affects all traffic passing through it, so it will also appear at every subsequent hop and at the destination. Therefore, if a middle hop shows, say, 20% loss but the hops after it and the final target show 0%, that hop is just de-prioritizing ICMP - ignore it. Loss that is real shows up at that hop and persists (at least as much) through to the end. The actionable number is loss at the final hop / destination. Corroborate with a transport-level tool that bypasses ICMP Shaping -
*   hping3 -S -p ‹port› or an actual TCP load test - since IMP-based ping / mtr can both over- and under-state what TCP experiences.
*  *How would you reproduce a 100 ms
WAN with 1% loss on a test box?**
Use tc" with the
"netem" qdisc, which injects artificial delay, loss, reordering, and corruption on an interface (root
required). Attach it to egress on the test interface: "sudo tc qdisc add dev ethe root netem delay 100ms loss 1% (you can
add jitter, e.g.
*delay 100ms 10ms*, and correlation for more realism). Inspect with tc -s qdisc show dev ethe and tear
it down with "sudo te gdisc del dev ethe root. The crucial caveat: "netem only shapes the direction you attach it to, and attaching to root affects *egress* only. A real WAN is symmetric, so delay/loss applies to both directions of the path; if you only shape one box's egress you model half the RTT and one-way loss. To emulate a symmetric WAN either apply
the same
"netem on both ends, or redirect ingress to an if (intermediate functional block) device and apply
'netem
there so you can shape the receive direction on a single box. Because netem adds one-way delay, a 100 ms egress delay on each end yields ~200 ms RTT; size accordingly depending on whether "100 ms WAN" means 160 ms one-way or 100 ms RTT.
**Why can increasing buffers hurt latency?**
Bigger buffers raise throughput resilience but create bufferbloat. A buffer absorbs bursts, but a large, dumb FIFO under sustained load simply stays full: packets then sit in the queue waiting their turn, and queuing delay is queue_depth / drain_rate, Oversize that queue and you add tens to hundreds of milliseconds of standing latency to every packet, even though no packets are lost, Worse, TCP's loss-based congestion control relies on timely signals (drops, or ECN) to back off; a huge buffer hides congestion by never dropping, so the sender keeps the pipe over-full and the standing queue - and its latency - persists. This is the classic "fixing throughput by enlarging buffers" mistake: you trade a little loss for a lot of delay, and tail latency (p99) suffers most. The right tools are active queue management that keeps the queue short by dropping/marking early (fa_codel, CAKE) and congestion control that targets delay rather than filling the buffer (BBR, ideally paired with the fa" qdisc). The principle: size buffers to the bandwidth-delay product, and manage the queue rather than letting it grow unbounded. ## diagnostic_tools.md
*  *Which command shows per-socket RTT and retransmits, and how do you read it?**
*   55 -ti* (TCP sockets, with internal
TCP_INFO') exposes the kernel's own per-connection TCP state. Key fields: the
congestion-control algorithm (e.g.
*bbr,
cubic);
•rtt: srtt/rttvar in milliseconds - the smoothed round-trip estimate
and its variance - plus
minrtt",
the lowest observed RTT, which is the best proxy for the path's true propagation delay
without queuing;
"mss" (current segment size);
•cund" (congestion window in segments - how much the sender may have in
flight);
"retrans: current/total®
- retransmits outstanding now and cumulative for the connection;
•delivery_rate - the
recently measured goodput, useful for spotting a connection that is not achieving line rate; and rev_rtt/ rev_space" relating to the receive side and autotuning. How to read it for diagnosis: srtt far above minrtt means queuing/ bufferbloat on the path; a nonzero and growing retrans" total means loss or congestion; cnd" stuck small together with low
"delivery_rate" suggests the connection is loss- or RTT-11mited rather than application-limited. On a "listering*
socket the same ss" output instead shows "Recv-Q as current accept-queue depth and Send-Q' as its max.


lostic_tools.md
**How do you tell a NIC ring drop from a softirq backlog drop?**
They are drops at two different stages, and you localize them with two different sources. A NIC ring drop happens at the hardware/driver boundary: the NIC received a frame but there was no room in the RX ring (or the host couldn't drain it fast enough), so the adapter dropped it. You read this from ethtool -5 <dev> as xmissed_errors / rx_no_buffer (and it aggregates into the "missed / dropped column of ip -s link). The signature is the hardware falling behind the wire.
A softirq backlog drop happens later, after the driver has pulled the packet off the ring: in the per-Pu software receive path, if the backlog queue fills or NAPI exhausts its polling budget, packets are dropped in software. You read this from /proc/net/softnet_stat, one row per CPU: column 2 is packets dropped because the backlog was full, and column 3 (time_squeeze) counts times NAPI ran out of budget before draining the ring. So: drops showing up in ethtool -S* as rx_missed/'r_no_buffer are ring-level (enlargeR rings with ethtool -G, spread R with RSS/IRQ affinity); drops showing up in "softnet_stat column 2 with time_squeeze in column 3 are softirq/backlog-level (give softirq more CPU, tune
•netdev_budget / netdev_max_backlog, use RPS/RFS to spread the load). Checking both tells you which stage is the bottleneck.
**'netstat -s vs 'nstat - why prefer one during an incident?**
Both read the same underlying SNMP/stack counters, but netstat -s" prints cumulative totals since boot, while nstat" is designed around deltas: run bare instat" and it prints only what changed since its previous invocation (it keeps a per-user baseline file). During an incident you want to know what moved *during the window of trouble*, not the lifetime sum in which a spike of a few thousand retransmits is invisible against billions of segments. So the workflow is nstat
-ž (reset baseline) or just two nstat calls around the event, then look at the counters that actually incremented -
*TExtListenOverflows, "TcpExtListenDrops,
•TepRetranssegs",
TcpExtSyncookiesSent", TcpExtTCPSynRetrans"•
instat -az
dumps absolute values including zeros when you want the full picture, and -z° resets.
•netstat -s is fine for a quick
one-off glance but is legacy and gives you lifetime totals that bury incident-scale changes, which is why "nstat" is preferred for live troubleshooting and scripting.
**What does 'Recv-Q' mean on a listening socket vs an established one?**
It is the same column with two different meanings keyed on socket state. On a *Listening* socket, "Recv-Q" is the current depth of the accept (completed-connection) queue - connections whose three-way handshake is done and that are waiting for the application to call accept() - and Send-Q is that queue's maximum (min(backlog, net.core.somaxconn)*). So
*Rec-Qclimbing toward Send-Qon a listener is your accept-queue-overflow early warning: the app isn't accepting fast enough, and continued overflow increments
TcpExtListenOverflows". On an *established* socket,
Rec-Q" is a byte count -
data received into the socket's receive buffer that the application has not yet read) - and Send-Q is bytes sent but not yet ACked by the peer. A high established Recv-Q° means the application is slow to consume; a high "Send-Q" means data is stuck in flight (slow or stalled peer, or a full congestion window). The trap is assuming "Recv-Q" on a listener means unread data; it means queued connections.
**which iperf3/netperf test measures latency vs throughput?**
For throughput, use iperf3 in its default TCP mode (iperf3 - SERVER-t 10-P4 for parallel streams) or UDP mode at a target rate ('-u -b 100M', which also reports loss and jitter) - iperf3 is a bulk-transfer bandwidth tool and its headline number is throughput, not latency. For latency, use netperf's request/response test:
netperf -H SERVER -t TCP RR,
which measures round-trip request/response time (and from it, transactions per second); add • -- -o min,mean,p99_latency" to get the latency distribution including the tail. netperf's "TCP_STREAM" test, by contrast, is the throughput (bulk) test, the netperf analogue of iperf3's default mode. So the mapping is: throughput → "iperf3" (default) or netperf -t
TCP_STREAM"; latency → "netperf -t TCP_RR". For HTTP-level tail latency under load, prefer •wrk2" with a fixed rate ("-R") because it avoids coordinated omission, which 'ab and plain urk do not correct for.
# Network - Interview Question Answers (Part C)
## packets_tools.md
**How do you capture only new TCP connection attempts with tepdump?**
A new connection attempt is the initial SYN: a segment with the SYN flag set and the ACK flag clear (the first packet of
the three-way handshake, before any SyN-ACK) - Filter on the TCP flags byte directly: sudo tcpdump -nn -1 enplse 'tcp
[tcpflags] & tcp-syn != 0 and tcp[tpflags] & tcp-ack == 0'. The tcp-syn = term matches any segment with SYN set,
and tcp-ack = e excludes the SYN-ACK (which has both set), so you see only client-initiated opens. This is the standard
way to watch for connection storms or SYN floods. Use -nnso reverse DNS doesn't add latency or noise, and add a host/ port term to scope it on a busy box. An equivalent modern syntax is •"tcp[13] & 8x12 = exe2** (0xe2 = SVN set, ex1e - ACK, so the mask isolates SYN-without-ACK).
*Interpret "IST, "IS.1, "I•l, '[R] in topdump output.**
tcpdump abbreviates the TCP flag field with single letters, and the dot represents the ACK flag specifically:
*[S] - SYN only. The client's opening segment of the handshake.
"[S. I" - SYN plus ACK. The server's response, acknowledging the client's SYN and sending its own.
-   "I-] - a bare ACK (no other flags). The client's final handshake ACK, or any pure data-less acknowledgment thereafter.
-   [R]" - RST (reset). The peer is tearing the connection down abruptly. A bare [R] arriving right after a SYN means the port is closed or the listener/app refused the connection - the classic "connection refused" signature, An RST mid-flow usually means a crashed peer, a firewall/contrack dropping the flow, or an application aborting, So the sequence [s] → [s.] → [.] is a healthy three-way handshake. Other letters you'll see:
•F'-FIN (gracefu]
close), "P*=PSH (push buffered data to the app,
*U=URG.
**A counter shows drops but topdump sees nothing - how do you find where the kernel dropped the packet?** tcpdump taps at the AF_PACKET layer, so a packet dropped *before* that tap (in the NIC ring, driver, or an early kernel stage) or dropped on a path tepdump isn't watching will never appear in the capture, First confirm the drop is real and locate it coarsely with counters: *ethtool-S <dev> forNIC/ring drops (rx_missed_errors, irx_no_buffer.
/proc/net/
softnet_stat for per-CPU backlog drops and " squeezed events, and nstat/netstat-s for protocol-layer drops. Then
attribute the in-kernel drop to an exact function. The skb:kfree_skb" tracepoint fires wherever the kernel frees a socket buffer as a drop, and on modern kernels (5.17+) it carries a reason enum that names *why* (e.g.
"NO_SOCKET', TCP_SUM",
"NETFILTER_DROP*). Use dropwatch -1 kas to map drops to the freeing function,
•perf trace -e skb:kfree_skb" (optionally
with perf record -ag for a stack flamegraph), or •bpftrace -e tracepoint: skb:kfree_skb (@[kstack] - count); ) to
rank drop locations by stack. For following one specific packet through every function it touches - including netfilter, contrack, and routing - pwru 'host X and tcp port Y' traces its whole in-kernel path and shows exactly where it's dropped or NAT'd.

**Capture vs display filters - what's the difference?**
They operate at different stages with different syntaxes. A *capture* filter is a BPF expression compiled and installed in the kernel (Libpcap/BPF syntax, e.g. tcp port 443); it decides which packets are copied to user space at all, so filtering tightly here reduces load and prevents the capture tool from dropping packets on a busy host. A *display* filter is applied afterward by Wireshark/shark to packets already captured (Wireshark syntax, e.g. retransmission,
tcp.analysis.
tcp.port == 443°), and only changes what is shown - the data is already on disk. Consequences: capture
filters can't reference protocol-dissector fields that only exist after parsing, and the two syntaxes are not
interchangeable (tcp port 443" is a capture filter; *tcp.port = 443 is a display filter). In tshark,
capture filter and •-Y takes a display filter.
•-f" takes a
**Which tracepoint attributes kernel drops and how do you use it?**
*   skb:kfree skb. It fires every time the kernel frees an "sk_buff" as a drop, recording the location (the kernel function and call site) and, on kernels since ~5.17, a reason enum describing the cause. It's the single most useful hook for drop attribution because it covers the whole stack-socket receive, netfilter, routing, contrack. Use it live by counting events and grouping by cause with •perf trace -e skb:kfree_skb*; capture stacks for offline analysis with perf record -e skb:kfree_skb-ag -- sleep 10 then perf report; or rank drop sites by kernel stack with •bpftrace -e
*  tracepoint: skb:kfree_skb {@[kstack] = count); }". The bcc/11bbpf tools "dropwatch" and newer "skbdrop / drop_monitor" tooling build on the same mechanism. (Note: a legitimately consumed packet is freed via consume_skb", which does *not* trigger this tracepoint, so what you see here is genuine drops rather than normal frees.)
## practice_1.md
**A box isn't CPU-bound but drops packets under load - where do you look?**
Overall cPu being low is misleading because packet processing can bottleneck on a single CPU or on a fixed-size queue while the rest of the machine idles. Work the layers from NIC upward, using deltas not lifetime totals:
-   **Per-CPU softirg**, not aggregate CPU: mpstat -P ALL 1 to spot one core pegged at high %soft while others are idle (the single-RX-queue signature).
-   **Softirq backlog and NAPI budget**: */proc/net/softnet_stat* - column 2 ("dropped") means the per-Cu backlog overflowed; column 3 (" squeezed") means NAPI exhausted its budget before draining the ring.
-   **NIC ring / driver**:
size; ethtool -1 for how many queues are actually active (RSS).
ethtool -S ‹dev› for "rx missed errors, "rx_no_buffer, "rdropped; "ethtool -g" for ring
- **Accept/SYN queues**:
ss -tl (Recv-Q vs Send-Q on LISTEN sockets) and 'nstat* *TcpExtListenOverflows /"ListenDrops" .
-   **conntrack** if NAT is in the path: 'nf_conntrack_count vs "nf_conntrack_max", plus "dmesg for "table full, dropping packet".
-   **Socket buffers / UDp**: 'nstat" for "UdpRcvbufErrors,
TcpExtTCPBacklogDrop°
The theme: a non-saturated box drops packets when work is concentrated on one CPU or a bounded queue fills faster than it drains. Measure the tail and per-CPU detail, not averages.
**How do you distinguish accept-queue overflow from SYN-queue overflow?**
They are two different queues at two stages of the handshake.
The **SYN queue** (half-open, sized by 'net.ipv4.
tep_max_syn_backlog®) holds connections that have received a SYN and sent a SYN-ACK but not yet completed. The **accept queue** (fully-established, sized by
*min(backlog, net.core.somaxconn)" where backlog" is the app's "listen()
holds completed connections waiting for the app to accept). Distinguish them by counter and by socket state:
argument)
- Accept-queue overflow: *s -tl shows "Recv-Q pinned at Send-Q on the LISTEN socket (Rec-Q - current accept-queue depth, Send-Q= its max), and
"nstat
shows "TcpExtListenOverflows"
*listen(backlog) *and* "net.core.somaxconn" - both, since the effective size is the min.
and TpExtListenDrops" climbing. The fix is the app's
- SYN-queue overflow: shows up as
*TExtTCPReqQFu11Drop* / TpExtTPRegQFulIDoCookies"
(the latter when SYN cookies
engage). The fix is tep_max_syn_backlog and/or tcp_syncookies".
Symptomatically, accept-queue overflow means the app isn't accepting fast enough (or backlog too small) even though handshakes complete; SYN-queue overflow means half-open connections pile up, typical under SYN flood or very high new-connection rate. Overflowed connections are dropped and the client retransmits after an RTO (~1 s), which is the tail latency you observe.
**Walk through diagnosing a single-RX-queue softirq bottleneck.**
Start from the symptom: packet loss under load with low overall CPU. Confirm it's softirq-bound and concentrated: mpstat
-P ALL 1* shows high "%soft"
on exactly one CPU (e.g. CPUe) while others idle. Correlate with sar -n DEV 1' showing high
rxpck/s*
but only moderate rkB/s - high PPS of small packets, which is the expensive case. Prove the drops: /proc/net/
softnet_stat shows "dropped›* and " squeezed›" on that same CPU, and 'ethtool -S ‹dev› shows rx_missed_errors" incrementing (ring overrun). Establish the root cause:
•ethtool -1 ‹dev› shows Combined: 1" (only one RX/T queue, so no
RSS spreading - every packet's receive softirq lands on the one CPU the queue's IRQ is pinned to), and 'ethtool -g ‹dev› shows a small ring (eg. RX 512 of 4096 max) that can't absorb bursts. Fix in order of leverage: enable multiple hardware queues so RSS hashes flows across CPUs (ethtool -L <dev› combined 8°); grow the ring to absorb bursts (ethtool -G <dev› rx 4096°); if the NIC can't do multiqueue, enable software steering RPS (echo ‹cpumask› > /sys/class/net/<dev>/queues/ rx-8/rps_cpus*); raise the per-CPU backlog and NAPI budget ("net.core.netdev_max_backlog,
•net.core.netdev_budget"); and
spread IRQs across cores (via
"irqbalance or pinning /proc/irq/N/smp_affinity®). Re-measure: %soft should spread
across CPUs and softnet_stat drops stop growing.
**How would you detect and fix a full contrack table?**
Detect: compare net.netfilter.nf_conntrack_count against net.netfilter.nf_conntrack max"
(near-equal means full), and
check dmesg for nf_conntrack; table full, dropping packet. Symptomatically it appears as intermittent timeouts and resets for *new flows while established flows are fine, because once the table is full the kernel drops packets that would create a new contrack entry, The usual driver is high churn of short-lived connections combined with long per-state timeouts holding entries after the connection is effectively done. Fix: raise the table size ('net.netfilter. nf_conntrack max, budgeting roughly 300 bytes per entry for peak concurrent flows), grow the hash buckets to keep lookups o(1) (/sys/module/nf_conntrack/parameters/hashsize, commonly set to 'nf_conntrack_max/4" or so), and shorten the states that hoard entries - especially net,netfilter.nf_conntrack_tcp_timeout_time_wait and ..._close_wait'. The cleanest fix,
if NAT/contrack isn't actually required on that path, is to stop tracking those flows (a "NOTRACK" rule in the raw table) or remove contrack from the path entirely.
**Why use 'nstat" deltas and fixed-rate load generators?**
Lifetime counters (what
"netstat -s or a bare nstat print since boot) are dominated by history and tell you nothing
about what's happening *during* an incident. nstat -2
zeroes (snapshots) the counters, so after a sleep/test window a
plain "nstat"
shows only what changed in that interval, and it conveniently prints a per-second rate column - so you can
say "421 ListenOverflows/s during the spike" rather than guessing from a huge cumulative number. Fixed-rate generators matter because closed-loop tools (that fire the next request only after the previous completes) "mask* tail latency: when the server slows,
the client slows too, so the measured latency understates reality - the "coordinated omission" problem.
A fixed-rate open-loop generator like "wrk2" (-R"requests/sec) keeps offering load at the target rate regardless of how the server responds, so queueing delay shows up honestly in the p99/p999. Together they give you honest, interval-scoped evidence to diagnose with and to verify the fix against the same deltas.

**Which metric matters most at each layer, and why does packet size change it?**
At the **network interface / network layer** the dominant metric is **PPS (packets per second**, because that layer's cost is largely per-packet (header parsing, routing lookup, per-packet interrupt/softirq work) rather than per-byte. At the **transport layer** it's **throughput (BPS)**, **connection count**, and **latency**, since TCP/UDP manage end-to-end byte streams and connection state. At the **application layer** it's **requests per second**, **throughput**, and
**latency** - what users actually experience. Packet size changes all of these because fixed per-packet overhead is amortized over the payload: at a given line rate, many small packets mean very high PPS and the system becomes PPS-bound (softirq/interrupt cost dominates), while few large packets mean lower PPS at the same BPS and the system becomes bandwidth-bound. That's why small-packet workloads expose softirq/ring bottlenecks that large-packet throughput tests
tool to the layer: pktgen for PPS,
never reach, and why you should test across a range of sizes - ideally replaying recorded production traffic. Match the
"iperf3 netperf for transport BPS,
wrk/'ab for application RPS.
**What is the bandwidth-delay product and how does it size socket buffers?**
The bandwidth-delay product (BDP) is bandwidth × round-trip time* - the amount of data that can be "in flight" on the path at once (the pipe's capacity in bytes). To keep the pipe full, the sender must be able to have at least one BDP of unacknowledged data outstanding, which means the send buffer (and the receiver's advertised window, bounded by its receive buffer) must be at least the BDP. If the buffer is smaller, the sender stalls waiting for ACks before it can refill the pipe, and throughput collapses to buffer / RTT regardless of available bandwidth. Example: a 10 Gbit/s path at 30 ms RTT has a BDP of about 37.5 MB, so socket buffers capped well below that can't saturate the link. On Linux, tcp_rmem / tep_wmem"
*net.ipv4.
are min default max triples and the kernel auto-tunes the window between those bounds, so what you
really set for high-BP paths is the **max** (and 'net.core.rmem_max/wmemmax",which cap what applications can request via SO_RBUF/SO_SNDBUF®). Oversizing wastes memory and can add latency/bufferbloat, so size to the actual BDP of the
path.
**why is tcp_tw_recycle dangerous, and what replaced it?**
net.ipv4.tcp_tw_recycle" aggressively recycled TIME_WAIT sockets using a per-remote-host timestamp heuristic: it dropped incoming SYNs whose TCP timestamp appeared older than the last one seen from that source IP. Behind NAT or a load balancer, many distinct clients share one source IP but have unsynchronized timestamp clocks, so legitimate SYNs were silently dropped - connections from behind NAT would randomly fail. Because NAT is ubiquitous, it was a notorious footgun, and it was **removed from the kernel in 4.12**; it no longer exists. The safe mechanism for the same goal (reusing TIME_WAIT resources) is 'net.ipv4.tcp_tw_reuse", which lets the kernel reuse a TIME_WAIT socket for a *new outbound* connection when it's provably safe to do so (using TCP timestamps per-connection, not per-remote-host, and only for the active/connect side). On modern kernels tcp_tw_reuse = 2° is the conservative default meaning "enable for loopback only." For server-side TIMEWAIT pressure the better levers are raising tep_max_t_buckets", widening "ip_local port_range reducing tcp_fin_ timeout, and using long-lived/pooled connections instead of churning short ones. (Note: TIME_WAIT Lives on the side that actively closes; tw_reuse therefore helps the client/connecting side, not an accept-heavy server.)
**When must you disable LRO, and why is GRO safe for forwarding?**
LO (Large Receive Offload) is a hardware feature where the NIC merges multiple received TCP segments into one big segment before handing it up the stack. The merge is lossy: it coalesces and discards per-segment header information in a way the NIC chooses, which is fine if the packets terminate locally but *corrupts* packets that must be forwarded - a router/ bridge can't faithfully re-emit the original segments, so you must disable LO whenever the host forwards or routes (routers, NAT gateways, bridges, Docker/overlay hosts) and generally whenever packets will be re-transmitted onward. GRO (Generic Receive Offload) solves the same batching benefit safely: it's a software merge done in the stack with strict rules - it only coalesces segments that are truly contiguous and header-compatible, and it keeps enough information that GSO can later re-segment them into packets equivalent to the originals. Because the merge is reversible and rule-bound, GRO is forwarding-safe and is the recommended replacement for LO; it also supports UDP (GRO/GSO), whereas LO is TCP-only.
**epoll vs. io_uring for a new high-concurrency server - trade-offs?**
"epoll is a readiness interface: it tells you *when* a file descriptor is ready, and you then issue the actual recv/
"send / accept syscalls yourself. It's mature, universally available, well understood, and excellent for the C10K-style many-idle-connections problem, but at very high rates it costs a syscall per readiness check and a syscall per I/0 operation, and it doesn't help with disk I/O (regular files are "always ready"). "io_uring" is a completion interface: the app submits operations (accept, recv, send, connect, read, write, even "openat) into a shared submission ring and reaps results from a completion ring, so many operations are batched into a single syscall - or zero syscalls with SQPOLL/
*  IORING_SETUP_SQPOLL kernel-side polling. It can be genuinely asynchronous for both sockets and files, supports fixed buffers/files and multishot operations, and removes the thread-per-blocking-I/0 pattern. Trade-offs: io_uring needs a recent kernel (networking is practical from w5.6, mature 5.10+), the API is more complex and easier to get subtly wrong, and it has had a string of security CEs such that some hardened/container environments disable it. Pragmatically: for maximum throughput and lowest syscall overhead on a controlled modern kernel, lo_uring; for portability, operational simplicity, and broad environment support, epoll remains the safe default.
*  *How do RPS/RFS and RSS differ (software vs. hardware steering)?**
All three spread received-packet processing across multiple CPUs to avoid a single-core softirq bottleneck, but at different layers:
-   **RSS (Receive Side Scaling** is *hardware* steering. The NIC has multiple RX queues, each wired to its own IRQ/CPU, and it hashes each packet's flow tuple to pick a queue. Spreading happens before the kernel even sees the packet, so it's the cheapest and highest-performing, but it requires NIC multiqueue support and spreads by hash without regard to where the consuming application runs,
-   **RPS (Receive Packet Steering)** is the *software* equivalent for NICs with too few queues: the kernel hashes the flow early in receive and hands the packet to a chosen CPU's backlog for the rest of stack processing, It costs some inter-CPU signaling but needs no special hardware,
-  **RFS (Receive Flow Steering)** adds application-awareness on top of RPS: it steers each flow's softirq processing to the *same* CPU where the application thread that owns that socket last ran, so the data is hot in that core's cache when the app reads it - improving cache hit rate and cutting latency, (accelerated RFS pushes this steering decision down into capable hardware.)
In short: RSS = hardware, flow-hash spreading; RPS - software flow-hash spreading; RFS • software spreading that follows the consuming application for cache locality.
## dns.md
**Recursive vs. iterative queries - who does which?**
A **recursive*» query asks the responder for a complete, final answer: the responder 15 obligated to either return the answer or an error, doing whatever work is necessary itself. This is what a stub client (your machine's resolver library) sends to its configured resolver (the LAN/ISP/public resolver such as "114.114.114.114 or
"8.8.8.8"). An **iterative**
query asks "give me the best you have," and the responder returns a referral* - a pointer to a name server closer to the answer - rather than chasing it down. This is what the recursive resolver does on the client's behalf: it iteratively queries the root servers, which refer it to the TLD servers, which refer it to the zone's authoritative servers, until it gets the final record. So the stub→resolver hop is recursive; the resolver→(root-TLD-authoritative) hops are iterative.
Authoritative servers normally refuse to do recursion for arbitrary clients.


on Answers (Part C) > L# how_to_determine_
**Why can "nslookup" succeed while "ping fails for the same name?** Because they resolve through different mechanisms.
server over port 53.
ping,
**, i.e.
nslookup" (and "dig") talk the DNS protocol "directly* to a name
hosts", mDN5,
"curl, and ordinary applications resolve through the C 11brary's **NSS (Name Service Switch)
*  getaddrinfo(), whose order and sources are governed by */etc/nsswitch.conf* (hosts: 11ne) and include /etc/
*  systemd-resolved', etc. - not just DNS. So a name can resolve via the DNservernslookup succeeds) yet
fail for applications because NSS is misconfigured: a wrong hosts:" ordering, a stale or conflicting /etc/hosts entry,
a broken nss" module, or systemd-resolved returning something different than the server nslookup queried. The fix path is to check/etc/nsswitch.conf s hosts: line and /etc/hosts, and to confirm which resolver applications actually use (resolvectl status) rather than assuming it matches the server nslookup hit. This is why "nslookup ok but ping fails with unknown host" is almost always an NSS/hosts problem, not a DNS-server problem.
**How does DNS enable GSLB, and what are its limits (caching/TTL)?**
Global Server Load Balancing uses DNS as a steering point: an authoritative (GLB) server returns *different* A/AAAA records depending on who's asking typically the geographic/network proximity of the resolver (or client, via EDNS Client Subnet), backend health, and load-so each user is pointed at the nearest or healthiest site, and failover is just handing back a different IP. Because clients keep using the stable name, backend IP changes are transparent. The 11mits all stem from DNS caching and TTL: the GSBanswer is cached by the client, the 0S, and every intermediate resolver for the record's TTL, so steering decisions are *stale* for up to that TTL and failover isn't instant. Lowering the TTL improves responsiveness but increases query volume and resolution latency (and some resolvers ignore very low TTLs). GSLB also steers by the resolver's* location, not the client's, unless EDNS Client Subnet is used - a problem when users sit far from a centralized public resolver. And it operates at connection-setup granularity (name+IP), so it can't rebalance an in-progress connection. These weaknesses are why anycast and L4/L7 load balancers often complement DNS-based GSLB.
Walk through "dig +trace from root to authoritative answer.**
"dig +trace performs the iterative resolution itself, printing each referral so you watch the delegation chain. For "time.
geekbang.org' :
- It first asks the configured resolver for the root (.)NS set and gets the 13 root server names (a'-'m.root-servers.
net*").
- It picks one root server and asks it for •time.geekbang.org'; the root doesn't know but returns a *referral* - the NS records for the org. TLD (e.g.
at.org.afilias-nst.info,
be.org.afilias-nst.org).
- It picks one 'org. server and asks again; that server refers it down to the NS records for the "geekbang.org." zone (e.
g. "dns9.hichina.com, "dns10.hichina.com).
- It picks one of those authoritative servers and asks a final time; that server is authoritative for the zone and returns the actual **** record ( time.geekbang.org. 600 IN A 39.106.233.176).
Each step prints s; Received Nbytes from <server>#53 ... in <ms>, so you can see which server answered and how long each hop took useful for pinpointing a slow delegation. Note +trace queries servers directly and bypasses your resolver's cache after the first hop, so the latencies reflect cold, authoritative lookups rather than what a cached
client would experience.
**How does "systemd-resolved" change where lookups actually go?**
On systems running
"systemd-resolved",
•/etc/resolv.conf® is typically a symlink to a stub file listing a single
nameserver "127.0.0.53° a local stub resolver, not the real upstream. Applications that read "/etc/resolv.conf® or go through glibc NSS therefore send queries to systemd-resolved®, which applies its own logic: per-link DNS configuration, split-DNS routing domains (sending different domains to different upstreams, e.g. a VPN's DNS for corporate names), local caching,
DNSSEC validation, and LLR/mDNS - before forwarding to the actual upstream servers it learned from DHCP/network
config/ resolvectl. So the server listed in /etc/resolv.conf® ("127.0.0.53) is *not* the one that answers authoritatively or even upstream; to see the real upstreams and per-link routing you run resolvectl status, and you
inspect cache behavior with resolvectl statistics / flush with resolvectl flush-caches. The practical traps: a tcpdump port 53° on the external interface shows the upstream traffic but not the stub hop, and debugging with nslookup ‹server›" against *127.0.0.53" behaves differently from hitting the upstream directly.
**A record vs. CNAME VS. NS - what does each return?**
These are different resource-record types answering different questions:
-   An **** record maps a name directly to an IPv4 address (and **AA** to an IPv6 address) - the terminal answer most lookups want.
-   A **NAME** (canonical name) is an *alias*: it maps one name to another name, saying
"look up that name instead. The
resolver then re-resolves the target to eventually reach an A/AAAA. A CAME can't coexist with other record types at the same name and must not appear at a zone apex. It adds a resolution step (and is why "dig" often shows a CAME chain before the final A).
- An **NS** record names the *authoritative name servers* for a zone, and is the mechanism of delegation - it's what a parent zone (root, TLD) returns as a referral to point you at the servers responsible for a subzone. NS records return server *names* (whose addresses are resolved via A/AAAA, often supplied as glue).
So: A/AAAA return addresses (the destination), CAME returns an alias name to chase further, and NS returns the servers that own a zone (the delegation).
# Network - Interview Question Answers (Part D)
Answers to the "Likely interview questions" in "NAT, md", "ddos_attack.md", and
*syn_attack.md",
## NAT. md
**Explain SNAT v5. DNAT V5. MASQUERADE and when you'd use each.** All three rewrite address/port tuples in the nat table, but they differ in which side they touch and how the translated address is chosen. SNAT (Source NAT) rewrites the source IP/port on egress and lives in the "POSTROUTING" chain; you use it so many internal hosts can share one public IP to reach the outside, and you give it an explicit --to-source address. DNAT (Destination NAT) rewrites the destination IP/port on ingress and lives in "PREROUTING (or
•OUTPUT for locally generated traffic); you use it to publish an internal service - load balancers, port forwarding, Docker's -p" publishing, Kubernetes service VIPs are all DNAT. MASQUERADE is a special form of SNAT where the kernel picks the source address dynamically from the outgoing interface at connection time instead of from a configured constant. You use MASQUERADE when the egress IP is not known or stable (DHCP/PPPoE WAN links, cloud NAT gateways); vou use static SNAT when the public IP is fixed, because it is cheaper. Combining

a "PREROUTING" DNAT with a "POSTROUTING"
SNAT for the same pair of addresses
gives bidirectional one-to-one NAT, which is how floating IPs are attached to VMs.
**How does Linux associate a NAT reply with the original request? (contrack)** Through the Netfilter connection-tracking module (nf_conntrack). When the first packet of a flow is seen, contrack creates a tracking object keyed by the 5-tuple (protocol, src IP/port, dst IP/port) and records the NAT transformation that was applied in both the original and reply directions. The object is stored in a hash table (nf_conntrack_buckets" buckets, chained). When a reply arrives, contrack looks up the entry using the inverted tuple and applies the stored reverse translation automatically, so you only need a rule in one direction - the kernel synthesizes the reverse mapping. This is why NAT is stateful: the mapping isn't re-derived per packet from rules, it's remembered per flow, and contrack states
"ESTABLISHED",
"RELATED*) drive both the translation and stateful
firewalling. The cost of maintaining this table is the main performance tax of NAT.
*  *What happens when the contrack table fills up, and how do you detect it?** when the number of tracked connections (nf_conntrack_count") reaches nf_contrack_max, the kernel cannot allocate a new tracking object, so it
*  silently drops* the packet that would have created a new flow and logs nf_conntrack: table full, dropping packet* (rate-limited, so you'11 also see
•net_ratelimit: Ncallbacks suppressed®). Because the drop is silent at the application layer, the symptom looks like random connection timeouts and spiky latency rather than an explicit error. To detect it: check dmesg for the table full message; compare sysctl net.netfilter.nf_conntrack_count against
*   nf_conntrack_max; inspect the state distribution with
*   conntrack -L -o extended | awk /tcp/ {sum[$6]++} END {for (s in sum) print s, sum[s]) "• (a huge "TIME_WAIT" share is a classic cause). The fix is to raise nf_contrack_max / nf_conntrack_buckets, shorten timeouts such as nf_conntrack_tcp_timeout_time wait, or skip tracking for flows that don't need NAT via the raw table (NOTRACK"/° notrack").
*  *Why is MASQUERADE slightly more expensive than static SNAT?**
Static SNAT has the translated source address baked into the rule, so applying it is a constant lookup. MASQUERADE instead asks the kernel to resolve the source IP from the outgoing interface for each new connection, which means an extra route/ device lookup to discover the current primary address. It also registers for interface events: when a link goes down, MASQUERADE flushes the contrack entries tied to that interface's address (since the address may change), which static SNAT does not do. The per-connection address resolution and the teardown machinery are the extra cost. The trade-off is correctness on dynamic-IP links - static SNAT would keep NATing to a stale, now-invalid address.
**How would you profile a NAT gateway that suddenly adds latency?**
Confirm the symptom first with curl/ab" and then work down the stack.
-   Check "dmesg for nf_conntrack: table full and compare nf_conntrack_count* against nf_conntrack_max.
-   Inspect the contrack table composition(contrack -L-o extended | awk...) to see whether •TIME_WAIT or half-open entries dominate.
-   Take a system-wide CPU profile under load:
*  perf record -a -g -- sleep 30° then perf report -g graph,e. On a conntrack-bound NAT box you'll see 'nfhook_slow dominating, with its time split between
*ipv4_conntrack_in (table lookup/allocation),
* iptable_nat_ipv4_in* (the DNAT/
SNAT hook), and "brnfprerouting" if bridge-netfilter is in the path.
- Trace where packets are dropped with eBPF:
*bpftrace -e 'tracepoint:skb:kfree_skb { @[kstack] = count(); } or the BCC
• dropwatch tool (SystemTap's "dropwatch.stp" on "kfree_skb" is the older equivalent). Drops concentrated at "nf_hook slow confirm contrack/NAT as the
bottleneck.
Then raise the contrack limits, trim timeouts, or "NOTRACK" flows that don't
need NAT, and re-measure,
**What is the SNAT source-port race and how do you fix it ("--random-fully')?** When many flows SNAT/MASQUERADE from the same source IP, each needs a unique (src IP, src port, dst IP, dst port) tuple in the contrack table. By default the kernel searches for a free source port starting from a predictable point, so multiple simultaneous connections can independently pick the same candidate port.
Insertion into the contrack table is not transactional across these parallel allocations, so two flows can race and one insert fails, which drops the SYN. The sender only recovers on TCP retransmission after its RTO (~1 s, then ~3 s), so the visible symptom is occasional, suspiciously round 1s / 3s connection-setup latency - common on busy container hosts with many egress connections. The fix is
•--random-fully on the MASQUERADE/SNAT rule, which fully randomizes source-port selection so parallel allocations collide far less often; spreading egress across multiple source IPs also helps by enlarging the tuple space.
**iptables vs. nftables on current distros - what actually runs in the kernel?** Both are user-space front-ends to the same in-kernel Netfilter framework; the difference is the kernel backend and byte-code. Legacy
•iptables talks to the
•x tables
subsystem. nftables replaces "x_tables with the 'nftables virtual
machine,
a single bytecode engine that is more flexible and generally faster for
large rule sets. On current distributions (Debian 10+, RHEL 8+, Ubuntu 20.04+), the
"iptables" command is usually the iptables-nft shim: it accepts legacy syntax but compiles rules into the 'nf_tables" backend, so what actually executes in the kernel is nftables regardless of which command you typed. Practically, prefer nft for new rule sets but legacy iptables commands still work through
the shim. You can confirm which backend is active with iptables --version (it prints
*nf tables° or
"legacy") and inspect the real ruleset with 'nft list ruleset.

**Name the
three DoS exhaustion categories and a defense for each.**
-  Bandwidth/volumetric exhaustion (UDP/ICMP/amplification floods) saturates the link itself. The only effective defense is upstream: ISP blackholing, a scrubbing center, or a CDN/anti-DoS service absorbing the traffic before it reaches your pipe - once the link is full nothing on the host can help.
-   OS resource exhaustion (protocol attacks such as SYN flood) exhausts kernel tables/connection slots. Defend with protocol-aware measures: SYN cookies, SYNPROXY, a larger backlog, shorter retransmit/timeout settings, and connection rate limiting (see syn_attack md").
-  Application resource exhaustion (HTTP floods, slowloris) ties up app threads, CPU, and downstream dependencies with expensive-looking-but-junk requests.Defend at L7: a WAF, request validation, caching/CDN, per-client rate limits, and bot/CAPTCHA challenges.
**Why can't a host defend against a volumetric attack on its own?**
Because the damage happens on the wire before the host can act. A volumetric flood fills the inbound link's bandwidth (and often the upstream router/switch), so legitimate packets are already being dropped by congestion on the path *into* the box. Any host-side rule - iptables drop, SYN cookies, DP - can only act on packets that have already traversed the saturated link, and the act of receiving and dropping them still consumes NIC, PCIe, and softirq CPU. Worse, when the pipe or CPU is saturated you often lose management access (SSH is TCP too), so you can*t even apply mitigations. Volumetric defense must happen upstream where there is still spare capacity: ISP/transit blackholing, a scrubbing center, or a CDN/anti-DDoS edge that has far more aggregate bandwidth than the attack.
**How do you tell a SYN flood from an application-layer HTTP flood?**
Look at where the resources go and what the packets look like. A SYN flood is a protocol attack: you see a very high inbound Pps of tiny packets with near-zero outbound PPS, a large and growing count of SYN_RECV sockets (ss -n state syn-recv | wc -1°), rising SYN-cookie/listen-drop counters in nstat, and a packet capture full of SYNs without matching ACKs. The connections never complete the handshake. An application-layer HTTP flood uses *completed* TCP connections carrying valid-looking HTTP requests, so the handshake succeeds; the tell-tales instead are many ESTABLISHED sockets, elevated application CPU and backend/DB load, growth in the web server's access logs, and often repetitive
URLs or user-agents. In short: SYN flood = half-open sockets and small-SYN PPS at
L3/L4; HTTP flood = full connections and hot app/DB resources at L7.
**What does high inbound PPS with near-zero outbound PPS indicate?**
It indicates a receive-side flood where the host is being pelted with packets it largely cannot or does not answer - the classic SYN-flood or spoofed-source flood) signature. In
*sar -n DEV 1 you see irxpck/s* in the tens of thousands
while txpck/s" stays tiny. The asymmetry means either the server's responses are going to spoofed/unreachable source addresses (so replies are few), or incoming packets are being dropped before a response is generated. The practical reading is that the box is spending its budget receiving and processing attack packets (watch softirg CPU), not doing useful bidirectional work.
**How do RPS/RFS and multi-queue help under a packet flood?**
A single RX queue steers all interrupts/softirq processing to one CPU, so under a high-PPS flood that one core pegs at 100% %soft while the rest sit idle - a throughput ceiling that looks like a CPU problem but is really an RX-steering problem. Multi-queue NICS (RSS) hash flows across multiple hardware RX queues, each tied to a different CPU via IRQ affinity, so packet processing scales across cores.
RPS (Receive Packet Steering) is the software equivalent: it redistributes received packets across CUs in the kernel when hardware RSS is unavailable or has too few queues. RFS (Receive Flow Steering) adds application locality on top of RPS by steering a flow's packets to the CPU where the consuming application runs, improving cache hit rates. Together they raise the aggregate PPS the host can absorb before dropping, which buys headroom during a flood - though for extreme rates you still want to drop at the driver with DP/eBPF before skb allocation.
## syn_attack,md
**Walk through exactly how a SYN flood exhausts the server.**
TCP setup is a three-way handshake: client SYN → server SYN+ACK → client ACK. When the server receives a SYN it replies with SYN+ACK and creates a half-open entry in state SYN_REC, placing it in the SYN queue whose size is bounded by
*net.ipv4,tcp_max_syn_backlog". That slot stays occupied until the handshake completes or the server gives up after retransmitting SYN+ACK
tcp_synack_retries" times (which with the default of 5 keeps a dead entry around for over a minute. A SYN flood sends SYNs at high rate, usually with spoofed or random source IPs (--rand-source*), and never sends the final ACk. Because the attacker never completes the handshake and the real address owners don't respond, every attack SYN pins a SYN-queue slot for the full retransmit timeout. Once the SYN queue is full, the kernel drops further incoming SYNs - including those from legitimate clients - so genuine users can no longer connect even though the machine is otherwise healthy.
**SYN queue vs. accept queue - which does a SYN flood attack, and how do you measure each?** The SYN queue (a.k.a. the half-open / incomplete-connection queue) holds connections in SYN_RECV between SYN+ACK and the final ACK; its cap is
-  net.ipv4.tcp_max_syn_backlog*. The accept queue (completed-connection queue) holds fully established connections waiting for the application to call
*  accept)*; its cap is min(listen backlog, net.core. somaxconn)*. A SYN flood attacks the *SYN queue*- it fills it with
"SYN_RECV" entries that never advance.
Measure the SYN queue by counting half-open sockets:

ss -n state syn-recv | wc -1* (or the legacy 'netstat -n | grep SYN_RECV | wc -1°), and watch SYN-cookie/overflow counters with nstat -az | grep -iE 'SyncookiesSent|ListenDrops|ListenOverflows|TCPReqQFu11Drop".
Measure the accept queue with ss -Itn*, whose Recv-Q column is the current accept-queue occupancy and Send-d is its configured maximum for a listening socket; accept-queue overflows show up as ListenOverflows / ListenDrops and are caused by a slow accept) loop or too-small somaxconn, not by a SYN flood.
**How do SYN cookies work, and why don't they need half-open state?** With SYN cookies, when a SYN arrives the server does not allocate a SYN-queue entry. Instead it encodes the essential connection parameters - a coarse timestamp, the negotiated MSS, and a hash over the 4-tuple and a server secret - into the 32-bit initial sequence number it puts in the SYN+ACK. That ISN *is* the cookie, and the server then forgets the connection entirely. When the client's final ACK comes back, its acknowledgment number equals cookie+1, so the server recomputes and validates the cookie from the ACK's fields; only if it checks out does the server build the real socket. Because all the state needed to rebuild the connection is carried in the sequence number the client echoes back, the server stores nothing in the interim - so an attacker sending SYNs it never completes consumes no server memory and cannot exhaust the SYN queue. The caveat is that a
32-bit cookie can't carry all TCP options (window scaling and SACK can be 11mited), so cookies are a fallback, not a full substitute for a properly sized backlog. On modern kernels (tcp_syncookies=1', the common default) cookies activate *only when the SYN queue overflows*, so you can safely run them alongside a large
*  tcp_max_syn_backlog" - the old claim that the two are mutually exclusive is outdated.
*  *Why is "tcp_synack_retries" relevant to flood resilience?** tep_synack_retries controls how many times the server retransmits an unanswered
SYN+ACK before it gives up and frees the half-open entry. With the default of 5, the exponential backoff keeps a dead SYN_RECV entry alive for over a minute, which during a flood means each attack SYN monopolizes a scarce SYN-queue slot for that whole time and the queue fills fast. Lowering it (e.g. to 1) makes the kernel abandon unanswered half-open connections much sooner, recycling slots quickly and raising the SYN rate the server can tolerate before the queue saturates. The trade-off is on lossy networks: fewer retransmits means a legitimate client whose SYN+ACK or ACK was genuinely lost is more likely to be dropped, so you tune it down for flood resilience but not to zero.
**When would you use SYNPROXY or XP instead of sysctl tuning?** Sysctl tuning (backlog size, SYN cookies, retransmit count) raises the threshold a single protected host can withstand, but it still processes every attack packet up through the TCP stack and still falls over against a large distributed flood. Use SYNPROXY when you want a gateway/firewall to terminate the handshake on behalf of the backend: the kernel answers with cookies and only creates and forwards a real connection for clients that complete a valid handshake, so the protected service never sees spoofed SYNs at all - ideal on a dedicated edge/router protecting many services. Use DP/eBPF when raw packet rate is the problem: an XP program runs in the driver (or NIC) *before* sk allocation, so it can drop or cookie-respond to SYN floods at the earliest, cheapest point in the datapath, giving the highest achievable mitigation throughput (the kernel even ships
"bpf_tep_raw gen_syncookie_ipv4 for this). In short: sysctls for everyday hardening, SYNPROXY to offload the handshake at a gateway, XP for extreme high-PPS floods where per-packet skb cost is the bottleneck.
**Why is source-IP blocking usually ineffective here?**
Because real SYN floods spoof or randomize the source IP ("hping3 --rand-source, or a botnet of many distinct addresses). A -s <ip> -j REJECT rule only works when the attack comes from a small set of fixed addresses; against randomized sources each packet appears to come from a different IP, so there is no stable address to block, the rule list would need millions of entries, and you risk blocking innocent hosts whose addresses were forged. Spoofing is also easy for SYN floods specifically because the attacker never needs to receive the SYN+ACK, so the source address can be arbitrary. That's why the effective defenses are stateless/protocol-level (SYN cookies, SYNPROXY, DP) or upstream scrubbing, rather than per-IP blocklists.
---