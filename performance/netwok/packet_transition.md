< -- performance/network/packet_transition.md --›
# Network Packet Transitions (RX / TX flow and metrics)
This note summarizes how a packet travels through Linux on receive and transmit, and the core network performance metrics. For the full kernel-level stack walkthrough (NAPI, GRO, netfilter hooks, RSS/RPS/RFS, drop counters) see 11nux_network_stack.md.
## Linux receive (RX) flow
1.    A frame arrives at the NIC. The NIC DMAs the packet into the **RX ring (receive queue) **,
2.    The NIC raises a **hardware interrupt** to tell the driver a packet has arrived.
3.    The driver's hard-IRQ handler does minimal work: it schedules a **softira(NET_RX")** via NAPI and (on legacy paths) associates the frame with an •sk_buff.Nodern drivers DNA directly into memory and wrap it with build_skb() rather than copying.
4.    The softirg/NAPIpoll pulls frames out of the ring and pushes each •sk_buff up the protocol stack, processing bottom-to-top:
5.    **Link Layer** - validate the frame, identify the upper-layer protocol (e.g.IPV4 VS IPv6), strip the frame header/trailer, hand up to the network layer.
6.    **Network layer (IP)** - read the IP header, decide the packet's fate (deliver locally vs forward). For locally destined packets, determine the transport
3.
protocol (TCP vs UDP), strip the IP header, hand up to transport.
*  *Transport layer»» - read the TCP or UDP header, use the 4-tuple
*  (src IP, src port, dst IP, dst port} to find the matching socket, and copy the payload into that socket's receive buffer.
4. **Application layer** - reads newly received data via the socket API (recumsg, "read", etc.).
## Linux transmit (TX) flow
1.    The application calls a socket API (e.g. "sendmsg"). This is a system call, so it traps into the kernel socket layer, which places the data into the socket **send buffer*s.
2.    The protocol stack takes data from the socket send buffer and processes it top-to-bottom:
-   Transport and network layers add the **TCP header** and **IP header*», perform a route lookup to confirm the next hop, and segment according to MTU (or defer segmentation to the hardware via TSO/GSO).
-   The segmented packets go to the network interface layer for physical (MAC) addressing - the next-hop MAC is resolved, frame header/trailer are added, and the frame is queued on the **T queue** (through the qdisc).
1.    A softirq notifies the driver that the TX queue has frames to send.
2.    The driver DMAs the frame off the TX queue and the physical NIC sends it out.
## Core network performance metrics
-   **Bandwidth** - the maximum transmission rate of the link, usually in **b/s** (bits per second). This is a capacity ceiling.
-  **Throughput** - the data actually transferred successfully per unit time, in b/s or B/s. Throughput is bounded by bandwidth; •throughput / bandwidth is the link utilization.
*  *Latency / delay* - time from issuing a request until the remote response arrives. Meaning varies by context: connection-establishment time (e.g. TCP handshake latency) or packet round-trip time (RTT).
*  *PPS (Packets Per Second** - transmission rate measured in packets. PPS is the usual measure of forwarding capacity. Hardware switches can approach line-rate forwarding (PPS at or near the theoretical maximum), whereas Linux-based forwarding
is sensitive to packet size (small packets - high PPS = high per-packet CPU cost).
-   **Availability** - whether the network can communicate normally.
-  **Concurrent connections** - number of active TCP connections.
-   **Packet loss rate** - percentage of packets dropped.
-   **Retransmission rate* - proportion of packets retransmitted.
#How to read these quickly
• console
Sip-s link $ sar -n DEV 1
$ 55 -5
$ nstat
*  per-interface bytes/packets/errors/dropped/missed
*  throughput + PPS per interface
*   connection counts by state
• TCP/IP counters incl, retransmits (TcpRetransSegs)
#*Likely interview questions
-   Walk through the RX path from NIC to application; where is the hard IRQ vs softirg boundary and why keep the hard IRQ short?
-  What is the difference between bandwidth, throughput, and utilization?
-   Why is PPS, not bandwidth, the right metric for a forwarding/router workload?
-  Where does segmentation happen, and what changes with TSO/GSO enabled?
## References
-   Geek Time course "Linux Performance Optimization in Practice".
-   *Documentation/networking/ in the Linux kernel source tree.
