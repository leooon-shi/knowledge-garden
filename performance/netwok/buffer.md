No pertormance/network/buffer.md -->
# Network Buffers and Queues
The Linux network send/receive path involves several queues and buffers, all living in kernel-managed memory:
1.    *Ring buffer (RX/T rings)** - exchanged with the NIC via DMA when the card receives or transmits packets.
2.    ***sk_buff buffers** - the kernel data structure allocated by the NICdriver / interrupt handling path to represent a network frame.
3.    **Socket buffers* - used when an application talks to the protocol stack through the socket interface.
#* 1. Ring buffer (NIC descriptor rings)
Because the ring buffer is exchanged with the NIC Via DMA, It belongs to the NIC device driver's domain. It is a fixed-size ring of *descriptors*; each descriptor points to a DMA-mapped memory region that holds (or wt11 hold) a frame.
-   On RX, the NIC DMAS incoming frames into buffers referenced by the RX ring, then raises a hardware interrupt.
-   On TX, the driver places frames onto the TX ring and the NIC DMAs them out.
Inspect and resize the rings with 'ethtool':
**console
$ ethtool -g enptse
* show ring sizes (preset max vs current)
$ ethtool -Genpise rx 4096 * grow the RX ring to 4096 descriptors
If the rings are too small for the incoming packet rate, the NIC drops frames; this shows up as "ry_missed /'ry fifo_errors" in ethtool -S" and "missed / dropped in
*ip -s link".
#* 2. "sk_buff buffer
*sk_buff (socket buffer) is the central kernel structure that describes a network frame. The in-flight frames are threaded onto doubly linked lists, so each element of the list is one packet.
Although the TCP/IP stack is layered, passing a frame between* layers does not copy the payload. Each layer simply adjusts pointers inside the same "sk_buff" (skb->data, "skb-›head, header offsets), adding or removing its header. This zero-copy-between-layers design is key to the stack's efficiency.
*   Correction to the original note: the NIC interrupt handler does not necessarily
*   *copy* the frame into "sk_buff". On modern drivers the frame is DMA'd directly into
*   memory, and the driver wraps that memory in an "sk_buff (often via "build_skb()"),
*   avoiding an extra copy. The old "copy into sk_buff" model only applies to legacy
*   copy-break paths for small frames.
## 3. Socket buffer
The socket buffer lets each socket configure its own receive and send buffer sizes.
-   Sending data = writing into the socket send buffer.
-   Receiving data = reading from the socket receive buffer.
Further processing of the buffered data (segmentation, ACking, reassembly) is handled by the transport layer TCP or UDP.
## Buffer vs Cache terminology
In memory accounting,
**Buffers* always relates directly to block devices, while the
other caches (including network-related allocations) are **Cache"*. The
*sk_buff pool, socket buffers, and connection-tracking entries are all managed by the **slab allocator**, You can see how much memory they use directly via:
•* console
$ cat /proc/slabinfo | grep -E "skbuff|TCP|nf_conntrack|sock"
* or, more readably:
$ sudo slabtop
•••
** How the kernel protocol stack runs
Network send/receive relies heavily on **softirgs**, There is a dedicated kernel thread, **ksoftirgd**, for softira processing, Each CPU 15 bound to one
*ksoftirad thread -e.g. with 2cPUsyou cUsyou get ksoftirad/eand •ksoftirgd/1',
Not all network work happens in the softirq thread, though. The kernel also uses hardware interrupts,
"kworker" threads, the slab allocator, and more; these
mechanisms cooperate to keep the whole stack running, High %soft' in top'/'mpstat or busy ksoftirgd"threads are a classic sign of a packet-rate-bound host.
## TCP / UDP connection limits
For both TCP and UDP, the port number is only 16 bits, so its maximum value is 65535.
**Client side:** every outbound TCP connection needs a free local port to reach the remote server. If you fix the destination (same remote IP + port), the local port is the only degree of freedom, so a single client is bounded by the ephemeral port

range (-28k-64k ports in practice; see "net. 1p4, 1p_local _port range"). The often quoted "65535 connections" is the theoretical port celling, not the practical one.
- **Server side:** a server usually listens on one fixed port (e.g. 80). A connection is identified by the full 4-tuple (src IP, sre port, dst IP, dst porth. with the server IP+port fixed, the theoretical ceiling is the number of distinct client (IP, port) pairs - astronomically large. In practice the 11mit is memory, flle descriptors (ulimit -n', "fs.file-max"), and 'sk_buff/socket memory, not the port count.
## MASQUERADE and contrack (brief)
"MASQUERADE" is one of the most common SNAT rules, typically used to give many internal IPs a shared egress IP. It still works correctly even when multiple internal IPs use the same source port, because MASQUERADE" records every connection through the **conntrack** mechanism. A connection is identified by a 5-tuple
•(protocol, sre IP, src port, dst IP, dst port)°: as long as the full 5-tuple is not simultaneously identical, connections proceed. When the SNAT'd source port would collide, the kernel rewrites the source port to keep it unique.
At large scale (many internal IPs/ high concurrency) this hits resource 11mits: the translated external IP's port space becomes a hard cap on concurrent connections, and the contrack table ('net.netfilter.nf_conntrackmax®), the gateway's bandwidth, and the single-point nature of the NAT box all become bottlenecks.
› For the full NAT/contrack discussion see ®NAT.md".
## Tuning knobs (defaults / trade-offs)
I sysctl | Typical default (6.8) | Effect / trade-off |
---|---| ---١
I 'net.core.rmem_max" max" / wmem_max" | distro-dependent (often 212992) | Upper bound an app can set via •SO_RCVBUF/
*50_SNDBUF*. Raising allows larger windows; wastes memory if set blindly. |
I net.ipv4.tcp_rem/tcpwmem |4096 1310726291456*/ 4096163844194304* | min/default/maxfor TCP autotuning.
Raise max for high bandwidth-delay-product links. |
I °net.core.netdev_max_backlog" | 1000 (often raised to 8192+) | Per-CPU backlog of packets waiting for softira processing. Too small → softnet_stat drops under bursts. |
1 NIC ring size (ethtool -6) | driver default | Larger rings absorb bursts but add latency (bufferbloat at the NIC)- I
## Pitfalls
-   Oversizing socket buffers everywhere increases memory pressure and can worsen latency (bufferbloat) rather than throughput.
-   Blindly growing NIC rings trades drops for latency; measure first.
-   *sk_buff and contrack memory is slab memory - a conntrack-table explosion looks | like a memory leak until you check /proc/slabinfo.
## Likely interview questions
-   Name the three main buffers on the Linux network path and which component owns each.
-   Why doesn't passing a packet up the stack require copying the payload?
-   Where would you look to confirm NIC ring-buffer drops vs softirq-backlog drops?
-   What actually limits the number of connections a server can accept?
-   How is "sk_buff /conntrack memory accounted, and how do you inspect it?
## References
-   Geek Time course "Linux Performance Optimization in Practice".
-   "man 7 socket, "man ethtool", ®Documentation/networking/scaling.rst* (kernel tree).
