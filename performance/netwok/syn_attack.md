<1-- performance/network/syn_attack.md --›
# SYN Flood Attacks and Defense
A SYN flood is a denial-of-service attack that abuses the TCP three-way handshake to exhaust a server's half-open connection resources, so legitimate clients can no longer connect. It is a specific, very common case of the DDoS resource-exhaustion attacks described in •ddos_attack.md".
#* Background: the handshake and the two queues
TCP connection setup is a three-way handshake: elent 'SVN* → server "SVN+ACK* + client "ACK". The kernel tracks connections in two queues (see also
*  network_configuration.md"):
-   **SYN queue (half-open / incomplete):** after the server receives a 'SYN* and sends
*   SVN+ACK*, the connection sits here in state "SN_REC awaiting the final "Ack". Its size is governed by "net.ipv4.tcp_max_syn_backlog •
-   **Accept queue (completed):** once the final "Ack arrives, the connection moves here, waiting for the application to call accept(). Its size is
*  min(listen backlog, net.core.somaxconn)*.
## The attack
The attacker sends a flood of SYN packets, often with spoofed/random source IPs, and never completes the handshake. Each 'SYN* consumes a SYN-queue slot that stays occupied until it times out after tcp_ synack_retries" SYNtAck retransmits) With the SYN queue full, the server drops new ®SYN's and legitimate clients cannot connect.
#* Reproducing it with hping3
*  **bash
*   -S : set the TCP SYN flag; -p 80 : destination port 80
*   u10 u10: send one frame every 10 microseconds $ hping3-S -p 80-1 10 192.168.0.30
-   If the effect is weak, lower the interval (e.g. "-i u1°) or add •--flood.
-   If your terminal becomes unresponsive, raise the interval (e.g.
•-1030°) SO
| you can still SSH into the target (SSH is also TCP).
-   Randomize the source IP to simulate a realistic flood:
*  **bash
$ hping3 -S -p 80 -i u10 --rand-source 192.168.0.30
observe the impact on a normal request (see •ddos_attack.md" for the full curl/sar walkthrough):
***bash
Http code: 000
Total time: 10.0015
curl: (28) Connection timed out after 10000 milliseconds
$ curl-wHttp code: %http_code}InTotal time:%time_total}s\n-o/dev/null--connect-timeout 10http://192.168.0.30
## Detection
### Count half-open connections
*   SYN_RECV (abbreviated from "SYN_RECEIVED*) is the tell-tale state.
*  **bash
# netstat (legacy): -n numeric, -p process
$ netstat -n -p | grep SYN_REC
tcp
192,168.0,30:80
tep
e192,168,0,30:80
tcp
◎
© 192,168,0,30:80
tcp
© 192,168.0.30:80
192.168.0.2:12503
192,168.0.2:13502
192.168.0.2:15256
192.168.0.2:18117
SYN_RECV
SYN_RECV
SYN_RECV
SYN_RECV
$ netstat -n -p | grep SYN_REC | wc -1
193
•
Prefer ss" (faster) on modern systems:
***bash
# all sockets in SYN-RECV state
$ s5 -n state syn-recv $ ss
-n state syn-recv | wc -1
# top source IPs by half-open count
$ ss -n state syn-recv | awk '{print $4) | cut -d: -f1 | sort | uniq -c| sort -n | head
## Count SYN-cookie / overflow events with nstat
••* bash
$ nstat -az | grep -iE *SyncookiesSent|SyncookiesRecv|ListenDrops|ListenOverflows|TCPReqQFu11Drop®

Rising TopExtSyncookiessent
"TepExtListenDrops, or "TcpExtTCPReqQFu11Drop"
confirms SYN-queue pressure. A packet capture shows many SYN" with few matching
*  ACK*:
*  **bash
3 tepdump -nh -1 ethe "tepftepflags) & tep-syn l= e and tepttepflags] & tep-ack -* 6°
#* Mitigation
##* 1. Block the offending source (only works for fixed IPs)
•**bash
5 Iptables -1 INPUT -s 192.168.8.2 -p tep -9 REJECT
After blocking, normal requests recover:
**bash
S curl -wHttp code: %(http_code) InTotal time:*(time_total)stn* -0/dev/nu11 --connect-timeout 10 http://192.168.8.36
Http code: 200
Total time: 1.5721715
But with "--rand-source the source IP is not fixed, so single-IP blocking 1s useless,
##* 2. Rate-limit sYNs (helps against moderate floods)
•*bash
# accept at most 1 new SYN per second
$ iptables -A INPUT -p tcp --syn -m limit --limit 1/s -j ACCEPT
* reject a source IP that opens more than 10 connections to :80 in
$ iptables -I INPUT -p tcp --dport 80 --syn -m recent --name SYN_FLOOD --update --seconds 60--hitcount 10-1 REJECT
If many machines flood simultaneously (a true DDoS), these local measures may fail - you might not even be able to SSH in (SSH is TCP too).
##* 3. Enlarge the SYN backlog
***bash
$ sysctl net.ipv4.tcp_max_syn_backlog
net.ipv4.tcp_max_syn_backlog - 256
# course VM value
$ sysct1 -wnet.ipv4.tcp_max_syn_backlog-1024
net.ipv4.tcp_max_syn_backlog = 1024
644
**Correction on defaults:** this value scales with RAM on modern kernels. On this host (Ubuntu 24.04, kernel 6.8) the default is "2048, and
"net.core.somaxconn* is *4096° (not the old "128").
### 4. Reduce SYN+ACK retransmits
Each SYN REC is retried on failure; the default is 5 retransmits, which keeps dead half-open entries around for over a minute. Lower it:
***bash
5 sysct1 net.ipv4.tcp_synack_retries
$ sysct1 -wnet.ipv4,tcp_synack_retries-1
net,ipva,tcp_synack_retries - 1
# default 5 on this host
### 5. SYN cookies
Sy cookies defend against SYN floods without keeping half-open state, On recedving a"S, the server encodes the connection parameters NSS, tinestamp.T and the 4-tuple) into a cryptographic hash used as the SYNtACK Initial sequence number - the "cookie" - and then forgets the connection. When the client's final "ACK" arrives, the server recomputes and validates the cookie and rebuilds the connection state, Because no half-open state is stored, the SYN queue can't be exhausted.
***bash
$ sysctl-w net.ipv4.tcp_syncookies-1
net.ipv4.tcp_syncookies - 1
••.
**Correction:** on this host tcp_syncookies • 1° by default. Modern kernels activate cookies **only when the SYN queue overflows**, so you can run SYN cookies *and* a large
*tep_max_syn_backlog" together - the old advice that the
two are mutually exclusive is outdated. (Caveat: cookles cannot carry all TCP options, e.g. window scaling may be 11mlted, so they are a fallback, not a replacement for a correctly sized backlog.)
### 6. Kernel-level scrubbing: synproxy and XP
- **iptables/nftables SYNPROXY** makes the kernel answer the handshake with

pray and DP
cookies and only create a real socket for validated clients, offloading the protected service entirely:
*  **bash
*   mark untracked SYNs, then synproxy them
iptables -t raw -A PREROUTING -1 ethe -p tcp ••dport 8e -syn -j CT •-notrack iptables -A INPUT -1 ethe -p tcp --dort 8-m conntrack -ctstate INVALID, UNTRACKED \ ... - SUPROXY --sack-pera --timestamp --wscale 7 =-H5S 1466
-**XDP/eBPF** can drop or cookie-respond to SYN floods at the driver level, before sk allocation, which is the highest-throughput mitigation (used by large-scale scrubbers; see •c10k_c10M_problem.md"). The kernel even ships an XP SYN-cooke helper ( bpf_tcp_raw_gen_syncookie_1pv4).
#* Tuning knobs summary
I sysctl| Default (host, 68) | Effect | Trade-off |
I net.ipv4. tcp_max_syn_backlog | 2048 | SYN-queue size | Too smal1 → drops under burst; large uses memory I I net.ipv4.tcp_synack_retries"|5 | SYN-ACK retransmits before giving up | Lower frees half-open slots faster; may drop clients on lossy links |
I 'net.ipv4.tcp_syncookies* | 1 | Cookie fallback on SYN-queue overflow | Some TCP options 11mited under cookies |
I 'net.core somaxconn" | 4096 | Accept-queue cap | Too small → accept-queue drops after handshake |
## Troubleshooting workflow
1.    Symptom: new connections time out (' curl' code 000), existing ones may be fine; softirq CPU spikes.
2.    Count SYN_RECV*: s5 -n state syn-rec | wc-1° - a large number confirms it.
3.    Identify sources: aggregate by source IP; if random, expect a flood with spoored IPs.
4.    Confirm queue pressure: "nstat" SYN-cookie/ListenDrop counters; "tcpdump"SYN-without-ACK ratio.
5.    Mitigate in order: enable SYN cookies, size the backlog, reduce tep_synack retries; add rate limits; for serious attacks use SYNPROXY or XDP; escalate to upstream/DDoS scrubbing for volumetric floods.
## Pitfalls
-  Blocking by source IP is futile against spoofed/random sources.
-   Over-aggressive •--limit" rate limiting can drop legitimate bursts.
-   Relying on local iptables rules fails if the link/CPU is saturated - you may lose SSH access.
-   SYN cookies disable some TCP options; don't treat them as a full substitute for a correctly sized backlog.
## Likely interview questions
-   Walk through exactly how a SYN flood exhausts the server.
-   SYN queue vs. accept queue - which does a SYN flood attack, and how do you measure each?
-   How de SYN cookies work, and why don't they need half-open state?
-   Why is tcp synack retries relevant to flood resilience?
-   When would you use SYNPROXY or XP instead of sysctl tuning?
-   Why is source-IP blocking usually ineffective here?
## References
- course material; Geek Time,
"Linux Performance Optimization in Practice".
- Related DDoS walkthrough: "ddos_attack.md.

