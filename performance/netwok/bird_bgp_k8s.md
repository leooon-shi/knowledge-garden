k1-- performance/network/bird_bgp_k85.md -->
# BIRD and BGP Pod Networking in Kubernetes
What BIRD is, why Calico runs it on every node, how pod routes are distributed with GP, how to operate and troubleshoot it, and where it does (and does not) fit into AI / agent architectures. Packet-level stack details are in
•linux_network_stack.md*; NAT/conntrack in "NAT.md".
## 1. What BIRD is
**BIRD** ("BIRD Internet Routing Daemon", maintained by CZ.NIC) is a user-space routing daemon. It speaks routing protocols (GP, OSPF, RIP, Babel, BFD, static) with peers, runs routes through **filters**, and installs the winners into the Linux kernel routing table (FIB). It does **not** forward packets itself - the kernel does. BIRD only decides *which routes* the kernel should have.
Version
Notes l
ee..s.ose.
1.6.x
Separate "bird" (IPv4) and •bird6" (IPv6) daemons. Calico's fork is based on this line. | One daemon for IPv4 + IPv6 via *channels*; new config syntax. |
| 3.x
I Multi-threaded rewrite for very large route tables (route servers, full Internet tables) I
Outside Kubernetes, BIRD is best known as the **route server at Internet Exchange Points**, where it handles hundreds of peers and full tables.
core config concepts:
-   **router id** - 32-bit identifier, normally a node IP.
-   **protocols** - instances such as "kernel (sync with the FIB), "device" (Learn interfaces, "direct, "static, "bgp, "bfd.
-   **tables** - BIRD's internal RIBs; protocols import into and export from them.
-  **filters**-a small language deciding what is accepted/ advertised (import filter ...; export filter ...;).
*••text
• Minimal BIRD 2.x example: advertise a static pod block to a ToR over eBGP router id 192.168.1.10;
I
protocol device { }
protocol kernel {
ipv4 { export all; }:
* push selected routes into the Linux FIB
}
protocol static {
ipva;
route 10.244.1.0/26 blackhole;
}
protocol bgp tor {
local 192.168.1.10 as 64513; neighbor 192.168.1.1 as 64512;
ipv4 {
import all;
}:
export where net - [ 10.244.0.0/16+ ]: only advertise pod CIDRS
## 2. Why Kubernetes needs a routing daemon
must make "10.244,x.y* routable. Two approaches:
-   **Overlay** (VXLAN/Geneve/IPIP): encapsulate pod traffic in node-IP packets.The underlay only needs to route node IPs.
-   **Routed/BGP**; tell the network (other nodes and optionally the ToR switches) "pod block lives behind node y" and forward pod packets natively.This avoids encapsulation overhead (CPU, MTU) and makes pod IPs visible to the fabric,
always means Calico,
## 3. Hon Calico uses BIRD
Inside each "calico-node" pod:
Component | Role |
**Felix**
I Programs the data plane: per-pod routes to "cal1* veths, iptables/nftables/eBPF policy, IPIP/VLAN
devices.
| **confd**
1 Watches the Calico datastore (Kubernetes API or etcd) and renders BIRD config ('/etc/calico/confd/config/
bird.cfg®),
then reloads BIRD.
I **BIRD/BIRD6**1 Runs BGP sessions to peers, advertises this node's pod blocks, installs routes learned from peers into the kernel. I
Route flow end to end:
1. Calico IPAM gives each node one or more **IP blocks** (default /26" for IPv4,
*/122° for IPv6) from an "IPPool, with "block affinity* to that node.
2. Felix adds a 132°
route per local pod to its call* Beth. Bird adds
 a 

**blackhole route** for the whole local block, so traffic to unused addresses in the block is dropped locally instead of looping.
1.    BIRD advertises the **block** (aggregated, not per pod) over BGP.
2.    Peers" BIRD learn it and install •block via ‹node-ip›* into their kernel FIB.
What it looks 1ike on § node (routes installed by BIRD are tagged "proto bird"):
***text
$ ip route | grep bird
blackhole 10.244.1.0/26 proto bird
10.244.2.0/26 via 192.168.1.11 dev ethe proto bird
10.244.3.0/26 via 192.168.1.12 dev tunle proto bird onlink
*   local block
*   no encapsulation
*   IPIP mode
#*# Encapsulation modes and whether BIRD runs
IPPool setting | Data path | BP/BIRD needed? |
*  ipipMode: Never,
*  ValanNode: Never | Native routing | Yes |
"IpipMode: Always /"CrossSubnet"| IPIP ("tun10*) across subnets | Yes - IPIP routes come from BIRD /
calico_backend: vxlan to disable BIRD |
valanMode: Always / "Crosssubnet" | VLAN ("vxlan.calico") | No - Felix programs VLAN routes itself; set
*CrossSubnet sends traffic natively within an L2 subnet and encapsulates only when crossing subnets. It is the common compromise for clouds that drop packets with unknown source IPs (remember to disable the cloud's source/dest check for native routing).
### BGP topologies
-   **Node-to-node full mesh** (default, "nodeToNodeMeshEnabled: true*). Every node peers with every other node: n(n-1)/2 sessions. Fine up to roughly 100 nodes, then session count, memory and convergence churn become a problem.
-   **Route reflectors (RR)**, Selected nodes (or dedicated RR pods/hosts) get e routeReflectorClusterID. Other nodes peer only with the RRs, so each node has 5 handful of sessions. Disable the mesh once the RRs are up.
-  **Peering with the fabric (ToR)**. Nodes peer over eBP with their top-of-rack switch (often "AS per rack"). Pod and Service IPs then become routable from outside the cluster. This is common on bare metal and in GPU clusters.
Why a full mesh or RR is needed at all: **1BGP does not re-advertise routes learned from one iP peer to another iP peer** loop prevention) With 2 single AS, either everyone peers with everyone, or route reflector relaxes that rule.
-**yam1
# Cluster-wide BGP settings
apiVersion: projectcalico.org/v3
kind: BGPConfiguration metadata:
name: default
spec:
nodeToNodeMeshEnabled: false asNumber: 64512 serviceClusterIPs:
-   cidr: 10.96.0.0/12 serviceLoadBalancerIPs:
-   cidr: 203.0.113.0/24
# Calico default AS
# advertise Service ClusterIPs to peers
# Every node peers with the route reflectors
apiVersion: projectcalico.org/v3
kind; BGPPeer
metadata:
name: peer-with-rr
spec
nodeSelector: all()
peerSelector: route-reflector » 'true'
Advertising Service IPs this way lets external routers reach Services with
**EC** across nodes, Combined with 'externalTrafficpolicy: Local", only nodes that host endpoints advertise the /32, which preserves the client source IP.
# 4, Operating and troubleshooting
*  *text
*   Peer summary (needs calicoct] on the node)
$ calicoctl node status
IPv4 BGP status
+00000000000009+100000
I PEER ADDRESS |
PEER TYPE
STATE
SINCE
| 192.168.1.11
192.168.1.12
INFO
node-to-node mesh | up
05:12:03 | Established
node-to-node
mesh|start
| 05:40:17 | Active
* Talk to BIRD directly inside calico-node for IPv6)
$ kubectl exec -n calico-system <calico-node-pod› -c calico-node •- \
birdcl-s /var/run/calico/bird.ctl show protocols
name
proto
table
state since
info
Mesh 192_168_1_11 BP master up
65:12:03
Established 

Mesh_192_168_1_12 GP master start 05:40:17 Active Socket: Connection refused ...birdcl-s/var/run/calico/bird.ctlshowprotocols all Mesh_192_168_1_11
$ ... birdcl -s /var/run/calico/bird.ctl show route
4
.. birdel-s /var/run/calico/bird.ctlshow route for 10.244.3.7
BP session states to know: Idle → Connect/Active → OpenSent + OpenConfirm +
Established. A session stuck in **Active** means BIRD keeps trying to open a TCP connection and failing (it is *not* "active and healthy").
The 'calico-node" readiness probe runs 'calico-node -bird-ready, which fails with messages such as BIRD is not ready: GP not established with 192.168.1.12'.
| Symptom | Likely cause | Check |
-  |--------
-  top sport = :179 or port = :179* |
Peer stuck "Active"/"Connect | TCP 179 blocked (security group, host f1rewal1), peer down | nc-ZV <peer> 179*, "ss I Peering with the wrong IP | IP autodetection picked docker/a secondary NIC | IP_AUTODETECTION METHOD" (e.g. interface-ethe, can-reach=<ip>"), "calicoctl get node -o yam!'|
I Established but pods unreachable | Routes learned but dropped: source/dest check, rp_filter, underlay ACL | "ip route get pod-ip, topump ni ethe host pod -ips |
I Cross-node pods unreachable in IPIP mode | IP protocol 4 blocked by the underlay | •tcpdump -ni ethe ip proto 4°
1 Routes flap after a node restart | No graceful restart /BFD; churn across a big mesh | show protocols all, BIRD 1ogs in "calico-node 1
I Session counts and cPU explode as the cluster grows| Full mesh beyond ~100 nodes | Move to route reflectors 1
Performance notes:
-   Native routing avoids the 20-byte (IPIP) or 50-byte (VLAN) encapsulation overhead and the encap/decap CPU cost. Pod MTU can equal the node MTU.
-   Block aggregation keeps the FIB small (one route per node block, not per pod). Watch for block fragmentation when nodes borrow addresses from blocks owned by other nodes.
-   GP convergence (session hold time - BIRD defaults to 240s hold / 80s keepalive unless tuned or backed by **BFD**) bounds how quickly traffic moves away from a dead node.
#* 5. BIRD and AI / agent architectures - a clarification
**BIRD the routing daemon is not a component of Al-agent architectures.** It has no role in LLM orchestration, tool calling, memory, or planning. There are two reasons the association comes up:
1.    **Name collision.** •*BIRD-SQL** ("BIg Bench for LaRge-scale DatabaseGrounded Text-to-SQL Evaluation") is a well-known *benchmark* for evaluatingLLM text-to-SQL systems and data agents. If you saw "BIRD" in an AI-agent paper or leaderboard, it is almost certainly this benchmark, which is unrelated to routing.
2.    **Infrastructure under AI workloads.** BIRD/BGP does legitimately appear
*beLow* AI systems:
-   GPU training and inference clusters on bare metal often use Calico (BIRD) or GP-to-the-host designs, peering nodes with the ToR so pod/Service IPs are natively routable and ECMP spreads traffic.
-   Inference endpoints can be exposed as **anycast** or LoadBalancer IPs that are advertised over BP from the nodes that serve them.
-   "Agentic NetOps": an LLM agent may be given tools that run read-only commands such as "birdc show protocols" or •calicoctl node status to diagnose peering problems. Here BIRD is a system the agent operates*, not part of the agent itself. Keep such tools read-only, or human-approved for"configure / disable, because a bad export filter can withdraw or leak routes cluster-wide.
## Pitfalls
-   Leaving the full mesh on in a large cluster.
-   Reading BP state "Active as healthy.
-   Wrong "IP_AUTODETECTION_METHOD® on multi-NIC nodes, which peers over the wrong network,
-  Native routing on a cloud VP without disabling source/destination checks.
-   Exporting too much to the fabric (e.g, without a filter) and leaking routes.
-   Assuming BIRD is running in VLAN mode, where it is disabled by design.
#* Likely interview questions
#*What is
BIRD, and what does it do vs, the kernel?**
BIRD is a user-space routing daemon. It exchanges routes with peers (BGP, OSPF, etc.), applies import/export filters, picks best paths, and syncs selected routes into the kernel FIB through its "kernel' protocol (netlink), The kernel does all packet forwarding. If BIRD dies, forwarding continues on the installed routes until they are withdrawn or the peers' hold timers expire. So a control-plane outage does not immediately become a data-plane outage, but the routes do go stale.
**why does Calico use BP at all, when Flannel Just uses VLAN?** To make pod IPs natively routable with no encapsulation. That means Lower per-packet CPU cost, no MTU reduction, easier packet captures, and the option to expose pod/Service IPs to the physical network by peering with ToR switches.
VLAN needs nothing from the underlay but adds overhead and hides pod IPs. Calico supports both: BGP (optionally with IPIP) or VXLAN without BIRD.
**Walk through how a pod on node A reaches a pod on node B in Calico BGP mode.**


IPAM gives node Ba /26 block. BIRD on B advertises the block over BGP, and BIRD on A installs •10.244.2.0/26 via <B-ip> proto bird. The pod on A sends to its default gateway (169.254.1.1 via proxy ARP on the "cali" veth). The packet enters A's host namespace, where policy is enforced (iptables/eBPF). A routes it to B natively, or through "tun1e" in IPIP mode. B matches the /32 route to the destination pod®s cali veth, applies ingress policy, and delivers it.
**Full mesh vs. route reflectors - why and when do you switch?*s iBGP does not re-advertise routes learned from one iP peer to another, so in a single AS every node needs a session to every other node: n(n-1)/2 sessions, and every route change fans out to all of them. Beyond roughly 100 nodes, the session count, memory, and update churn become significant, Route reflectors are allowed to reflect 1BGP routes, so each node peers only with 2-3 RRs (keep at least two for redundancy. Switch by labeling RR nodes, setting routeReflectorClusterID, adding "BPeer" objects, and then setting
•nodeToNodeMeshEnabled: false.
**What are IP blocks and the blackhole route for?**
Calico allocates pod IPs in per-node blocks (default /26) so that it advertises one route per block instead of one per pod. That keeps BP updates and FIB size small. The local blackhole route for the block makes sure a packet to an unused address in the node's own block is dropped locally. Without it, the packet would follow the default route back to the fabric and could loop. More specific /32 routes for live pods win by longest-prefix match.
*  *IPIP VS. VXLAN vs, native in Calico, and which ones need BIRD?** Native and IPIP both rely on BIRD-distributed routes. IPIP just points the next hop through "tunle". VLAN routes are programmed by Felix from the datastore, so BIRD is not needed (calico_backend: xlan). IPIP costs 20 bytes and needs IP protocol 4 allowed. VLAN costs 50 bytes, uses UP 4789, and passes through almost any network. "CrossSubnet encapsulates only between subnets.
*  *A "calico-node' pod is not Ready with "BGP not established". How do you debug?**
1.    Run "calicoctl node status and •birdcl ... show protocols to see which peer is failing and in which state. "Active or Connect means TCP isn't coming up.
2.    Check reachability on TCP 179 in both directions: security groups, host firewall, "nc -zv.
3.    Confirm each node peers from the right IP (IP_AUTODETECTION_METHOD, calicoctl get node -o yaml).
4.    Check AS numbers and peer config (BPPeer", "BPConfiguration) and read the BIRD 1og Lines in the calico-node" container.
5.    Once the session is Established, verify that routes were actually learned and installed: " show route, ip route | grep bird".
**How do you make Kubernetes Services reachable from outside the cluster via BGP?**
Set "serviceclusterIPs,
serviceExternalIps, and/or "serviceLoadBalancerIPs in
*  BPConfiguration and peer the nodes with the fabric. The routers learn the Service CIDRs/IPs from many nodes and load-balance with ECMP. With externalTrafficPolicy: Local®, Calico advertises the Service /32 only from nodes that run endpoints, which avoids a second hop and preserves the client source IP.
*  *How fast does traffic fail over when a node dies, and how do you improve it?** Without help it takes as long as the BP hold timer (BIRD defaults to 240s hold / 80 s keepalive; RFC 4271 suggests 98 s) before peers withdraw the dead node's routes. Faster options: lower the hold/keepalive timers, use **BFD** for sub-second failure detection, and use graceful restart so a *planned* BIRD restart doesn't withdraw routes that are still forwarding. Kubernetes endpoint removal also matters for Service traffic,
BGP-to-the-host designs.
**Is BIRD part of Al-agent architectures?**
No. It is a routing daemon, "BIRD" in AI contexts usually means the **BIRD-SQL text-to-SQL benchmark**, BIRD/BGP does show up in the "infrastructure* of AI platforms (GPU cluster pod routing, anycast inference endpoints). An agent can also operate* BIRD through read-only diagnostic tools, but BIRD 1s not an agent component,
I
## References
-  BIRD documentation: <https://bird.network.cz/?get_doc>
-   Calico BGP configuration:<https://docs.tigera.do/calico/latest/networking/confl.gur/ng/bgp>
-   Calico route reflectors and IP autodetection:<https://docs.tigera.io/cal1co/latest/networking/1pam/ip-autodetectlon>
-   Calico Service IP advertisement:<https://docs.tigera.1o/cal1co/latest/networking/conf1gur2ns/advertlse.serv1ce.dps>
-  BIRD-SQL benchmark: <https://bird-bench.github.lo/>
-   RFC 4271 (BGP-4), RFC 4456 (route reflection), RFC 5880 (BFD).






