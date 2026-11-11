<1-- performance/network/network_delay.md --›
# Network Latency
How to reason about, measure, simulate, and reduce network latency.
#* 1. Sources of latency
Total latency is the sum of several independent components:
-   **Propagation delay** - distance / speed of signal in the medium (~5 us/km in fiber). Fixed by geography; a round trip between continents is tens of ms and no amount of tuning removes it.
-   **Serialization (transmission) delay** - time to clock the bits onto the wire!
*  frame_bits / link_bandwidth. Matters for large frames on siow 11nks.
-   **Queuing delay / bufferbloat** - time spent waiting in a queue (NIC ring, qdisc, switch buffer). The worst offender under load: oversized dumb FIFO buffers hold packets for tens to hundreds of ms. Mitigated by AQM ("fa_codel", CAKE).
-   **Processing delay** - per-hop work: route lookup, firewal1/conntrack, encryption, interrupt/softirg handling on the host.
-   **protocol-induced delay:**
-   •*Nagle's algorithm ("TCP_NODELAY") + delayed ACK interaction** - Nagle holds small writes until outstanding data is ACKed; the receiver's delayed-ACK holds the ACK up to w40 ms ("TCP_DELACK_MAX"). Together they can stall a request/response flow for ~40 ms per exchange. Fix: set "TCP_NODELAY" on latency-sensitive sockets (or "TCP_QUICKACK" / TCP_CORK" as approprlate).
-   **TCP handshake** - 1 RTT for the 3-way handshake before data flows(TCP Fast Open can save it on repeat connects).
-   **Slow start** - early transfers are RTT-bound while cwnd ramps.
-   **DNS resolution** - a cold lookup adds one or more RTTs to the resolver before the real connection even starts. See "dns.md".
-   **TLS handshake** - TLS 1.2 adds ~2 RTTS, TLS 1.3~1 RTT (6-RTT for resumption). #* 2. Measuring latency
### ping - baseline RTT (ICMP)
*  * console $ ping -c 5 example.com
*  look at min/avg/max/mdev; mdev (fitter) matters as much as avg Caveat: many hosts rate-limit or deprioritize ICMP, so •ping can over- or under-state real TCP latency.
### hping3 - TCP-level RTT (not installed here; apt install hping3)
•* console
#SYN to port 80, measure RTT at the transport layer (bypasses ICMP shaping) %. sude hping3 -5 -p 80 -c 5 example.com
### traceroute / mtr - per-hop latency
*** console $ traceroute -n example.com
$ wtr -rwzbc 100 example.com
*   per-hop RTT, find where latency Jumps
*  report mode: los5% and avg per hop
•••
"mtr" combines traceroute + ping continuously; a hop showing loss that does **not** propagate to later hops is usually just ICMP de-prioritization on that router, not real loss.
### cur1 -w - break down HTTP timing
•* console $ cur1-w *dns-%{time_namelookup} connect-%(time_connect} \ tl5-%{time_appconnect) ttfb-%{time_starttransfer} total-%(time_total}\n" | ..-/dev/nul1 -s https://example.com
This isolates DNS vs TCP connect vs TLS vs time-to-first-byte vs total - the fastest way to attribute user-visible latency to a specific phase.
### wrk / wrk2 - latency under Load
•* console
$ wrk -t4 -c100 -d305 --latency http:///target/
*  throughput+ latency distribution
*  wrk2 adds a fixed rate (-R) for correct tall-latency (avolds coordinated omission)
$ wrk2 -t4 -c100 -d305-R2000 --Latency http:/target
### s5 -ti - kernel's own RTT estimate
*** console $ ss -ti
*. per-socket: rtt:27.1/8.27 (srtt/rttvar ms), minrtt, cund, retrans, dellvery_rete #* 3. Simulating latency with te netem netem injects delay/loss/reorder for testing on an interface (root required):
•** console
* add 100ms + 10ms delay to egress on ethe $ sudo tc qdisc add dev ethe root netem delay 100ms 10ms
# add 1% random loss
$ sudo tc gdisc change dev ethe root netem loss 1%
# combine delay + loss + reorder
$ sudo tc qdisc change dev ethe root netem delay 50ms loss 0.5% reorder 25% 50%
# inspect and remove
s tc -s adisc show dev eth0

$ sudo to qdise del dev ethe
root
Note: "netem only shapes the direction you attach it to (usually egress). To emulate a symmetric wAN, apply it on both ends or use an "ifb" device for ingress.
* 4. Fixes (map cause → remedy)
1 Cause | Remedy 1
Nagle + delayed ACK stall | TCP_NODELAY on the socket; avoid tiny writes | Bufferbloat / queuing | "fa_codel/ cake* qdise; right-size buffers; BBR + fa'| High per-connection overhead connection reuse/ keep-alive: multiplexing |
TLS/handshake cost|TLS 1.3, session resumption, TCP Fast Open |
DNS cost | local caching resolver; see "dns.md" |
Propagation | move closer: CDN/edge, regional endpoints (can't beat c) | i Host processing | IRQaffinity+RSS,offloads; see 1inux_network_stack.md"|
#* 5. Pitfalls
-   Trusting 'ping alone ICMP shaping misleads; confirm with TCP-level tools.
-   Measuring average latency only - tail latency (p99) is what users feel; use wrk2.
-   "Coordinated omission" in load tools - use constant-rate mode (wrk2 *-R®).
-   Fixing throughput by enlarging buffers - often increases latency (bufferbloat).
#* 6. Likely interview questions
-   Break down the components of end-to-end latency for an HTTPS request.
-   Explain the Nagle + delayed-ACK interaction and how you'd diagnose/fix it.
-   How do you tell real packet loss from ICNP de-prioritization in‘ntr"？
-   How would you reproduce a 100 ms WAN with 1% loss on a test box?
-   Why can increasing buffers hurt latency?
#* 7. Related notes
-   DNS latency: "dns.md"
-   Tooling details: ®diagnostic_tools.md*, "packets_tools.md
## References
-   Geek Time course "Linux Performance Optimization in Practice".
-   "man tc-netem, "man ss", man curl (write-out variables).
-   Bufferbloat / Codel: https://ww.bufferbloat.net/


