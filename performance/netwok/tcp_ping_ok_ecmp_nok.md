<-- performance/network/tcp_pang_ok_emp_nok-ma »
#Why Ping Succeeds but TCP Connections Intermittently Fail (ECMP)
A classic, confusing symptom: "ping" to a destination works every time, yet TCP connections to the same* destination sometimes fail. In a network using **ECMp** this is expected behavior, and understanding why is the key to diagnosing it.
## What ECMP is
ECMP (Equal-Cost Multi-Path) lets a router use several 1inks at once toward the same destination network, multiplying the avalable bandwidth. (On a Linux server, NIC **bonding** achieves a simtiar bandwidth-aggregation effects some bonding modes require switch cooperation.)
Most moderately sized data centers already use ECMP, It greatly increases network bandwidth-but it also raises a new challenge: the number of possible network paths Jumps by an order of magnitude, and so does troubleshooting complexity.
# The path explosion
Without ECMP, each hop has a single usable next-hop, so there may be only one path from source to destination. With ECMP, the number of paths is the **product of the per-hop next-hop counts**. For example, in a 6-hop topology where hops 1 through 4 each have 4 next-hops:
1x4 x 4x4 x 1 x 1 - 64 possible paths
#* ECMP path-selection strategy
Faced with multiple equal-cost paths, a router *could use round-robin - first packet to path 1, second to path 2, and so on.
*  *The problem with round-robin:** when one node on an ECMP path (say "r8°) fails, every path through it is affected. For TCP, the previous packet might have taken a path of healthy nodes and arrived fine, while the next packet - because of round-robin - selects a path that includes the faulty rg® and is lost.
*  *The fix - pin a flow to one path.** To bound the blast radius, we make the same flou always take the same link. Enabling **hash-based forwarding** (layer-3 load balancing) does this. The common hash is over the packet's **5-tuple**: source IP, destination IP, source port, destination port, and protocol - which uniquely identifies a TCP flow.
With hash forwarding, a single faulty node affects **only the flows hashed onto it**; flows not assigned to that node are unaffected - a risk-**isolation** effect.
## Nhy ICMP (ping) always succeeds
ECMP®s hash is usually over the 5-tuple, but an ICMP packet has **no ports** - its
5-tuple has source IP, destination IP, and protocol, with the port fields empty.
For the same client and server, source IP, destination IP, and protocol never change, and the ports are absent, so **the hash value never changes**. That means ICHP always selects the **same path**. If that path happens to be healthy, then no matter how many times you ping, it will always succeed - masking the failure on the other paths.
## Why TCP sometimes fails, sometimes works
Each new TCP connection generally uses a **different source port** from the previous one, This is the crucial difference from ICMP: different TCP connections have different 5-tuples, therefore different hash values, therefore possibly
**different paths**,
If one path is broken, a TCP connection that happens to hash onto that path fails, while connections that hash onto healthy paths succeed - producing the intermittent "sometimes it connects, sometimes it doesn't" symptom.
### Diagnostic consequence
- **ICMp** travels a **fixed** path (constant 5-tuple) → a consistent, and possibly misleading, result.
**Different TCP/UDP connections** travel **different** paths (the source port varies the 5-tuple) → intermittent, per-connection fallures.
So "ping is fine, TCP is flaky" 1s not a contradiction - it is the signature of a faulty path in an EMP fabric that IMP never exercises.
## Reproducing it deliberately
Because the source port drives the path selection, you can force a connection onto specific paths by fixing the local port - useful for isolating which path is broken:
•** bash
curl--help
#
--local-port <num/range›
Force use of RANGE for local port numbers

Sweeping "--local-port across a range makes "curl' (or any elient that supports binding the source port) take different ECMP paths, so you can find the local ports) - hence the paths) - that fail while others succeed.
## ECMP for multi-active load balancing
Beyond backbone bandwidth, ECMP has an important second use: **multi-active load balancing (LB)**.
##The Limitation of ARP-based sharing
A traditional Buses a one-primary/one-standby architecture. Large sites prefer
**multi-active** - all LB nodes serve traffic - which requires the same VIP to be served by multiple LB nodes at once.
Normally an IP is "clatmed" via ARP broadcast: machines and switches in the broadcast domain learn the IP&MAC mapping, and the switch forwards packets for that IP to that machine. But if multiple LB nodes configure the *same** IP and all race to send ARP, only the #*last** node to send ARP actually wins the IP: the others cannot serve it. ARP alone cannot do multi-active.
**# The ECMP-based solution
With ECHP, the LB nodes run a routing protocol -**GP or OSPF** - instead of
ARP, and peer (e-g- BGP) with the upstream gateway. The gateway then treats the multiple LB nodes not as end devices but as fellow **next-hop routers**. Combined with ECHP, traffic is distributed relatively evenly across all the LB nodes, giving true multi-active load balancing.
## Conclusion
-   ECMP®s main purpose is to use multiple links for greater transmission bandwidth.
-   To preserve availability, ECMP typically uses **hash-based (5-tuple) forwarding**, giving stateful per-flow forwarding across the links.
-   The multiple paths make network troubleshooting harder:
-   **IMP** packets take a **fixed** line (constant 5-tuple), so •ping can succeed consistently even when some paths are broken.
-   Different TCP/UDP** connections take **different** lines because the
5-tuple changes (notably the source port), so connections intermittently fail depending on which path they hash onto.
- A second major use of ECMP is **multi-active load balancing**, where LB nodes advertise a shared VIP via **BP/OSPF** (not ARP) so the gateway spreads traffic across them as equal-cost next hops.
**Practical tip:** fix or sweep the client's source port (curl --local-port) to steer connections onto specific EMP paths and isolate the faulty one.