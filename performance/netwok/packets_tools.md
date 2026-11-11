<1-- performance/network/packets_tools.md -->
# Packet Capture and Drop-Analysis Tools
tepdump and Wireshark/tshark for seeing packets on the wire, plus modern eBPF-based tools (puru, dropwatch) and perf trace for finding why* packets are dropped.
## 1. tcpdump
The universal CLI capture tool (11bpcap). It compiles BPF filters in the kernel so only matching packets are copied to user space.
### Essential flags
1 Flag | Meaning |
-  ----
*  -i eth | interface ('-i any for all) |
-  r file pcap | read from a capture file |
-  sesnap full packet (modern default is already 262144; *-s 96*for headers only) |
-  c N° | stop after N packets |
-  vI-v° | more protocol detail |
*  -A* / *-X* I print payload as ASCII / hex+ASCII |
*  -e | show Link-layer (MAC) header |
*  -tttt* | human-readable timestamps |
## Filter examples
* console
HTTP to/from a host, headers only, numer
sudo tcpdump -nn -1 epise host 10.0.0.5 and tcp port 80 -c
* only retransmit-suspect / RST / FIN
$ sudo tepdump -nn tcp[tcpflags] & (tcp-rst|tcp-fin) I= 6°
# DNS traffic
$ sudo tcpdump -nn -1 any udp port 53
### Reading TCP flags in output
10:00:01.123456 IP 10.0.0.1.53124 > 10.0.0.5.443: Flags [S], seq 12345, win 64240, ...
Flag letters: *S*-SYN, -ACK (the dot), "F°-FIN, "R*-RST, "P'-PSH, "U°-URG.
So [S] then "[S.] then "[-I is the normal 3-way handshake. A bare "[R] early in a flow means the peer refused/reset - a classic "port closed / app crashed" signature.
### Capture discipline
-   -nn° to avoid DNS lookups polluting the trace and adding latency.
-   Always filter tightly on a busy host; an unfiltered capture can drop packets itself (watch the "packets dropped by kernel" line tepdump prints on exit).
-   Capture to *-w' and analyze offline in Wireshark; don't eyeball high-rate flows Live.
## 2, Wireshark/ tshark
Mireshark is the GUT analyzer; "tshark" 1s its CLI. Both read the same -pcap pcapne and share Wireshark display filters (note: *display* filters differ from BPF "capture
•* console
; thank 1 ease sakep port 43: tep analysis. transniston"
read a file and show TCP retransmissions / zero windows (health red flag tshark-r/tmp/cap,pcap -y "tcp,analysis.flags" -t i
* follow a TCP stream's payload
$ tshark -r/tmp/cap,pcap -q -z follow, tcp, ascl1,0
* protocol hierarchy + conversation stats $ tshark -r/tmp/cap.pcap -q -z io,phs -z conv, tcp
Wireshark's **Expert Info** and tcp.analysis.*filters auto-flag retransmissions. duplicate ACKs, zero windows, and out-of-order segments - the fastest way to spot loss/congestion in a trace,
## 3. Finding WHY packets drop (kernel-side)
tepdump shows what's on the wire; these show drops inside* the kernel that never make it to (or leave) the wire.
### dropwatch - where in the kernel skbs are freed as drops
••• console
$ sudo dropwatch -1 kas
› start
3 drops at tcp v4 rcv+...

aune kerned kos are freed s
1 drops at _udp4_1ib_rcv+...
٠٠٠
Maps drop events to the kernel function that freed the 'sk_buff" (via the
*   skb:kfree_skb tracepoint). Great first answer to "the counter says drops, but where?" ### pru (packet, where are you? - eBPF packet tracer
*  •*console
$ sudo puru host 10.0.0.5 and tep port 443'
eBPF/BTF based.)
a# perf trace / tracepoints
*  * console
*   count kfree_skb drops by location $ sudo perf trace -e skb:kfree_skb
*   or with perf record for a flamegraph of the drop path sudo perf record -e skbikfreeskb-ag - sleep 10; perf report
*   skb: kfree_skb (and on newer kernels "skb:kfree_skb" carries a "reason enum) is the single most useful tracepoint for drop attribution.
#1# bcc/bpftrace one-liners
*  * console
*   bpftrace: top drop locations by kernel stack sudo bpftrace -e tracepoint:skb:kfree_skb {@[kstack] - count(): 3°
#*4, Troubleshooting workflow (capture-driven)
1. Counter says drops/retransmits (nstat, "ethtool -S', "softnet_stat) - see
•diagnostic_tools.nd •
1.    Localize on the wire: tcpdump -w' a tight filter during the incident.
2.    Analyze offline: Wireshark Expert Info / •tcp.analysis.retransmission®.
3.    If packets vanish in the kernel (never on wire): "dropwatch" → "puru / perf trace® on sk:kfree_skb" to find the exact function + reason.
4.    Correlate with netfilter/conntrack (drop in "nf_*) or socket buffer (drop in tcpv4_rcv/ udp_rcv®).
## 5. Pitfalls
-   Confusing capture (BPF) filters with Wireshark display filters - different syntax.
-   Running unfiltered tepdump on a 10G host -tepdump itself drops and skews results.
-   Forgetting ®-nn* - reverse DNS adds latency and noise.
-   Assuming "no packets in tepdump" = "nothing arrived" - the packet may be dropped"before* the tap point (e.g. NIC ring); confirm with ethtool -S.
# 6. Likely interview questions
-  How do you capture only new TCP connection attempts with tcpdump?
-   Interpret [ST, "[s.], "[.I, "[R] in tepdump output.
-   A counter shows drops but tepdump sees nothing - how do you find where the kernel dropped the packet?
-   Capture vs display filters - what's the difference?
-   Which tracepoint attributes kernel drops and how do you use it?
## 7. Related notes
-   Metrics and counters; " diagnostic_tools.md
-   Where drops happen in the stack: "linux_network_stack.md
-   Worked case study: "practice_1,md"
## References
-   Geek Time course "Linux Performance Optimization in Practice".
-   "man tepdump","man pcap-filter, Wireshark docs: https://ww.wireshark.org/docs/
-  pru: https://github.com/c111um/pwru
-   dropwatch: https://github.com/nhorman/dropwatch
