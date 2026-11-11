tabing baseline
<1-- performance/network/ddos_attack.md --›
# DDoS Attacks
**DDoS (Distributed Denial of Service)** uses a large volume of seemingly legitimate requests to consume a target's resources so it can no longer serve genuine traffic. The three broad exhaustion targets:
1.    **Bandwidth exhaustion.** Servers, routers, and switches all have a fixed bandwidth ceiling. Once saturated, congestion prevents other normal traffic from being carried. (Volumetric attacks: UDP/ICMP/amp11fication floods.)
2.    **OS resource exhaustion.** Network services need CPU, memory, and software resources such as connection tables. Exhaust those and the system can no longer accept new connections. (Protocol attacks: SYN flood, etc. - see
*syn_attack.md' .)
3. **Application resource exhaustion.** Apps depend on downstream resources and systems. If the app is busy handling junk requests, real requests slow down or get no response. (Application-layer attacks: HTTP floods, slowlords.)
#* Establishing a baseline
•**bash
* -w prints only the HTTP status code and total time; -0 /dev/nul1 discards the body $ curl -s -w "Http code: *http_code) InTotal time:*(time_total}sIn' -o/dev/nu11 http://192.168.0.38/
Http code: 200
Total time:0.002s
•..
Normally, reaching Nginx takes ~2 ms (0.002 S).
## Reproducing a SYN-flood-style DDoS with hping3
*  *bash
*   -5 sets the TCP SYN flag; -p 80 targets port 80
*  -i u10 sends one frame every 10 microseconds $ hping3-5-p 80-1 10 192.168.0.30
-   If the effect is weak, lower the interval (e.g. "-1 u1*) or add •--flood.
-   If the terminal freezes entirely, raise the interval (e.g.
• -i u30°), or you
will lose SSH access to VM1 (SSH is also TCP).
Measure the impact on a normal request:
# --connect-timeout bounds the connection phase
$ curl- "Http code: %(http_code}InTotal time:*(time_total}s\n°-o/dev/null --connect-timeout 10 http://192.168.0.30
Http code: 000
Total time:10.0015 curl:
(28) Connection timed out after 10000 milliseconds
Match the interface rate with "sar":
***bash
$ sar -n DEV 1
68:55:49
08:55:S8
08:55:50
08:55:58
5535
txkB/s
6.60
37.78
6.06
176
0.00
%ifutil
0.00
0.02
0.00
Note the huge inbound PPS (rxpck/s* ~22,274) against tiny txpck/s* - many small syNs arriving, few responses leaving - the classic SYN-flood signature.
## Mitigation
The specific SYN-flood case (SYN/accept queues, SYN cookies, tcp_max_syn_backlog®,
*tep_synack_retries, SYNPROXY, XDP, and the full
detection/mitigation workflow) 1s covered in depth in "syn_attack,md.
General Dos defense layers:
-   **Rate limiting / connection limiting** at the host (Aptables "11mlt* /"recentmodules) or edge,
-  **SYN cookies and SyNPRoxy** for protocol floods.
-   **XDP/eBPF** drop at the driver for high-PPS floods (see
*(10k_C10M problem, nd).
-   **Upstream scrubbing / CDN / anti-DDos services** for volumetric attacks that saturate the 11nk - once the pipe is full, nothing on the host can help,
-   **Application-layer defenses** (HAF, request validation, caching) for HTTP floods.
Local host rate-11miting examples:
•*bash
# accept at most 1 new SYN per second
$ iptables -A INPUT -p tcp --syn -m 11mit --11mit 1/s -j ACCEPT
# reject a source IP exceeding 10 new connections to : 80 in 605
$ iptables -I INPUT -p tcp --dport 80 --syn -m recent --name SYN_FLOOD --update --seconds 60 --hitcount 10 -j REJECT


•*Note:** if the attack is volumetric and saturates the link or CPU, local mitigation fails because you may lose even SSH access. Volumetric DDoS must be handled upstream (ISP/scrubbing center/CDN).
## Observable impact
1.    **Software-interrupt (softirq) CPU usage rises** as the kernel processes the packet flood - check with mpstat -P ALL 1° (*%soft) or •top.
2.    **Request latency increases** - network delay climbs.
3.    **TCP connection slots are consumed** (half-open or otherwise), blocking | Legitimate clients.
## Troubleshooting workflow
1.    Confirm the symptom: "curl returns code 060 / timeouts while the host is reachable at L2.
2.    Check interface rate and direction (sar -n DEV 1°) - high inbound PPS with small packets suggests a flood.
3.    Check CPU softirq (mpstat -P ALL 1°); a single core at 100% %soft" points to packet-processing saturation (consider RPS/RFS, multi-queue).
4.    Classify the attack: protocol (SYN flood → 'ss -n state syn-rec), volumetric (bandwidth saturated), or application (HTTP flood → app logs).
5.    Apply the matching defense layer; escalate volumetric attacks upstream.
## Pitfalls
-   A saturated uplink cannot be fixed on the host - you need upstream help.
-   Single-IP blocking fails against spoofed/random sources (*--rand-source*).
-   Aggressive local rate limits can drop legitimate traffic and still not stop a distributed attack.
-   One pegged softirg core can look like a CPU problem; the root cause is RX steering, not application CPU.
# Likely interview questions
-   Name the three DoS exhaustion categories and a defense for each.
-   Why can't a host defend against a volumetric attack on its own?
-  How do you tell a SYN flood from an application-layer HTTP flood?
-   What does high inbound PPS with near-zero outbound PPS indicate?
-   How do RPS/RFS and multi-queue help under a packet flood?
## Related notes
-   SYN flood deep dive: " syn_attack.md.
-   High-PPS packet handling and DP: ^c10k_c10M_problem.md®.
-   Interface/PPS observability (sar", "ss, "nstat"):
1 *network_configuration.md,
## References
- Course material; Geek Time, "Linux Performance Optimization In Practice".