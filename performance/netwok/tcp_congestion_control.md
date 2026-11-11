performance/network/tcp_congestion_control.md -->
ip_congestion_control.md ...
# TCP Congestion Control
At the start of a TCP transfer, the speed ramps up from low to high- it rarely begins at full speed. This behavior is tied directly to TCP congestion control.
This document explains what the congestion window is and how TCP detects and avoids congestion.
To cope with the complex and unpredictable conditions of the internet, TCP uses a congestion-control mechanism to keep transmission fast and stable. It has four key phases:
1.    Slow start
2.    Congestion avoidance
3.    Fast retransmit
4.    Fast recovery
#* Slow start
Slow start means a TCP transfer begins at a relatively low speed (hence "slow").
In fact, during this phase the congestion window grows by *doubling*, so judging by the growth pattern you could just as well call it "fast start."
In this phase, every time TCP receives an ACK that acknowledges data, the congestion window increases by one MSS.
The ACK must acknowledge *new* data, not be a duplicate ACK. For example, if you receive two ACKs with the same acknowledgment number, the second one is not an
"ACK that acknowledged data," so the window grows by one MSS, not two.
Slow start ends when one of two things happens:
-   Congestion is encountered, or
-   The congestion window reaches the *slow-start threshold**.
### Slow-start threshold (ssthresh)
The slow-start threshold (ssthresh) is the point past which the congestion window's growth slows sharply: instead of growing by one MSS per data-acknowledging ACK, it grows by only one MSS per RTT.
Example: with an initial congestion window (ICW) of 4 MSS and an ssthresh of
32 MSS, after each RTT in slow start the window becomes 8, then 16, then 32 MSS
- exponential growth. Once it reaches the threshold, TCP enters congestion avoidance, growing by only one MSS per RTT, which appears on a graph as a low-slope straight line.
The initial ssthresh can be set aggressively high. When congestion occurs (a timeout or three duplicate ACKs), ssthresh is reduced to half of the congestion window at the congestion point - now a value adapted to real conditions rather than the initial guess. So after the first slow start, TCP usually does not proactively enter congestion avoidance; it waits until congestion appears to obtain its first realistic ssthresh.
### Interval (delayed) ACKs
Many TCP implementations (e-g.Windows) acknowledge every other segment rather than each one:
-   Receive 1 and 2 → acknowledge 2
-   Receive 3 and 4 → acknowledge 4
In Wireshark, selecting segment 17 highlights the segment it acknowledges (segment 16); you can also match them by comparing NextSeg and ACK.
Such interval ACks can make the congestion window grow more slowly than when every segment is acknowledged.
## Congestion window (cwnd)
The congestion window (CHND, or CW) is the sender's 11mit on outstanding data.
The **initial** congestion window is ICW (or IW),
-   Before Linux kernel 3.0, the initial congestion window was small - 2 to 4 MSS.
-   From Linux 3.e onward,
TCP_INIT_CWND* 1s defined as 10 (see
*include/net/tcp.h),
The congestion window is maintained **per connection**, If one host has two TCP connections transferring data, each maintains its own congestion window independently - one can be large and the other small.
### Window and MSS
A window is some number of MSS. The MSS has a definite upper bound, typically 1460 bytes. Enabling TCP options such as timestamps subtracts from that, so the MSS drops below 1460 (e.g.
1440). The window's unit is bytes, so it is often not
an exact multiple of the MSS - which is normal.
## Congestion avoidance
After passing the slow-start threshold, TCP enters congestion avoidance, characterized by **AIMD** - Additive Increase / Multiplicative Decrease.


Because of AID, the congestion window grows by only one MSS per RTT, so growth is linear until congestion is detected. On congestion the window drops by *half* (multiplicative decrease).
## Fast retransmit
For every segment it sends, TCP starts a retransmission timer. If no acknowledgment arrives within the 1imit, the sender assumes the segment was lost and retransmits it - this is **timeout retransmission**.
TCP*s minimum retransmission timeout is generally about 200 ms. While timeout retransmission solves packet loss, it introduces a new problem: waiting 280 ms (or more) per loss hurts latency-sensitive applications.
**Fast retransmit** addresses this wasted time: as soon as the sender receives three duplicate acknowledgments (four counting the first acknowledgment), it retransmits the segment immediately instead of waiting for the timer.
## Fast recovery
Fast recovery, introduced by the TCP Reno algorithm, works together with fast retransmit. Unlike the older slow start → congestion avoidance slow start + congestion avoidance® pattern, after a congestion point reached via fast retransmit, TCP does **not** re-enter slow start. Instead it starts from the halved congestion window and continues the same linear growth as congestion avoidance, until the next congestion point.
## Tuning
### Change the initial congestion window (ICW)
When a link's network conditions are poor and a lower ICW is more appropriate, you can change it:
•*bash
# Find the current route entry and copy the whole line (call it <item›):
$ ip route
default via 10.0.2.2 dev enp0s3 proto dcp src 10.0.2.15 metric 180
# Change that route's initial congestion window to n (e.g. 2):
$ sudo ip route change default via 10.0.2.2 dev enp0s3 proto dhcp \
src 10.0.2.15 metric 100 initcwnd 2
**# Change the congestion-control algorithm
On Linux, use sysctl to view or change the algorithm. Linux defaults to the
"cubic' algorithm:
•**bash
$ sysctlnet.ipv4.tcp_congestion_control
net.ipv4.tcp_congestion_control = cubic
If the kernel is newer than 4.9, BBR is already available:
•*bash
$ sudo sysctl net.ipv4.tcp_congestion_control-bbr # use BBR
net.ipv4.tcp_congestion_control bbr
$ sudo sysct1 net.core.default_qdisc-fq
net.core,default_gdisc = fa
# adjust the queueing discipline
Write these into */etc/sysctl,conf to persist them across reboots.
BBR** is a newer algorithm proposed by Google in 2016. Its motivation: modern network devices have ever-larger buffers, so packet loss is less frequent than in the small-buffer era, but packet delay* is worse, To detect congestion more accurately, BBR focuses more on delay and adjusts the congestion window based on changes in delay.
## See also
-[tcp_wireshark_troubleshooting.md](tcp.wireshark_troubleshootIng.md) - case
studies that observe these mechanisms (slow start, fast retransmit, window limits) with Wireshark.