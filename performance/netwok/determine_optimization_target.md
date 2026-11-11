pertornance/network/how_to_determine_optimization_target-md -->
# Determining Your Network Optimization Target
The overall goals of network performance optimization are to **lower latency** (e.g. RTT) and **raise throughput** (e.g. BPS and PPS). But the right metric, and the priority order among metrics, differs sharply by application and by the layer of the stack you are tuning.
## Choose metrics by protocol layer
### Network interface and network layer
These layers handle encapsulation, addressing, routing, and the actual send/receive of packets. The key metric is **pps (packets per second)**, especially for small packets. Test it with the kernel's built-in packet generator, "pktgen".
#*# Transport layer (TCP / UDP)
Responsible for end-to-end transport. The key metrics are **throughput (BPS)**,
**connection count*s, and **latency**, Test with "iperf3" or "netperf.
### Application layer
Here the
metrics that matter most are **throughput (BPS)**, **requests per
second**, and "*latency**. Test with wrk, "ab, etc.
I
*   Packet size directly affects every one of these metrics, so always test across
*   a range of packet sizes, and make the test scenario resemble production as
*   closely as possible. A good technique is to record real traffic in production
*   and replay it in the test environment.
## Network performance optimization, layer by layer #*# Application-layer optimizations (network I/O)
There are two main I/O-model strategies:
1.    **1/0 multiplexing with 'epoll** - the modern replacement for select and poll. This is the key to solving the C10K problem and the default mechanism in most high-performance network applications today. (See"network_io_model.md and c10k_10M problem. md" .)
2.    **Asynchronous I/0.** The application issues many I/0 operations without waiting for them to finish; the kernel notifies it via events on completion.
**Correction / update:** POSIX AIO ("librt) was always awkward and limited.
On modern Linux the real asynchronous interface is ***io_uring** (since kernel 5.1, mature from 5.10+), which supports networking operations (accept, send/rec, connect) and generally replaces both POSIX AIO and much
*epoll boilerplate for new designs.
Two process/worker models:
1.    **Master process + multiple worker children.** The master manages connections and child lifecycles; children do the real work. The most common model (e.g. Nginx)-
2.    **Multiple processes all listening on the same port** with 'S0 REUSEPORT enabled; the kernel load-balances incoming connections across them. Avoids a single accept bottleneck and the thundering-herd problem.
Additional application-layer wins:
3. **Use long-lived connections instead of short ones** to amortize the cost of
TCP connection setup. The gain is large at high request rates.
4, **Cache rarely-changing data in memory»* to cut the number of network I/Os and speed up responses,
1.    **Use compact serialization (e.g. Protocol Buffers)** to shrink the payload and raise throughput.
2.    **Use DNS caching, prefetch, or HTTPDN** to reduce resolution latency (see "dns.md").
### Socket layer
Every socket has a read and a write buffer:
-   **Read buffer* caches data received from the peer, If it is full, no new data can be accepted,
-   **Write buffer** caches data to be sent, If it is full, the application's write blocks.
To raise throughput you typically tune these buffers:
- "net.core,optmem_max"
-  max ancillary buffer per socket.
-   "net.core.rmem_max" / "net.core, wmem_max" - max socket receive/send buffer.
-   "net.ipv4.tcp_rmem"
•net.ipv4.tcp_wmem" - TCP receive/send buffers.
The ideal send-buffer size is roughly **bandwidth × delay** (the bandwidth-delay product), which lets you f111 the pipe for maximum utilization.
Notes on the triples:
- "tcp_rmem" and "tcp_wem" are "min default max"; the kernel auto-tunes the actual buffer between these bounds.


- "udpmem is min pressure max" and governs total UDP memory (pages), not a per-socket buffer.
*  *Observed defaults on this host (Ubuntu 24.04, kernel 6.8):**
*  **text
net. ipv4.tcp_rmem = 4096
131072
net.ipv4.tcp_wmem • 4096
16384
6291456
4194304
net.core.rmem_max • 33554432
net.core. wmen_max = 335544324
Note: "rem_max wmem_max" on this host are tuned values, not kernel defaults (stock defaults are 212992 bytes); wmem_max" here looks 11ke a typo of 33554432.
Socket options that change connection behavior:
-   "TCP_NODELAY" - disable Nagle's algorithm (send small packets Immediately).
-   TCP_CORK" - coalesce small writes into larger packets (can delay sma11 sends).
-   "SO_SNDBUF" / "SO_RCVBUF - set per-socket send/receive buffer sizes.
### Transport layer: TCP
To optimize CP, understand the basics first: flow control, slow start, congestion avoidance, delayed ACK, and the state machine.
**Congestion-control update:** older notes assumed 'cubic. Modern kernels ship
"bbr" and fa: this host defaults to net.ipv4.tcp_congestion_control = bbr
and net.core.default_qdisc = fa. BBR often improves throughput on
lossy/high-BDP paths.
**Class 1- many TIME_WAIT connections** (high request rate) tie up memory and ports:
1.    Increase 'net.ipv4.tcp_max_tw_buckets"; if using contrack, also raise net.netfilter.nf_conntrack_max.
2.    Reduce 'net.ipv4.tcp_fin_timeout" and
•net.netfilter.nf_conntrack_tcp_timeout_time_wait" to release resources sooner.
3. Enable port reuse with •net.ipv4.tcp_tw_reuse" so TIME_WAIT ports can be used for new outbound connections. *Correction:** •net.ipv4.tcp_tw recycle was
*removed in kernel 4.12** - do not use it; it broke NAT. On this host tcp_tw_reuse = 2° (the "loopback only / conservative" default).
4. Widen the ephemeral port range •net.ipv4.ip_local_port_range" and raise file-descriptor 1imits ('fs.nr_open", "fs.file-max", or "LimitNOFILE in the service's systemd unit).
**Class 2 - SYN flood defense** (see "syn_attack md" for the full treatment):
1. Increase the SYN (half-open) backlog net.ipv4.tcp_max_syn_backlog, or enable SYN cookies 'net.ipv4.tcp_syncookies".
**Correction: ** modern kernels
can use SYN cookies *and* a large backlog together; cookies kick in only when the backlog overflows, so the old mutually exclusive" warning is outdated.
2. Reduce SYN+ACK retransmits with 'net.ipv4.tcp_synack retries".
**Class 3-Keepalive** for long-lived connections. Defaults rarely match app needs:
1.    *net.ipv4.tcp_keepalive_time" - idle time before the first probe.
2.    *net.ipv4,tcp_keepalive_intvl' - interval between probes.
3.    "net.ipv4.tcp_keepalive_probes" - failed probes before giving up.
##* Transport layer: UDP
1, Increase socket buffer sizes and the UDP memory range.
2, Widen the ephemeral port range (as with TCP).
3. Size UDP datagrams to the path MTU to avoid IP fragmentation.
### Network layer
Handles encapsulation, addressing, and routing (IP, ICMP, etc.), Main areas: routing/forwarding, IP fragmentation, and ICMP.
**Routing/forwarding:**
1.    Enable forwarding on routers/NAT gateways/Docker hosts:net.ip4.ip_forward -1.
2.    Tune packet TTL, e.g. "net.ipv4. ip_default_ttl - 64° (raising it wastes work on undeliverable packets).
3.    Enable reverse-path filtering, e.g. •net.ipv4.conf.ethe.rp_filter - 1°, to mitigate IP spoofing and some DDos.
*  *Fragmentation / MTU:** Ethernet frames are max 1518 B; minus the 18 B Ethernet header gives the standard 1500B MTU. Overlay networks (VLAN, GRE) add headers, so adjust MTU. VLAN adds 14 B Ethernet + 8 B VLAN + 8 B UDP + 20 B IP - 50 B per packet, so raise switch/router TUto ~1550, or lower the pre-encapsulation (eg. VM NIC) MTU to ~1450.
*  *ICMP:** to resist host probing and ICMP floods:


1.    net.ip4.imp_echo_ignore_all = 1- ignore all echo requests (blocks ping).
2.    "net.ipv4.1cmp_echo_ignore_broadcasts = 1° - ignore broadcast echo requests.
## Link layer
Handles transmission over the physical network: MAC addressing, error detection, and frame transmission via the NIC.
NICIRQhandling (especially softirgs) burns CPU; spreading it across cores raises throughput:
1.    Set IRQ CPU affinity ("smp_affinity") or run "irqbalance.
2.    Enable **RPS (Receive Packet Steering)** and **RFS (Receive Flow Steering)** so softirg and application processing land on the same CPU, Improving cache hit rate and cutting latency.
Hardware offloads move work from software to the NIC:
1.    **TSO / UFO** - send large TCP/UDP buffers; the NIC segments (by MSS) / fragments (by MTU). **Note:** UFO has been removed from recent mainline kernels; TSO and GSO remain the relevant knobs.
2.    **GSO** - software segmentation deferred until just before the NIC, used whenTSO/UFO is unavailable.
3.    **LRO**-NIC reassembles received TCP segments before handing them up. Must be **off when forwarding/routing**, since merging mismatched headers corrupts packets.
4.    **GRO**- the general, forwarding-safe fix for LO; supports TCP and UDP.
5.    **RSS** - hardware multi-queue receive, spreading packets across CPUs.
6.    **VXLAN offload** - NIC performs VLAN encapsulation.
Interface-level throughput tuning:
1.    Enable multi-queue so each queue uses a different IQ on a different CPU.
2.    Increase ring-buffer sizes and queue lengths (may raise latency).
3.    Use Traffic Control (tc) for Qos across traffic classes.
4.    Use **DPDK** to bypass the kernel stack and poll packets in user space, combined with huge pages, CPU pinning, memory alignment, and pipelining.
5.    Use **Dp* to process packets in the kernel before the stack, for very high performance at low cost. (See c10k_c10M_problem. md" .)
## Summary
1.    Application layer - optimize I/0 model, worker model, and app protocol.
2.    Socket layer - tune socket buffer sizes.
3.    Transport layer - tune TCP and UDP.
4.    Network layer - tune routing, forwarding, fragmentation, and ICMP.
5.    Link layer - tune packet send/receive, offloads, and NIC options.
# Troubleshooting workflow (top-down)
1.    Define the SLO: is the complaint latency, throughput, or connection capacity?
2.    Reproduce and measure at the right layer (wrk/ab" for app, "iperf3" for"pitgen" for PPS).
3.    Localize:
4. Correlate with CPU/softirq (mpstat -P ALL®,
•top"), drops (ethtool -5,
"netstat -s*), and retransmits ("nstat -az | grep -i retrans").
1.    Apply the smallest tuning knob that targets the bottleneck; re-measure.
2.    Persist changes in */etc/sysctl.d/* only after validating.
## Likely interview questions
-  Which metric matters most at each layer, and why does packet size change it?
-  What is the bandwidth-delay product and how does it size socket buffers?
-   Why is "tcp_tw_recycle dangerous, and what replaced it?
-  When must you disable LO, and why is GRO safe for forwarding?
-  epoll vs, iouring for a new high-concurrency server - trade-offs?
-  How doRPS/RFS and RS differ (software vs, hardware steering)?
## References
- Course material: Geek Time, "Linux Performance Optimization in Practice".
