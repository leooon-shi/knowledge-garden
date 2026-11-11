K!-- performance/network/diagnostic_tools.md --›
# Network Diagnostic Tools and Metrics
The metrics that matter for network performance and the exact tools to read them.
#* 1. The metrics
-   **Bandwidth** - link capacity (b/s).
-   **Throughput** - actual data/sec (b/s or B/s); *throughput/bandwidth* • uti1ization.
-   **Latency / RTT** - request-to-response time.
-  **PPS**- packets/sec; the metric for forwarding/smal1-packet workloads.
-   **Concurrent connections** - active sockets by state.
-  **Errors / drops**- RX/TX errors, drops, overruns, retransmits.
## 2. Interface stats
## ip -s link
*** console $ ip -s link
2: enpise: <BROADCAST, MULTICAST, UP, LOWER_UP> atu 9000 qdisc fq state UP ...
RX: bytes
23668573587 109731844
packets errors dropped missed cast
TX: bytes
23112522405 72974057
packets errors dropped carrier collsns
e
"errors" - malformed frames (CRC, length). "dropped" - no buffer/filtered.
- "missed" (RX) - NIC ring overrun: packets the host couldn't pick up in time (classic
"need bigger rings or RPS" signal). "carrier / collsns" (TX) - 11nk/duplex 1ssues.
#a ethtool - NIC-level truth
•*console
$ ethtool -S enplse
$ ethtool -k enpise
$ ethtool -g enp1se
$ ethtool -1 enp2se
ethtool enpise
*   driver counters: r_dropped, rx_missed_errors, t* ...
*   offloads: gro, gso, tso, Iro (on/off)
*   ring sizes: preset max vs current
*   number of RX/TX queues (RSS); -L to change
*   speed/duplex/link
ethtool-S is the authoritative source for hardware/driver drops - ip -s link" aggregates and can hide the specific cause.
## 3. Socket and connection stats
### s5 - the modern netstat
•* console $ ss -s
Total: 372
TCP:
...
$ ss -tnp
$ ss -tni
$ 55 -t1
* summary by protocol/state
42 (estab 21, closed 5, orphaned e, timewait 4)
*   TCP, numeric, with process
*   TCP with internal/TCP_INFO metrics
*   listening sockets (Recv-Q = accept-queue backlog!)
*  ss -ti exposes the kernel's per-socket TCP_INFO - invaluable:
•** console $ ss -ti
bbr rtt:27.137/0.174 ms5:1448 cnd: 69 bytes_acked: 108407 \ ..retrans: 8/8 rcv_rtt:28.64 delivery_rate: 16455112bps minrtt:26.941
Read: rtt: srtt/rttvar*,
"cund" (congestion window), "retrans:current/total,
* delivery_rate,
"minrtt. On a listening socket,
•Rec-Q-current accept queue
depth and "Send-Q" = accept queue max ("somaxconn"-bounded) - "Recv-Q' near "Send-o° means accept-queue overflow.
### netstat-s / nstat - protocol counters
•* console $ netstat -s
$ nstat
$ nstat -az
S nstat -2
*  Cumulative protocol stats (legacy)
*   deltas since last call: cleaner
*   absolute values, show zeros too
# reset baseline
Key TCP counters to watch:
•* console
$ nstat-az| grep -LE"ListenOverflows|ListenDrops|RetransSegs|SyncooklesSent|TCPSynRetrans"
TcpExtListenOverflows
e.e
* accept queue overflow (app too slow)
TcpExtListenDrops
0.0
* SYN dropped (syn/accept queue)
TopRetransSegs
* retransmits (loss/congestion)
IçpExtSyncooklesSent
e.e
* SYN flood pressure
Prefer nstat over "netstat-s: it computes deltas so you see what changed during the incident*, not lifetime totals.
## 4. Time-series with sar
•*console
$ sar -n DEV 1
$ sar -n EDEV 1
$ sar -n TCP, ETCP 1
$ sar -n SOCK 1
*   per-interface throughput + PPS (rxpck/s, rxkB/s)
*   interface errors/drops over time
*   active/passive opens, retransmits, resets
*   socket counts
sar is the go-to for was there a spike at 03:147" - It records history (/var/log/sysstat/) so you can look back after an incident.
## 5. Softirq/backlog health

console
$ cat /proc/net/softnet_stat 088F2bd6 00000080 08000082 •••
* colt-processed, col2-dropped(backlog full), col3-time_squeeze(NAPI out of budget)
See linux_network_stack.md for the full column decode and the auk helper,
# 6. Active throughput/latency generators
### iperf3 - bandwidth test
***console
# server
$ iperf3 -s
*   client: 10s TCP, 4 parallel streams $ iperf3 -c SERVER -t 10 -P 4
*   UDPattarget rate, report loss/jitter $ -c -c SERVER -U-b 100M
enetperf - latency + throughput micro-benchmarks
**console $ netserver
$ netperf -H SERVER -t TCP_STREAM
*   server
*   throughput
$ netperf -H SERVER -t TCP_RR -- -0 min, mean, p99_latency
# request/response latency
"TCP_RR" (request/response) is the right netperf test for latency; "TCP_STREAM" for bulk throughput.
#** pktgen - in-kernel packet generator (PPS stress)
Kernel module pktgen generates packets at high rate directly from the TX path to stress PPS/forwarding without a user-space bottleneck. Configured via /proc/net/pktgen/**• Use on test hosts only.
#I# HTTP load: ab / wrk / wrk2
** console
$ ab-n 10008-c100 http://target/
# simple, Apache bench
$ urk-t4 -cle -d30s --latency http://target/
$ wrk2-t4 -c100 -d30s -R2808 --1atency http://target/
* fixed rate, true tail latency
Prefer wrk2' for tail-latency numbers (avoids coordinated omission). "ab is fine for a quick smoke test but single-threaded and dated.
## 7. Troubleshooting workflow
1.    **Scope:** sar -n DEV/EDEV + ip-s link" - is it throughput, PPS, or errors?
2.    **Errors/drops:** •ethtool -S* (NIC) and "softnet_stat" (backlog) to localize drops.
3.    **Connections:** •ss -5, ss -t1" (accept queue),
nstat" deltas for overflows/
retransmits.
4. **Latency:** *ss -ti" (srtt, retrans), "curl -w, "mtr (see "network_delay.nd").
5.
**Capture:** •tcpdump® / tshark® for the smoking gun (see "packets_tools.md®).
6. **Confirm fix:** re-run the same command and compare deltas.
## 8. Pitfalls
-   Reading lifetime totals (" netstat -5°) during an incident - use 'nstat deltas.
-   Trusting "ip -s link for drop root cause - go to ethtool -5.
-   Benchmarking with a tool that saturates the client, not the server.
-   Ignoring Recv-Q" on listening sockets- It's your accept-queue overflow early warning.
# 9. Likely interview questions
-   Which command shows per-socket RTT and retransmits, and how do you Head it?
-   How do you tell a NIC ring drop from a softirg backlog drop?
-   "netstat -s vs "nstat" - why prefer one during an incident?
-   What does "Recv-Q" mean on a 11stening socket vs an established one?
-   Which test measures latency vs throughput?
## 10. Related notes
-   Stack internals + drop counters; "linux_network_stack, md
-   Packet capture / drop tracing; " packetstools,md
-   Latency measurement; "network_delay,md"
-   Optimization targets: "how_to_determine_optimization_target,md
## References
-   Geek Time course "Linux Performance Optimization in Practice",
-   •man ss,"man ip, "man ethtool,
"man sar", "man nstat", "man 4perf3",
"man netperf,
