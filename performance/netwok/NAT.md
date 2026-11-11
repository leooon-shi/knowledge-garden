<!-- performance/network/NAT.md -->
# NAT (Network Address Translation)
Network Address Translation (NAT) rewrites the source or destination IP address (and often the port) of IP packets as they traverse a router or gateway.
Why NAT matters:
1.    NAT can rewrite the source or destination IP of a packet, so it is widely used to work around the shortage of public IPv4 addresses. It lets many hosts on a private network share a single public IP to reach the Internet.
2.    By hiding the internal network, NAT also provides a degree of security and isolation for machines on the LAN.
3.    In Linux, NAT is implemented on top of the kernel's connection-tracking (contrack) module. Tracking per-connection state has a real cost, so NAT has a measurable impact on network performance.## Types of NATBy how addresses are mapped, NAT falls into three categories:
4.    **Static NAT** - a fixed one-to-one, permanent mapping between a private IP and a public IP.
5.    **Dynamic NAT** - a private IP is mapped to a public IP chosen dynamically from a pool.
6.    **NAPT (Network Address and Port Translation)** - maps private IPs onto different ports of a single public IP, so many private hosts can share one public address. This is the form used by almost all home routers and cloud NAT gateways. NAPT itself has three sub-types:
- **SNAT(Source NAT)** - the destination is unchanged; only the source IP and/or source port is rewritten. Used so multiple internal hosts can share one public IP to reach external resources.
**DNAT (Destination NAT)** - the source is unchanged; only the destination IP and/or port is rewritten. Used to expose multiple internal services through different ports of a public IP, while hiding the backend server's real IP.
-**Bidirectional NAT**- both SNAT and DNAT at once. On receive, DNAT maps the public destination to an internal IP; on send, SNAT maps the internal source to the public IP. This is a one-to-one mapping between a public and private IP, commonly used in virtualized environments to assign floating public IPs to VMs.
## Worked example of SNAT and DNAT
Assume:
-   Local server internal IP: 192.168.0.2"
-   NAT gateway public IP: •100.100.100.100°
-   Destination server "baidu.com": "123.125.115.110°
When the server reaches out to "baidu.com, the NAT gateway rewrites the source address from the internal •192.168.0.2" to the public "100.100.100.100" before forwarding the packet. When "baidu.com" replies, the gateway rewrites the destination from *100.100.100.100° back to "192.168.0.2" and delivers it to the internal server.
## iptables / nftables and NAT
The Linux kernel's **Netfilter** framework allows packets to be modified (e.g.
NAT) and filtered (e.g. firewalling). On top of Netfilter, user-space tools provide friendlier command-line interfaces:
-   **iptables / ipotables / ebtables** - the classic (legacy) interface.
-   **nftables ("nft")** - the modern replacement. Since Debian 10 / RHEL 8 / Ubuntu 20.04,iptables is usually the iptables-nft" shim that translateslegacy syntax to the nftables backend. **Correction vs. older notes: ** on a current system (Ubuntu 24,04, kernel 6.8) you should prefer nft for new rule sets; the "iptables" commands below still work through the nft shim.
### SNAT
SNAT is configured in the POSTROUTING chain of the nat" table.
To apply SNAT to a whole subnet and let the kernel piek the egress IP automatically - this is the common **MASQUERADE** target (It re-resolves the source IP on every connection, handy for dynamic/DHCP WAN IPs but Slightly more expensive than static SNAT):
•**bash
Iptables -t nat -A POSTROUTING -S 192,168.8.9/16 -3 NASQUERADE
To configure SNAT for a specific IP with an explicit translated source:
•••bash
iptables -t nat -A POSTROUTING -s 192.168.0.2 -j SNAT --to-source 100.100.100.100
Equivalent in nftables:
•**bash
nft add table ip nat

nft add chain ip nat postrouting (type nat hook postrouting priority 100 l; ) nft add rule ip nat postrouting ip saddr 192.168.0.0/16 masquerade nft add rule ip nat postrouting ip saddr 192.168.0.2 snat to 100.100.100.100
### DNAT
DNAT is configured in the "PREROUTING" or •OUTPUT' chain of the 'nat' table.
"PREROUTING" is more common because it also applies to forwarded packets:
•*bash
iptables -t nat -A PREROUTING -d 100.100.100.100 -j DNAT --to-destination 192.168.0.2
### Bidirectional NAT
Add both SNAT and DNAT rules to create a one-to-one mapping between a public and a private IP:
**bash
iptables -t nat -A POSTROUTING -S 192.168.0.2
-j SNAT --to-source 100.100.100.100
iptables -t nat -A PREROUTING -d 100.100.100.100 -j DNAT --to-destination 192.168.0.2
Don*t forget IP forwarding
When a box does NAT it must forward packets originating from other IPs, So IP forwarding must be enabled:
••bash
$ sysctl net.ipv4.ip_forward
net. ipv4.ip_forward - 1
To make the setting survive a reboot, persist it (modern systems use
-/etc/sysct1.d/*.conf; */etc/sysctl.conf" still works):
$ cat/etc/sysctl.conf| grep ip_forward
net.ipv4.ip_forward-1
## Analyzing a NAT performance problem
Tools used: "docker", "tcpdump", "curl, "ab", and a dynamic tracer. The notes originally used **SystenTap**; on kernel 6.8 the modern equivalent is
**bpftrace / BCC (eBPF)**, which needs no debug symbols or kernel-module compilation. SystemTap still works but is largely superseded. Both approaches are show below.
The case study is Nginx running in a Docker container, with Apache Bench (ab®) as the load client.
### Baseline (no NAT)
**bash
# Temporarily raise the max number of file descriptors for this shell
$ ulimit -n 65536
# -c 5000 : 5000 concurrent requests
# -n 100000: 100000 total requests
# -r: keep going when a socket receive error occurs
# -s 2: 25 timeout per request
$ ab
- 5000-n 100000-r-52 http://192.168.0.30/
Requests per second:
Time per request:
Time per request:
Transfer rate:
6576.21
[#/sec] (mean)
760.317 [ms] (mean)
0.152 [ms] (mean, across all concurrent requests)
5390.19 [Kbytes/sec] received
Connection Times (ms)
Connect:
Processing:
Waiting:
Total:
min
mean[+/-sd] median
7333
1
204 716.3
28
max
7338
952
7349
-   Requests per second: 6576
-   Mean time per request: 760 ms
-   Mean connect time: 177 ms
## Reproduce the NAT path with Docker
***bash
$ docker run --name nginx --privileged -p 8080:8080 -itd feisky/nginxinat
Inspect the NAT rules Docker installed:


***bash
$ iptables -nL
-t nat
Chain PREROUTING (policy ACCEPT)
target
prot opt source
DOCKER
all -- 0.0.0.0/e
•..
Chain DOCKER (2 references)
target
RETURN
prot opt source
DNAT
all
0.0.0.6/0
tcp
e.0.0.e/e
destination
0.0.0.0/0
ADDRTYPE match dst-type LOCAL
destination
0.0.0.0/0
0.0.0.0/6
tep dpt: 8080 to:172.17.0.2:8080
In "PREROUTING"
• packets destined to the local host jump to the 'DOCKER" chain;
there, TCP packets with destination port 8888 are DNAT'ed to •172.17.0.2:8080*, which is the Nginx container's IP,
Confirm Nginx is up:
***bash
$ curl http://192.168.0.30:8088/
<p>en>Thank you for using nginx.</em>e/p> /body> </html>
Now run the same load against the NAT'ed port:
•**bash
$ ab -c 5000-n 100000-r-s 2 http://192.168.0.30:8080/
•••
apr pollset poll: The timeout specified has expired (70007)
Total of 5602 requests completed
"ab fails with connection timeouts. With •-s 2° (25 per-request timeout) only 5682 requests completed. Raising the timeout to 30s:
***bash
$ ab -c 5000 -n 10000 -r-s 30 http://192.168.6.38:8088/
•••
Requests per second:
Time per request:
Time per request:
Transfer rate:
76.47 [*/sec] (mean)
65388.868 [ms] (mean)
13.076 [ms] (mean, across all concurrent requests)
44.79 [kbytes/sec] received
Connection Times (ms)
min
mean[+/-sd] median
Connect:
Processing:
Waiting:
Total:
0 1300 5578.0
1
0 37916 59283.2
2
8.7
1 39216 58711.6
1
1021
max
65184
1 130682
414
130682
-   Requests per second: 76
-   Time per request: 65 s
-   Connect time: 1300 ms
A catastrophic regression versus the baseline.
* How NATworks under the hood
From the Netfilter packet flow, NAT requires at least two things:
1.
**Hook functions** in Netfilter rewrite the source or destination address.
2, The **contrack** module associates the request and response of the same connection so the reverse translation can be applied.
# Finding the drops
A dropwatch-Style SystenTap script (historical approach):
•* systemtap
# /usr/bin/envstap
# Dropwatch.stp
# Author: Ne11 Horman <nhorman@redhat.com>
*   An example script to mimic the behavior of the dropwatch utility
*  http://fedorahosted.org/dropwatch
# Array to hold the list of drop points we find
global locations
# Note when we turn the monitor on and off
probe begin {printf("Monitoring for dropped packets\n")} probe end printf"Stopping dropped packet monitor")


*   increment a drop counter for every location we drop at probe kernel.trace(kfree_skb") {locations$location] « 1)
*   Every 5 seconds report our drop locations probe timer.sec(5)printf("\n")foreach (1 in locations-) {printf(d packets dropped at %sin",}
count(locations[2)), symname(2))
delete locations
This probes the kernel kfree_skb()'
call and counts where packets are dropped.
Run it:
•**bash
$ stap --all-modules dropwatch.stp
Monitoring for dropped packets
10031 packets dropped at nf hook_slow
676 packets dropped at tcp v4 rcv
7284 packets dropped at nf hook_slow
268 packets dropped at tcp_v4_rcv
**Modern equivalent (eBPF).** On kernel 6.8, use the BCC tool •dropwatch or
"bpftrace to trace the same kernel tracepoint without any setup:
•-bash
# BCC:
sudo /usr/share/bcc/tools/dropwatch
# if packaged
# bpftrace, counting drop locations by kernel stack:
sudo bpftrace -e tracepoint:skb:kfree_skb {@[kstack] - count(): }°
Then capture a CUprofile during load:
-*bash
$ ab -c 5000-n 10000-r-s 30 http://192.168.0.30:8080/
* record for ~30s, then Ctr1+C $ perf record -a -g -- sleep 30
# produce the report
S perf report -g graph,e
The profile shows nf hook_slow dominated by three callers:
*ipv4_conntrack_in,
"br nf pre_routing, and iptable_nat_ipv4_in°. In other
words, "nf hook slow is busy doing three things:
1. On receive, looking up the connection in the contrack table and allocating a tracking object (bucket) for new connections.
is implemented with a bridge.
3. On receive, performing the DNAT that forwards traffic on port 8080 into the container.
## Optimization: tuning contrack
DNAT depends on contrack, The kernel exposes several contrack tunables:
***bash
$ sysct1 -a | grep contrack
net.netfilter.nf_conntrack_count - 180
net.netfilter,nf_conntrack_max - 1000
net.netfilter,nf_conntrack_buckets - 65536
net.netfilter.nf_conntrack_tcp_tlmeout_syn_recv - 60
net.netfllter.nf_conntrack_tcp_tlmeout_syn_sent - 120
net.netfilter.nf_conntrack_tcp_t4meout_time_walt - 220
-   "net.netfilter.nf_conntrack_count' - current number of tracked connections.
-   "net.netfilter.nf_conntrack_max" - maximum number of tracked connections.
-   "net.netfilter.nf_conntrack_buckets" - size of the contrack hash table.
**Correction on defaults:** the '1000' / 65536" values above are from the course's constrained VM. On a modern host these scale with RAM. On this Ubuntu 24.04 / kernel 6.8 machine the observed defaults were
• nf_conntrack_max - 262144° and nf_conntrack_buckets - 262144°. Note the module may be named "nf_conntrack" (the old •nf_conntrack_lpv4 split was merged years ago), and the sysctls 11ve under •net.netfilter. * once the module is loaded ("net.nf_conntrack_max" is an alias).


During the overloaded "ab run the kernel logs table exhaustion:
***bash
$ dmesg | tail
[104235.156774] nf_conntrack: nf_conntrack: table full, dropping packet
[184243.880401] netratelimit: 3939 callbacks suppressed
[184243.888401] nf_conntrack: nf_conntrack: table full, dropping packet
[184262.962157] of_conntrack: nf_conntrack: table full, dropping packet
*net_ratelimit®means many log 1ines were suppressed - a kernel defense against
1og flooding.
*nf_conntrack_buckets" is the hash-table size. Each bucket is a 1inked 11st, so the average list length is 'nf_conntrack_max / nf_conntrack_buckets. Rough memory estimate:
••°text
* conntrack object w376 B, list entry ~16 B nf conntrack_max * object_size + nf_conntrack_buckets * entry_size
-   1000*376 + 65536*16 B
*   1.4 MB
Raise the limits:
***bash
$ sysctl
-w net.netfilter.nf_conntrack_max-131072
s sysetl -w net-netfllter.nf_conntrack_buckets-65536
Re-run the load:
***bash
$ ab -c 5000 -n 100000 -r-s 2 http://192.168.0.30:8088/
Requests per second:
Time per request:
Time per request:
Transfer rate:
6315.99 [$/sec] (mean)
791.641 [ms] (mean)
0.158 [ms] (mean, across all concurrent requests)
4985.15 [bytes/sec] received
Connection Times (ms)
แม็ก
Connect:
Processing:
Maiting:
Total:
15
mean [+/-sd]
median
355 793.7
311 855.9
2 ที
292 851.5
36
666 1216.3
148
max
7352
14481
14481
14645
-   Requests per second: 6315 (vs. 6576 without NAT)
-   Time per request: 791 ms (vs. 760 ms without NAT)
-   Connect time: 355 ms (vs. 177 ms without NAT)
Near-baseline performance restored, with a small NAT overhead.
## Reading the conntrack table
*  **bash
*  -L list, -o extended = extended format
$ conntrack-L-o extended | head
2pva 2 tcp 67 TIME_WAIT 5rC-192.168.0.2 dst-192.168.8.96 sport-51744 dport-8980 src-172.17.0.2 dst-192.168.0.2 sport-8080
dport-51744 [ASSURED] mark-0 use-1
ipv42 tcp 66 TIME_WAIT SrC-192.168.0.2 dst-192.168.0.96 sport-51524 dport-8080 src-172.17.0.2 dst-192.168.0.2 sport-8080
dport-51524 [ASSURED] mark-® use-1
Each entry carries protocol, connection state, source IP/port, destination
IP/port, and tracking flags, The fixed format lets you aggregate with
I
***bash
# total tracked connections
$ conntrack -L -o extended | wc -1
14289
* TCP connections by state
3 conntrack -L -o extended | awk/^,*tcp.*s/ {sum[$6]++) END {for(1 In sum) print 1, sum(t])°
CLOSE WAIT 9
ESTABLISHED 2877
FIN WAIT 3
SYN_SENT 2113
TIME_WAIT 9283
#tracked connections by source IP
$ conntrack -L -o extended | awk '{print $7)' | cut -d "-" -f 2 | sort | unfą -c | sort -nr | head -n 10
14116 192.168.0.2
172 192.168.0.96
Most TCP
Contract entaisc are in TIME_WAIT, nearly all from 192.168.0.2


Most TCP contrack entries are in "TIME_WAIT, nearly all from "192.168.0.2" (the VM running "ab"). "TIME WAIT entries are reclaimed after a timeout; the default is 120 s:
**bash
$ sysctlnet.netfilter.nf_contrack_tcp_timeout_time wait
net.netfilter.nf_conntrack_tcp_timeout_time_watt - 120
If your connection rate is very high, consider reducing this timeout - but with care (see trade-offs below).
## Tuning knobs (contrack-focused)
I sysctl | Typical default (modern) | Effect | Trade-off |
١ح|اا
I net.netfilter.nf_conntrack max" | scales with RAM (e.g. 262144) | Max tracked connections | Too low + *table fu11" drops; too high→ more RAM (~376B/entry) |
I "net.netfilter.nf_conntrack_buckets" | scales with RAM (e.g. 262144) | Hash-table size | Too small + long bucket chains, Slower lookups |
I net.netfilter.nf_contrack_tcp_timeout_time_wait*|120s |How long TIME_WAIT entries are kept | Lower frees slots faster but can confuse late/retransmitted packets |
I net.netfilter.nf_conntracktcp_timeout_established | 432800s (5 days) | Idle timeout for ESTABLISHED | Lower reclaims idle entries but may prune valid long-lived flows |
Inet. ip4.ip_forward||Enable forwarding | Required for any NAT/router box I ## Troubleshooting workflow
1.    Confirm the symptom: curl" latency/timeouts,
2.    Check "dmesg' for "nf_conntrack: table full'.
3.    Compare "nf_conntrack_count against "nf_conntrack max .
4.    Inspect the contrack table state distribution (conntrack -L | awk ..•).
5.    Profile the kernel with perf record-a-g" and look for nf_hook_slow dominating; drill into "ipv4_conntrack_in", bridge, and NAT hooks.
6.    Trace drops with "bpftrace /BCC "dropwatch (or SystemTap on older kernels).
7.    Raise nf_conntrack_max /buckets; reduce relevant timeouts if TIME_WAIT dominates; consider skipping contrack for high-volume flows with iptables -t raw ... -j NOTRACK* / nftables "notrack" where NAT is not needed.
## Pitfalls
-  **conntracktable exhaustion** silently drops packets and manifests as random latency, not errors - always check dmesg.
-  **SNATport races:** when many containers SNAT from the same source IP, they all start searching for a free source port from the low end; inserting into the contrack table is not transactional, so two flows can pick the same tuple and one insert fails, dropping the SYN. The retransmit (after ~15 or 35 RTO) picks a new port. The symptom is occasional 1s/3s connection latency. Mitigate with *--random-fully on the MASQUERADE/SNAT rule.
-   Enabling the bridge-netfilter path ("br_netfilter") runs iptables on bridged traffic, adding the "brnf pre routing" cost seen above.
## Related notes
-   Packet flow through the stack: see •linux_network_stack.md".
-   Packet-level buffers: see "buffer.md".
-   Packet capture tools: see "packets_tools.md" and"packet_transition.md".
-   General diagnostic tools: see "diagnostic_tools.md".
## Likely interview questions
-   Explain SNAT VS, DNAT V5, MASQUERADE and when you'd use each.
-   How does Linux associate a NAT reply with the original request? (conntrack)
-   What happens when the contrack table fills up, and how do you detect it?
-   Why 15 MASQUERADE slightly more expensive than static SNAT?
-  How would you profile a NAT gateway that suddenly adds latency?
-  What 15 the SATsource-port race and how do you fix it random-fully)
-   iptables vs, nftables on current distros - what actually runs in the kerned
## References
- SNATport-allocation race (15/35 Latency) anaLysis: <https://mp.weixin.gg.com/s/VBs81gf0HsNg9WAxktZYQ>
