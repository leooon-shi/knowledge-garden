<!-- performance/network/practice_1.md -->
# Practice Case Study 1: High Latency and Packet Loss Under Load
A realistic, step-by-step troubleshooting walkthrough for a web service that becomes slow and starts dropping requests under load. The point is the method: form a hypothesis, confirm with the right tool, fix, verify.
## Scenario
- p99 latency jumps from 20 ms to > 2s,
An Nginx-fronted API runs fine at low traffic. During a traffie spike, ellents report!
-   intermittent connection timeouts / resets,
-   error rate climbs even though CPU on the box is *not* saturated.
Host: Ubuntu 24.04, kernel 6.8, single 25G NIC, NAT/contrack in path.
** Step 0 - Reproduce and scope
Generate steady load and measure the tail (not just the average):
•* console
$ -d60s-R50800 --Jatencyhttp://ap1.internal/healt/
*.:. Latency Distribution: 99% 2.145 (conflrus the tall problem)
Check the obvious resource first - is it CPU/softirq or network?
** console
$ mpstat -P ALL 1
•Xusr low, %sys moderate, but %soft high on CUe only → single-CUsoftirq bottleneck?
$ sar -n DEV 1
• rapek/s very high, rak/s moderate + smal1 packets, high PPS
Hypotheses to test, cheapest first:
1.    Accept-queue (listen backlog) overflow - app can't accept fast enough.
2.    NIC ring / softirq backlog drops - packet-rate bound, one CPU.
3.    contrack table full - NAT in path.
*  # Step 1 - Connections and accept queue
*  * console $ ss -s
Total: ..•
TCP: 1800 (estab 1700, closed 20, orphaned 5, timewait 300)
$ ss -t1
State
* listening sockets: Recv-Q vs Send-Q
Recv-Q
Send-Q
Local Address:Port
LISTEN 512
512
0.0.0.0:80
••.
* Recv-Q = Send-Q == full accept queue!
* Recv-0*
on a LISTEN socket is the current accept-queue depth; "Send-Q' is its max
(bounded by
*min(somaxconn, backlog)). "Recv-Q" pinned at "Send-Q = overflow.
Confirm with the counter (use deltas, not lifetime totals):
** console
$ nstat -z>/dev/nu11 ; sleep 10; nstat | grep -iE "ListenOverflows|ListenDrops" TpExtListenDverflows
4213
421.3
* 421/s - accept queue overflowing
IspEstListenDrops
4213
421.3
*  *Root cause #1 found:** the accept queue overflows. Overflowing SYN-ACKed connections are dropped; clients retransmit, adding ~15 (SYN-ACK RTO) of latency - exactly the tail we saw, The app's "listen(backlog) and/or •net.core.somaxconn® is too small.
*  ^ console
$ sysct1net.core,somaxconn
net, core, somaxconn = 4096
*   kernel default on 6.8 (was 128 before 5.4l)
*   Correction to a common outdated note: "somaxconn"'s default is **4096** since kernel
*   5.4; the old "128" value is frequently cited but wrong on modern kernels. If the app
*  st111 passes *backlog-128" to listen(), it is capped at 128 regardless of
*   somaxconn®- the app config matters too,
*  *Fix #1:**
•* console
* kernel ceiling
$ sudo sysctl -w net.core,somaxconn-8192
AND raise the app's listen backlog (nginx: 11sten 80 backlog-8192;) then reload 5 sudo sysctl-wnet. spva.tcp_nax_syn_backlog-8192 • SYN (half-open) queue too
Re-test: "ListenOverflows" delta drops toward e, but p99 is st111 ~800 ms, There's a second bottleneck - keep going.
## Step 2 - Softirq / NIC drops (the high %soft clue)
•* console
$ awk 'fprintf "cpu%-3d processed-%d dropped-%d squeezed-%d\n*, NR-1, strtonum("ex"$1), strtonum("0x"$2), strtonum("0x"$3)} /proc/net/softnet_stat
coup
processed=98123456 dropped=20451 squeezed=8832 • drops + squeeze on cpu0 only
•


cpul
processed-128 dropped-e squeezed-e
*** console
$ ethtool -1 enpise
Combined: 1
rx_missed_errors: 28451
$ ethtool -g enpise
RX: 512
(max 4896)
*   ring overrun confirms it
*   tiny ring
*  Root cause #2:** al1 RX handled by one CPU (no RSS/RPS) with a small ring → ring overruns and softirq backlog drops under PPS spikes.
*  *Fix #2:**
•*console
*   use more NIC queues (RSS) so Rspreads across CPUs $ sudo ethtool -L enpise combined 8
*   grow the ring to absorb bursts $ sudo ethtool -G enpise rx 4096
*   bigger per-CPU backlog + NAPI budget
5 sudo sysctl-w net.core.netdev_max_backlog-16384
$ sudo sysctl -w net.core.netdev_budget-600
* spread IRQ (or let irqbalance do it) sgrep enple /proc/interrupts
* find IRQ numbers, pin via /proc/irq/N/smp_affinity
Re-test: %soft'
now spread across Pus, softnet_stat" drops stop growing, p99 down to
-120 ms. Better, but still occasional resets under the heaviest load - check the NAT
## Step 3 - conntrack table (NAT in path)
•* console
$ sysctl net.netfilter.nf_conntrack_max net.netfilter.nf_conntrack_count
net.netfilter.nf_conntrack max - 262144
net.netfilter.nf_conntrack_count = 261990
# ~100% ful1！
$ dmesg | grep -i contrack
L:*• 1 nf_conntrack: nf_contrack: table full, dropping packet
**Root cause #3:** the contrack table is full; new flows get their packets dropped, showing up as timeouts/resets. High connection churn + short-lived connections + long TIME_WAIT-style contrack timeouts exhausted the table.
#*Fix #3:38
•** console
# raise the table size (costs ~300 bytes/entry → size for peak flows)
$ sudo sysctl- net.netfilter.nf_conntrack_max-1048576
# bump the hash buckets to keep lookups o(1)
(If NAT/contrack isn't actually needed on this path, the better fix is to remove it - see "NAT, md"-)
## Step 4 - Verify end to end
Re-run the exact same load and compare the *same* deltas you used to diagnose:
•* console
"ListenOverflows|RetransSegs"
* ListenOverflows 0, RetransSegs delta tiny $ auk 'fprint strtonum(" 0x"$2)} /proc/net/softnet_stat
• dropped == eacross CPUS
3 sysct1 net.netfilter,nf_conntrack_count
• well below max
* wrk2: 99% 95ms → tall Latency recovered
Confirm with s5 -tithat retransmits per socket are near zero and TT 1s back to baseline.
## Summary of root causes and fixes
1 * 1 Symptom | Tool that proved it | Root cause | Fix |
--|---|---|---|---
i 1 | ~15 tail, timeouts | •ss -tl* Recv-Q, "nstat" ListenOverflows | Accept queue too small | f"somaxconn **and** app
• listen(backlog) |
121 high %soft on CUe, loss | softnet_stat*, "ethtool-S/-1/-8 | Single R queue + small ring | RSS ('-L"), bigger ring (-G"), RPS, backlog/budget |
I 3 | resets at peak | nf_conntrack_count" vs *_max', "dmesg | conntrack table full | f nf_conntrack_max* + hashsize + shorter timeouts

## Lessons / method
-   Measure the *tail**, drive load at a **fixed rate** (wrk2) to get honest p99.
-   Use **deltas** (nstat -2" then "nstat"), not lifetime totals, during an incident.
-   A not-PU-saturated box can still drop packets - look at per-CPU %soft" and
"softnet_stat, not just overall cPU.
- Fix one confirmed cause at a time and re-measure; multiple bottlenecks stack.
*Recv-Q'on a LISTEN socket is the cheapest accept-queue-overflow signal there is.
## Likely interview questions
-   A box isn't CPU-bound but drops packets under load - where do you look?
-   How do you distinguish accept-queue overflow from SYN-queue overflow?
-   Walk through diagnosing a single-R-queue softirq bottleneck.
-   How would you detect and fix a full contrack table?
-   Why use 'nstat deltas and fixed-rate load generators?
## Related notes
-   Tools and counters; "diagnostic_tools. md, "packets_tools.md
-   Stack internals and drop points: "linux_network_stack.md"
-   NAT/conntrack detail; *NAT.md*
-   SYN flood specifics: syn_attack. md"
## References
-   Geek Time course "Linux Performance Optimization in Practice".
-  man man "man nstat", "man ethtool", "Documentation/networking/scaling.rst.
