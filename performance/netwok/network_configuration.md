Ki-- performance/network/network_configuration.md --
* Network Configuration and Basic Observability
How to inspect interface configuration, socket state, protocol statistics, throughput/PPS, and connectivity on Linux.
#* Interface configuration and status
Two tool families: "ifconfig" (from the old "net-tools") and ip*
(from
iproutez"). *prefer "ip"** - "net-tools is deprecated and missing newer features.
•••bash
ip-s addr show dev ethe ifconfig ethe
•*bash
$ ifconfig ethe
ethe: flags-4163<UP, BROADCAST, RUNNING, MULTICAST> mtu 1500
inet 10.240.0.30 netmask 255.240.0.0 broadcast 10.255.255.255
inet6 fe80::20d: 3aff:fe07:cf2a prefixlen 64 scopeid 0x20<11nk>
ether 78:0d:3a:07:cf:3a txqueuelen 1000 (Ethernet)
RX packets 40809142 bytes 9542369803 (9.5 GB)
RX errors e dropped overruns e frame e
TX packets 32637401 bytes 4815573306 (4.8 GB)
TX errors dropped overruns carrier co11151ons ®
$ ip -s addr show dev ethe
2: ethe: <BROADCAST,MULTICAST,UP, LOWER_UP> mtu 1500 qdisc mq state UP group default qlen 1000 link/ether 78:0d:3a:07:cf:3a brd ff:ff:ff:ff:ff:ff
inet 10.240.0.30/12 brd 10.255.255.255 scope global ethe
valid_lft forever preferred_lft forever inet6 fe80::20d:3aff:fe07:cf2a/64 scope link
valid_lft forever preferred_ift forever
RX: bytes packets errors dropped overrun mcast
9542432350 40809397 6
193
TX: bytes packets errors dropped carrier collsns
4815625265 32637658 0
٠٠-
Both show interface flags, MTU, IP/subnet/MAC, and send/receive counters. What to read:
1.    **Link status,** *RUNNING" (ifconfig) or LOWER_UP" (ip) means the physical link is up - the NIC is connected to a switch/router. Missing it usually means the cable is unplugged (or the virtual link is down).
2.   *NTU.** Default 1500. Raise or lower it depending on architecture (eg. overlay networks like VLAN).
3.    **IP, subnet, MAC.** Must be correct for the interface to work.
**Counters,** Nonzero errors, dropped, "overruns,
carrier, or
"collisions on TX/RX usually signal a network I/0 problem:
"errors - packets with errors (checksum, frame sync, etc.).
- "dropped*-packets reached the ring buffer but were dropped, e.g. out of memory.
overruns-ring buffer overflowed because I/o arrived faster than it could be drained (queue full).
"carrier" - carrier errors, e-g. duplex mismatch or a bad cable.
I - "collisions - packet collisions.
For detailed, driver-level counters use "ethtool -S ethe (e.g.
"r*_dropped", "rx_missed_errors", per-queue stats).
## Sockets
Interface-level counters come from "ifconfig /ip'. For protocol-stack and per-socket state use ** ss***, which is faster than 'netstat". Their output is similar: socket state, receive/send queues, local/remote address, and the owning PID and process name,
*  * bash
*   head -n3: first 3 lines
# -1: listening sockets only
# -n: numeric addresses and ports
* -p: show process info
S netstat -nip | head -n 3
Active Internet connections (only servers)
Proto Recv-Q Send-Q Local Address tcp
• 127.0.0.53:53
Foreign Address
0.0.0,0;*
State
LISTEN
# -1 listening, -t TCP, -n numeric, -p process
$ ss -1tnp | head -n 3
State
Recy-Q
Send-Q
Local Address:Port
LISTEN
128
127.0.0.53%1o:53
LISTEN
128
0.0.0.0:22
Peer Address: Port
0.0.0.0:*
0.0.0.0:*
PID/Program name
840/systemd-resolve
### Reading Recv-Q and Send-o
They should normally be e. Nonzero means packets are p11ing up - but the meaning depends on socket state:
users: ((*systend-resolve,pid-840, fd=13))
users: ((®sshd.pid-1459, fd-3))

-   •*Established:**
-   *Recv-Q-bytes received into the socket buffer not yet read by the app.
-   "Send-Q" - bytes sent but not yet Acked by the peer,
-   **Listening:**
-   "Recv-Q - current length of the **accept (completed) queue**.
-   "Send-Q-the **maximum** accept-queue length (effectively
*  min(backlog, net.core.somaxconn)).
*  *Correction on the "somaxconn default:** historically 128 (seen above), but it was raised to **4096** in kernel 5.4+, On this host:
•**bash
$ sysctl net.core.somaxconn
net.core.somaxconn = 4896
•..
### Accept queue vs. SYN queue
-**Accept (completed) queue / "full connection":** after the server receives the client's ACK (three-way handshake done), the connection is moved here. It must be taken by
*accept) before the server can process the request.
- **SYN queue / "half connection":** connections that have not finished the handshake. After receiving the client SYN, the server puts the connection in the SYN queue and replies SVNACK. (See "syn_attack.md".)
Watch accept-queue overflows with:
••*bash nstat -az
nstat -az
grep -i ListenOverflow grep -1 ListenDrops
#* Protocol-stack statistics
***bash
$ netstat -5
Tcp:
3244906 active connection openings
23143 passive connection openings
115732 failed connection attempts
2964 connection resets received
1 connections established
13825010 segments received
17606946 segments sent out
44438 segments retransmitted
42 bad segments received
5315 resets sent
InCsumErrors: 42
$ 55 -5
Total: 186 (kernel 1446)
TCP:
4 (estab 1, closed 6, orphaned 0, synrec e, timewait 8/0), ports 0
Transport Total
IP
IPvб
1
2
•netstat -s
shows active/passive opens, failed attempts, segments sent/received
and retransmitted, etc, **Prefer "nstat'** for scripting - It reads the same SNMP counters and can show deltas:
•**bash nstat -az
nstat
799
* all counters, absolute
# delta since last run
nstat -az | grep -iE 'retrans|ListenDrop|ListenOverflow|SyncookiesSent'
Retransmit ratio - TcpRetransSegs / TpoutSegs*; a rising ratio indicates loss.
## Throughput and PPS
•sar-n reports network statistics: Interface ("DEV'), Interface errors
(EDEV), plus "TCP*, "UDP", "ICMP", etc.
•** bash
* the trailing 1= emit one sample per second $ sar -n DEV 1
Linux 4.15.0-1035 (ubuntu)
01/06/19
_x86_64- (2 CPU)
13:21:40
13:21:41
13:21:41
13:21:41
IFACE ethe dockert
10
rxpck/s
18.00
0.00
0.00
txpck/s
20.00
0.00
0.00
5.79
0.00
0.00
txkB/s
4.25
0.00
0.00
rxcmp/s
0.00
0.00
0.00
txcmp/s
0.00
0.00
0.00
rxmcst/s
0.00
0.00
0.00
%ifut11
6.00
0.00
6.60

-   rxpck/s, txpck/s" - receive/send PPS (packets/s).
-   -rxkBIS'txkB/s" - receive/send throughput (KB/s).
-   *rxcmp/s°, *txcmp/s° - compressed packets/s.
-   "%ifutil" - interface utilization: half-duplex
(rxkB/s+txkB/s)/Bandwidth; full-duplex maxrxkB/s, txkB/s)/Bandwidth .
Get the link bandwidth with 'ethtool (units are usually Gb/s or Mb/s - note the lowercase "b" means *bits**, not bytes):
***bash
3 ethtool ethe|grep Speed
Speed: 1000Mb/s
٠٠٠
## Connectivity and latency
ping" tests reachability and round-trip latency using ICMP:
***bash
# -c3: stop after three ICMP echoes
$ ping -c3 114.114.114.114
PING 114.114.114.114 (114.114.114.114) 56(84) bytes of data. 64 bytes from 114.114.114.114: 1cmp_seq=1 tt1-54 time-244 ms
64 bytes from 114.114.114.114: 1cmp_seq=2 tt1-47 time-244 ms
64 bytes from 114.114.114.114: icmp_seq-3 ttl-67 time-244 ms
--- 114.114.114.114 ping statistics --.
3 packets transmitted, 3 received, 0% packet loss, time 2001ms rtt min/avg/max/mdev - 244.023/244.070/244.105/0.034 ms
-   Part 1: per-request info - ICMP sequence ("icmp_seq), TTL (hop budget), and
-   Part 2: summary of the three requests (packets sent/received, loss, RTT min/avg/max/mdev).
Note TTLcan vary per reply, which hints at ECMP/multipath routing. For per-hop latency use "mtr" or "traceroute".
## Troubleshooting workflow
1.    Confirm the link is up (ip link", look for "LOWER UP"); check ethtool" speed/duplex.
2.    Check interface counters (ip-s link, "ethtool "ethtool-5) for errors/drops.
3.    Check socket queues (ss -Itn* for accept-queue overflow; "ss -ti for per-connection cund/rtt/retrans).
4.    Check protocol counters (nstat", retransmit ratio, ListenDrops).
5.    Measure throughput/PPS over time ("sar -n DEV 1°).
6.    Test reachability and RTT (ping, "mtr®).
## Pitfalls
-   "net-tools (ifconfig/"netstat®) is deprecated and can miss namespaces, newer address flags, and multiqueue details - use "ip/ss/'nstat®.
-   somaxconn default changed to 4096; stale tuning guides assume 128.
-   "%ifutil from "sar" can mislead on full-duplex links; prefer per-direction
-   A nonzero "Recv-Q* on a *Listening* socket means accept-queue backlog, not unread data - different meaning than on an established socket.
## Related notes
-   Packet-level diagnostic tools: see "diagnostic_tools.md".
-   Packet capture ("tcpdump"/" wireshark"): see "packets_tools.md".
-  Ring buffers and socket buffers: see "buffer, md".
#* Likely interview questions
-   Difference between the SYN queue and the accept queue; how to detect overflow.
-  What do Recy-Q/Send-Qmean on 11stening vs, established sockets?
-  How do you compute interface utilization and TCP retransmit ratio?
-   Why prefer "ss" I'nstat" over "netstat'?
-   What does a nonzero 'ethtool -s ra_missed_errors' indicate?
## References
-   Course material; Geek Time, "Linux Performance Optimization in Practice".
-  Related article: <https://mp.weixin.49.com/s/VH3PZGEropbpA-AwAMythQ>

