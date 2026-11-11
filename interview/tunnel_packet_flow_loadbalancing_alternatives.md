<!-- interview/tunne1_packet_flow_loadbalancing_alternatives.md -->

# Tunnels Deep Dive: Packet Journey, Load-Balancing, and Alternatives

Follow-up to `gre_in_udp_over_dtls.md`. Three parts: how a packet actually reaches the
remote server; how/where to load-balance; and other tunnels with trade-offs.

## Part 1 - How a packet travels to the remote server

Tracing one inner packet end-to-end through `outer-IP | UDP | DTLS | GRE | inner`.

**Stage 1- Inner packet ingress,**An inner host/appenits a packet routed (via routing
table /VF / tun/ greinterface) toward the tunnel endpoint. The kernel or userspace
tunnel process receives the raw inner packet.

**Stage 2 - GRE encapsulation.** Prepend GRE: **protocol Type** = inner Ethertype, **Key**
= tenant/tunnel id, optional sequence number. -> `GRE | inner`.

**Stage 3 - DTS protection.** Hand the GRE packet to DTLS as application data. DTLS picks
the current epoch's AEAD key, assigns the record (epoch, seg), encrypts+authenticates ->
a DTS record with an auth tag. The CPU-heavy per-packet step.

**Stage 4 - UDP + outer IP (entropy step).** Wrap in UDP: **dest port 4755**, **Source
port = hash(inner 5-tupple)** (ECMP entropy). Prepend outer IP: src = this endpoint's
underlay IP, dst = remote endpoint's underlay IP. -> ful1 datagram
`outer-IP | UDP | DTLS | GRE | inner`.

**Stage 5 - Host egress.** outer dst looked up in the underly routing table -> next hop
-> egress interface. **MTU check** (fragment if oversized, or PMTUD lowers inner MTU - why
inner TU is pre-reduced). **Offloads:** UDP checksum, GSO/GRObatching, possibly crypto
offload. Frame goes out on the wire to the L2 next hop.

**Stage 6 - Underlay core (ECMP).** Each router/switch forwards on the **outer IP header*
and load-balances across equal-cost paths / LAG members by hashing the outer **5-tuple**
`(srcIP, dstIP, UDP, srcPort, dstPort)`. Because the source port varies per inner flow,
each inner flow hashes to a different path -> multipath spread. The core treats the
encrypted inner packet as opaque. NAT (if present) rewrites/ tracks the outer UDP flow -
works because it's UDP, not protocol 47.

**Stage 7 - Remote endpoint: demux + decapsulation.**

1. Outer IP delivers to the remote underlay IP; UDP dst 4755 -> tunnel socket.
2. **Session demux** to the right DLS session by peer address (or **DTS Connection ID**
   in 1.3 if the address changed via NAT).
3. **DTLS verify+decrypt:** check (epoch, seq) vs anti-replay window, verify AEAD tag,
   decrypt -> "GRE | inner'.
4. **GRE parse:****Key** - tenant/tunnel context/VRF; Protocol Type - inner protocol.
5. **Strip GRE**, inject the inner packet into the correct inner network/VRF, routed to
   the final inner destination.

Return traffic reverses this over the same 4-tuple (or CID).

**Mental model:** the core routes and ECMP-balances on the **outer UDP/IP headers** and
treats everything inside as opaque; all tunnel intelligence (security, tenant demux, inner
routing) lives only at the two endpoints.

## Part 2- Load-balancing: where and how

Two distinct LB questions - conflating them is the classic mistake.

### (a) Within the core (across paths) - L3/L4 ECMP

What GRE-in-UDP is designed for: **routers/switches at L3/L4** hash the outer 5-tuple
across ECMP/LAG. No LB box needed - inherent per hop. The endpoint's job is to **supply
entropy**; vary UDP source port per inner flow (stable within a flow, varied across
flows). **Recommended primary mechanism** for the tunnel itself - stateless, infinitely
scalable, works because the core only touches outer headers.

### (b) Across backend endpoints (distribute tunnels to a server pool)

Critical constraint: **DTS is stateful/connection-oriented** - all packets of a tunnel
session must reach the **same backend** holding that session's keys/epoch/replay-window.
Stateless per-packet spray breaks decryption. The LB must be **flow-affine (sticky per
tunnel).**

By layer:

- **L4LB with consistent hashing on the outer UDP 5-tuple (recommended).**
  Maglev/Katran-style, IPVS, or hardware LB pins each tunnel to a backend and minimizes
  reshuffling on scale changes. Needs no DTS/GRE visibility - Just sticky outer flow. Use
  **Direct Server Return (DSR)** so replies bypass the LB.
  - **Caveat-entropy vs, stickiness:** you vary the inner-derived UP source port for
    core ECMP; an L4 LB hashing on port keeps a tunnel together only if that tunnel's
    outer source port is stable. If one logical tunnel uses many source ports,
    port-hashing could split it across backends. Fix: keep the whole tunnel on one stable
    outer 4-tuple to the LB (add ECMP entropy after the LB / on another segment), or hash
    on sre+dst IP, or on **DTLS CID**.
- **DTLS Connection ID-aware LB (L4 + light DPI) - best for mobility.** DTLS 1.3 CID
  identifies the session independent of 4-tuple; a CID-aware L8 routes roaming clients to
  the same backend.
- **L7 / app-layer LB - generally NOT appropriate.** Can't content-balance an encrypted
  DTLS tunnel; no per-request boundary; terminating+re-originating DTLS at the LB makes it
  the crypto bottleneck+ a trust boundary. Only If the LB is the DTLS terminator by
  design.
- **GP/anycast + ECMP for scale-out.** Advertise the endpoint VIP via anycast; routers
  ECMP-spread tunnels across endpoints; combine with consistent hashing to pin flows. How
  large UDP/QUIC front-ends scale.

**recommendation:** distribute tunnels at with consistent hashing on the outer low
(CID-aware if DTLS 1.3 + roaming)**, using DR; rely on **core L3/L4 ECMP** (fed by
source-port entropy) for path balancing. Keep the LB DTLS-unaware unless you must
terminate. **pivotal rule: per-tunnel affinity is mandatory because DTS is stateful.**

## Part 3 - Other tunnels, with trade-offs

| Tunnel                             | Layer / crypto                    | Advantages                                                                                                    | Disadvantages                                                                                        | Best for                                                              |
| ---------------------------------- | --------------------------------- | ------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------- |
| **GRE-in-UDP over DTLS** (yours)   | L3 encap / UDP + DTLS             | Generie multi-protocol (L2/L3), NAT traversal, ECMP via src port, per-datagram security, multi-tenant via Key | Stacked MTU overhead; per-packet DTLS CPU; DTLS state complicates LB; manage keepalives/liveness     | Multi-tenant, multi-protocol secure overlays needing traversal + ECMP |
| **IPsec (ESP) / NAT-T**            | L3, proto 50 (UDP 4500 for NAT-T) | Mature, standardized, widely HV-accelerated; strong security: transport/tunnel mode                           | NAT/ECMP-hostile without UP encap: complex (IKE); no native multi-protocol; SPIdemux less flexible | Site-to-site VPNs, standards-mandated environments                    |
| **GRE over IPsec**                 | GRE inside IPsec                  | GRE multi-protocol + multicast/routing-protocol support, with IPsec security                                  | More overhead; ECMP-hostile unless UDP-encapsulated; complex                                         | Enterprise VPNs running routing protocols over IPsec                  |
| **WireGuardes                      | L3 / UDP, Noise cypto            | Simple, small, fast, great crypto defaults, endpoint roaming, UDP traversal/ECMP                              | Opinionated (no cipher agility), L3-only (no L2/multi-protocol), no built-in multi-tenant demux      | Point-to-point/mesh VPNs, performance-sensitive, modern               |
| **VXLAN** (UDP)                    | L2-in-UDP, VNI = tenant           | L2 overlay for DC multi-tenancy, 24-bit VNI space, NAT/ECMP-friendly (src-port entropy)                       | No built-in crypto (add IPsec/MACsec); L2 broadcast/flood concerns; ~50B overhead                    | DC network virtualization / cloud overlays                            |
| **Geneve** (UDP)                   | L2/L3-in-UDP, TLV options         | VXLAN successor: flexible metadata TVS, NAT/ECMP-friendly, protocol-agnostic                                 | No built-in crypto; less mature Hu offload than VXLAN; overhead                                      | Modern SDN/NFV needing rich metadata (NSX, OVN)                       |
| **MPLS / MPLS-in-UDP**             | L2.5 label switching             | Fast label forwarding, traffic engineering, QoS; UDP variant gets ECMP/traversal                              | Needs MPLS control plane; no built-in crypto; complex                                                | Carrier/backbone traffic engineering                                  |
| **QUIC / MASQUE (CONNECT-UDP/IP)** | L3/UDP proxied in QUIC (TLS 1.3)  | Integrated modern crypto, streams-datagrams, connection migration, HTTP/3 infra reuse, strong traversal       | QUIC CPU cost; never; datagram-vs-stream semantics to design; maturing ecosystem                     | VPN/proxy over HTTP/3 infra, modern edge/CDN                          |

**Choosing (reasoning):**

- **L2 or multi-protocol + multi-tenant?** GRE (yours), VLAN, Geneve - GRE if
  L3-generic/routing-protocol friendly; VLAN/Geneve if L2 DC overlay.
- **Crypto?** Yours (DTS), IPsec, WireGuard, QUIC - WireGuard for simple/fast p2p; IPsec
  for standards/interop; DTLS/GRE-in-UDP for generic-multi-tenant + security.
- **Traversal+ ECMP?** Anything UDP-based - the UDPwrapping is the common thread and why
  your design chose UDP.
- **Mobility/roaming?** WireGuard roaming, DTS 1.3 CID, or QUIC connection migration.

## Staff / Principal-level framing

- **The outer header is the only thing the network acts on; everything else is opaque.**
  Explains both the packet journey (core forwards/ECMPs on outer UDP/IP) and the LB design
  (key on outer flow / CID; must be sticky because DTS is stateful).
- **Two LB planes, don't conflate:** (1) *path* LB in the core - free via ECHP if you
  supply source-port entropy; (2) *endpoint/backend* LB = L4 consistent-hashing with
  per-tunnel affinity (DTS state forbids per-packet spray). The **entropy (spread) vs.
  affinity (stickiness)** tension is the subtle point.
- **MTU and per-packet crypto CPU are perennial constraints** across all tunnels - the
  choice mostly changes who pays and where.
- **UDP-encapsulation is the universal modern answer** to NAT traversal + ECMP; every
  contemporary tunnel converges on it (raw GRE and raw ESP both got UDP-wrapped variants).

## Interview Q&A

**Q: Walk through what the network core actually acts on as a GRE-in-UDP-over-DTLS packet
crosses it,** A: Only the outer IP/UDP headers. Each router forwards on the outer IP
destination and ECMP-balances across equal-cost paths by hashing the outer 5-tuple
`(SrCIP, dstIP, proto, srport, dstPort)`; the DTLS-encrypted GRE+inner packet is opaque
payload it never inspects, A11 tunnel intelligence - security, tenant demux via GRE Key,
inner routing - lives only at the two endpoints. *Follow-up:* Why can NAT traverse this
path when it couldn't for raw GRE? Because it's a normal UP flow with ports, not protocol
47; the NAT rewrites/tracks the outer UDP 4-tuple, *Red flag:* Thinking core routers read
the GRE Key or inner addresses.

**Q: There are two distinct load-balancing questions here - what are they, and why is
conflating them the classic mistake?** A: (a) *Path* load-balancing within the core: L3/L4
ECMP across equal-cost paths/LAG, which is stateless and free per hop as long as the
endpoint supplies entropy, (b) *Backend* load-balancing: distributing whole tunnel
sessions across a server pool. They're different because (a) only needs outer-header
hashing while (b) is constrained by DTS being stateful - conflating them leads people to
spray packets across backends and break decryption. "Follow-up:* Which plane does
source-port entropy serve? The path plane (a) - it feeds core ECMP. *Red flag:* Treating
ECMP path spread and backend distribution as the same problem.

**Q: Why must the backend load balancer be flow-affine (sticky per tunnel), and how do you
build it?** A: DTS is stateful/connection-oriented - every packet of a session must reach
the same backend holding that session's keys, epoch, and replay window; stateless
per-packet spray lands packets on backends that can't decrypt them. Build it as an L4 LB
with consistent hashing on the outer UP 5-tuple (Maglev/Katran/IVS), using Direct Server
Return so replies bypass the LB, and make it CID-aware for roaming clients. *Follow-up:*
Why avoid an L7 LB here? You can't content-balance an encrypted DTS tunnel and there's no
per-request boundary; terminating DTLS at the LB makes it the crypto bottleneck and a
trust boundary. *Red flag:* Proposing stateless per-packet round-robin across backends.

**Q: Explain the entropy-vs-stickiness tension in this design.** A: You deliberately vary
the UDP source port per inner flow to feed core ECMP (entropy = spread), but an L4 backend
LB that hashes on the source port will then split one logical tunnel across multiple
backends - and DTLS needs it pinned (affinity • stickiness). Resolve by keeping the whole
tunnel on one stable outer 4-tuple to the LB (add ECMP entropy on a later segment),
hashing on srcedst IP only, or hashing on the DTS Connection ID. *Follow-up:* Which
option is best for mobile/roaming clients? CID-based hashing - the DTS 1.3 Connection ID
identifies the session independent of the 4-tuple. *Red flag:* Not noticing that the very
entropy added for EMP can break backend stickiness.

**Q: Debugging scenario - after you scale the backend pool out, a chunk of existing
tunnels break. Why, and what would have prevented it?** A: The LB is rehashing flows to
new backends on a membership change, so established DTS sessions land on a backend
without their keys/epoch/replay state and fail to decrypt. Consistent hashing
(Maglev-style) minimizes reshuffling on scale changes so most existing flows stay pinned;
combining anycast/GP scale-out with consistent hashing preserves affinity while adding
capacity. *Follow-up:* How does DR interact with this? DR only changes the return path
(replies bypass the LB); the inbound stickiness st111 depends on consistent hashing. *Red
flag:* Using plain modulo hashing and being surprised that scaling reshuffles everything.

**Q: Design trade-off - when would you pick WireGuardor IPsec over
GRE-in-UDP-over-DTLS?** A: WireGuard when you want a simple, fast, modern
point-to-point/mesh VPN with great crypto defaults and roaming, and you don't need L2 or
multi-protocol carriage or built-in multi-tenant demux. IPsec when standards/interop or
mandated environments require it and hardware acceleration matters, accepting it's
NAT/ECMP-hostile without UDP encapsulation. GRE-in-UP-over-DTLS fits specifically when
you need GRE's generic multi-protocol/multi-tenant semantics (Key-based demux) plus
traversal, ECMP, and per-datagram security together. *Follow-up:* What's the common thread
across all the modern alternatives? UP encapsulation - it's the universal answer to NAT
traversal plus ECMP, which is why raw GRE and raw ESP both got UDP-wrapped variants. *Red
flag:* Picking a tunnel by brand familiarity rather than the L2/L, crypto, and
multi-tenant requirements.

**Q: Why is UDP source port called an "entropy field," and what breaks if you compute it
wrong?** A: It's not a real service port - the destination is a fixed well-known port
(4755 for the DTS variant); the source port exists purely to inject per-flow variation
into the outer 5-tuple so core ECMP spreads flows. It must be stable within a flow but
varied across flows. A constant source port gives no load-balancing (all flows hash to one
path); a per-packet-random source port causes intra-flow reordering. *Follow-up:* What
computes it? A hash of the inner 5-tuple, taken once per flow. *Red flag:* Randomizing the
source port per packet "for better spreading."

## Synthesis

**Packet journey:** prepend GRE (Key + protocol type), DTS encrypts into a record with
`(epoch, seq)`, wrap in UDP (dst 4755, **src port = hash of inner flow**) + outer IP; the
underlay forwards on the **outer 5-tuple**, EMP-spreading flows via that source-port
entropy while treating the encrypted inner packet as opaque; the remote endpoint demuxes
to the DTS session (peer addr/**CID**), verifies+decrypts, reads GRE **Key** for
tenant/VRF, strips GRE, and injects the inner packet to its destination.
**Load-balancing** has two planes: *within the core*, L3/L4 **ECM** on the outer 5-tuple
(stateless, fed by source-port entropy) is the recommended path mechanism; *across
backends*, use an **L4 LB with consistent hashing + per-tunnel affinity** (DTLS is
stateful same backend for a whole tunnel), ideally **CID-aware** for roaming and using
**DS**- not L7, not DLS-terminating unless intended. **Alternatives** trade along
L2-vs-L3/multi-protocol, built-in-crypto, and simplicity axes - IPsec (mature,
NAT/ECMP-hostile without UD), WireGuard (simple/fast/roaming, L3-only), VLAN/Geneve (L2
DC overlays, no built-in crypto), MPLS-over-UDP (carrler TE), QUIC/MASQUE (modern
integrated crypto + migration) - all converging on UP encapsulation for the same
traversal/ECMP reasons.

## References

- RFC 8086 - GRE-in-UDP
- RFC 9147 - DTLS 1,3
- RFC 2992 - Analysis of an ECMP algorithm
- RFC 6438- IPv6 flow label for ECMP
- RFC 7690-ICMP6 Packet Too Big delivery through ECMP/Load balancers (PMTUD breakage)
- RFC 4303 / RFC 7296 - ESP / IKEV2
- RF 7348 - VLAN
- RFC 8926 - Geneve
- RFC 3031 - MPLS architecture
- RFC 9298 - Proxying UDP in HTTP (MASQUE CONNECT-UDP)
- RFC 9484- Proxying IP in HTTP (CONNECT-IP)
- RFC 9221 - QUIC unreliable datagrams
- Eisenbud et al., "Maglev", NSDI 2016; Facebook Katran (GitHub)
