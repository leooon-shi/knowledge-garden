KI-- performance/network/linux_network_stack.md -->
# The Linux Network Stack
A deep reference on how packets flow through the Linux kernel, the data structures involved, the scaling mechanisms, and exactly where and how packets get dropped.
# 1. TCP/IP model vs the Linux stack layers
The classic models and how they map to Linux code:
I OST (7) | TC/IP (4) | Linux implementation |
١---/-/-/
i Application / Presentation / Session | Application | User space; socket API (" send /*recv*) | i Transport |Transport |net/ipv4/tcp*.c, "udp.c' (per-socket buffers) | [Network|Internet|net/ipv4/ip*crouting,netfilter |
i Data 13nk+ Physical | Link|*net/core/dev.c, adise, NIC driver, DMA |
Key idea: inside the kernel a packet is one "sk_buff; moving "up" or "down" a layer just adjusts header pointers - the payload is not copied between layers.
#* 2. Receive (RX) path, end to end
NIC
L DMA into RX ring buffer (descriptors point to DMA-mapped memory)
L Hardware IRQ -
→ driver hard-IRQ handler (minimal work)
L softing NET_RX (ksoftirqd/N)
1 _naps_schedule(): raise NET_RX softirq
L NAPI poll: driver drains ring, builds sk_buff (build_skb) L GRO (Generic Receive Offload): coalesce adjacent segments L netif_receive_skb/RPS: possibly steer to another CPU's backlog
L protocol handler dispatch (by EtherType)
L IP layer: ip_rcv
L netfilter PREROUTING (raw/mangle/nat) → conntrack
L routing decision: local vs forward
L netfilter INPUT (for local delivery)
L Transport: tcp_v4_rcv/ udp_rcv
- demux to socket by 4-tuple/5-tuple enqueue payload into socket receive buffer
L Application: recvmsg() copies data to user space
#*# Hard IRQ vS softirg
The hard-IRQ handler must be short (it runs with interrupts disabled), so it only acknowledges the NIC and schedules the NET_RX" softirq. The heavy lifting (protocol processing) happens in softirq context, which on a busy host is accounted to the
*ksoftirgd/N kernel thread (one per CPU). High %soft" in "mpstat -P ALL 1° indicates a packet-rate-bound CPU.
### NAPI
Under high load, raising one IRQ per packet would livelock the CPU. **NAPI** switches the driver from interrupt mode to **polling"»: the first packet raises an IRQ, the IRQ is then masked, and the kernel polls the ring in softig until it is drained (up to a budget), then re-enables IRQs. This is interrupt coalescing in software and is My NIC-level coalescing (ethtool -C°) interacts with it.
### GRO /
LRO
**GRO**
(Generic Receive Offload) merges multiple received segments of the same flow
into one large sk_buff before passing it up, reducing per-packet overhead. GRO is the safe, software, resegmentable version; **LO** is the older hardware version that can break forwarding/bridging. Check with ethtool -k eth | grep -E "gro|1ro".
#* 3. Transmit (TX) path, end to end
٠٠٠
Application: sendmsg()
- socket layer; copy into socket send buffer L TCP: build segments, add TCP header, congestion/flow control
LIP: ip_output, add IP header, route lookup, netfilter OUTPUT/POSTROUTING (+NAT) L G50/TSO: defer segmentation (keep one big skb as long as possible)
Ladise (traffic control): e.g.fa, fa_codel, pfifo_fast enqueue/dequeue
L driver xmit: place descriptors on TX ring L NIC: DMA out; TX-complete IRQ frees the skb
- **GSO (Generic Segmentation Offload)** keeps a super-sized • sk_buff through the stack and segments late (in software just before the driver, or in hardware via
**TSO** for TCP/**UFO**/**GSO_UDP** for UDP), Fewer trips through the stack per byte - higher throughput.
- **gdisc** is the queueing discipline. The modern default is 'fa/'fa_codel' (net.core, default_qdisc"), which fights bufferbloat. tc qdisc show dev ethx'.
#* 4. "sk_buff°
*   sk_buff® is the packet descriptor used everywhere in the stack, Important flelds:
-   "head,
*  data, "tall, "end" - pointers delimiting the buffer; "data moves as headers are pushed (TX) or pulled (RX).
-   "len',
*  data_len* - total length and paged (non-11near) length.
-   header offsets: •transport_header,
"network_header, "mac_header.
- *dev" - the net device;
sk"- the owning socket.
cb[] - 48-byte control block for per-protocol scratch state.


- fragments live in "skb_shared_info
(page frags) for scatter-gether / zero-copy.
Because layers only adjust pointers, there is no payload copy between layers. "skb's are allocated from the slab allocator ("skbuff_head_cache"); see /proc/slabinfo.
** 5. Scaling across CPUs: RSS / RPS / RFS / XPS
| Mechanism | Layer | What it does |
*RSS**|Hardware|NIChashes theflow(Toeplitz over the 4-tuple) intomultiple Rqueues,each with its own IRQ → spreads R load across PUs. Configure queues with ethtool-L, view with ethtool -1. |
1**RPS** | Software | Software emulation of RSS: "netifreceive_skb hashes the flow and steers it to a remote CPU's backlog. Set /sys/class/net/ethX/queues/rx-N/rps_cpus* (CPU mask). Useful when the NIC has one queue. 1
1**RFS**| Software | Adds flow-to-CUaffinity on top of RPS so a flow is processed on the same CPU the application run on (better cache locality).
*net.core.rps_sock_flow_entries*+ per-queue rps_flow_ent, |
1**PS**|Software (TX) | Picks the TX queue based on the sending CPU teavold lock contention. /sys/class/net/ethx/
1**aRFS** | Hardware | Accelerated RFS - the NIC steers a flow's RX to the CPU where the app runs. 1
Align IQ affinity (/proc/irq/*/smp_affinity) with these for best results; mismatch causes cross-CPU cache bouncing.
# 6. Where packets get dropped, and which counter shows it
I Drop location | Cause | Counter/where to read it |
i NIC Rx ring full | ring too small / softira can't keep up | ethtool-s eth" → rx_dropped, "r_missed_errors,
"rx_fifo_errors; ip-s link" → "missed"/ dropped |
Per-Cubacklog full | softirg backlog> "netdev_max_backlog|/proc/net/softnet_stat® column 2 (dropped) |
I softnet time/budget squeeze | NAPI ran out of budget/time|/proc/net/softnet_stat®column 3 (time_squeeze) |
| IP layer | bad checksum/header, no route, TTLe | 'nstat" → "IpInHdrErrors,
"IpInAddrErrors, "IpInNoroutes" |
I contrack table full | 'nf_conntrack count® = 'nfconntrack_max" | "dmesg "nf_conntrack: table full"; "nstat/conntrack
stats |
I SYN/accept queue overflow | app not accepting fast enough | 'nstat* → "TcpExtListenOverflows, TcpExtListenDrops* | I socket receive buffer full | app reads too slowly (UDP) | 'nstat" → "UdpRcbufErrors, "UdpInErrors; ss -u Recv-e l I qdisc (TX) | token/queue limits | °tc -s qdisc show dev eth* → •dropped / overlimits® |
a Reading /proc/net/softnet_stat®
One row per CPU, hex values. The columns that matter most:
** console
$ cat /proc/net/softnet_stat
...col1
co12
col3
-   **col1 (processed" )** - packets processed by this CPU's softirg.
-   **co12 ("dropped)** - packets dropped because the backlog queue was full (netdev max_backlog too small or CPU overloaded).
**co13 (time squeeze")** - times NAPI poll ran out of budget/time with work left
-  zero and growing means the CPU can't keep up → consider RPS/RSS or bigger budge (net.core.netdev_budget,
-   The last column is the CPU index (cu_collision/received_rps and the index vary by kernel version; on 6.8 the trailing non-zero column increments per CPU index).
A quick decode helper:
** console
$ amk "fprintf "cpuk-3d processed-%d dropped-%d squeezed-%d \n*, NR-1, \ strtonum("0x"$1), strtonum("0x"$2), strtonum("0x"$3)}° /proc/net/softnet_stat
## 7. Tuning sysctl(defaults / trade-offs)
1 sysct1|Default(6.8-ish)| Purpose/trade-off |
1--------/
net.core, netdev max_backlog | 1000|Per-PUR backlog length; raise (e.g. 8192) for high PPS, costs memory. | I °net.core, netdev_budget" | 300 | Packets per NAPI poll cycle; raise 1f time_squeeze grows, at the cost of scheduling fairness. 1
"net.core.default_gdisc' | fa_codel (often "fa") | AQM to 11mit bufferbloat; 'fa" also enables pacing for BBR. | I 'net.ipv4.tcp_congestion_control | cubic (host here: "bbr") | BBR Improves throughput on lossy/high-BOP 21nks but needs *fa/pacing. | | net.core.rmem max
1 net.ipv4.tcp_rmem
wmem_max
tcp_wmem"
212992 | Max socket buffer apps can request; raise for high-BP transfers. |
• 4096 131072 6291456* / 4096 16384 4194304* | TCP autotuning bounds. |
#* 8. Pitfalls
-  Pinning all IRQsto CUP -one CPU saturates at high PPS while others idle, Spread with RSS + IRQ affinity.
-   Growing NIC rings to "fix drops" without checking softira CPU - Just moves the bufferbloat and adds latency.
-   Confusing "dropped (backlog) with time_squeeze" (budget) In "softnet_stat" - the fixes differ.
#* 9. Likely interview questions
-   Trace a packet from the wire to recmsg', naming the IRQ/softirq/NAPI boundaries.
-   What problem does NAPI solve and how?
-   Difference between RSS, RPS, and RFS? When would you use RPS over RSS?Name five dictinct niacec a nacket can be
dronned and the counter for each.

	- Packet flow summary and metrics: "packet_transition.md"
	- Buffers and queues in depth: "buffer.md"
	- Metrics tooling: " diagnostic_tools.md
	Packet capture / drop tracing: " packets_tools. md
	-NAT /conntrack; "NAT,md"
-   Explain the three leading columns of /proc/net/softnet_stat.
-   Why does BBR need "fast"?
## 10, Related notes
## References
-   Geek Time course "Linux Performance Optimization in Practice",
-   Linux kernel ®Documentation/networking/scaling.rst' (RSS/RPS/RFS/XPS),"man 7 netdevice", "man ethtool", "man te,
