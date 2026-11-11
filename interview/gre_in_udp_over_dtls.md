<!-- interview/gre_in_udp_over_dtls.md -->

# GRE-in-UDP over DTLS

## Concept - tunneling and why layers stack

A **tunnel** encapsulates one protocol's packets inside another's payload, so packets
cross an intermediate network as if through a virtual point-to-point link. The inner
packet is opaque payload to the outer network; endpoints encapsulate on entry, decapsulate
on exit. This carries protocols the underlay doesn't support, joins disjoint networks,
enforces policy, and adds security.

The stack is three encapsulations deep, each solving a problem the layer below can't:

```
[ IP | UDP | DTLS | GRE | <inner packet: IP / Ethernet / MPLS> ]
  \______ outer / underlay ______/  \_ tunnel _/  \_ payload _/
```

## Layer 1-GRE (Generic Routing Encapsulation)

**What.** GRE(RFC2784/2890) is a simple, general encapsulation: a smal1 header wrapping
an arbitrary inner protocol for carriage over IP. Header: flags + version, a **Protocol
Type** (EtherType) naming the inner payload (IPv4/IPv6/Ethernet/MPLS...), and optional
(RFC 2890) **Key** (32-bit tunnel/flow discriminator), **Sequence Number**, and Checksum.

**Why.** Protocol-agnostic ("generic") - carries L2 (Ethernet) or L3 (IP/MPLS) over IP.
Workhorse for overlays, VPNs, multi-tenant traffic. The **Key** field is the natural
tenant/tunnel discriminator when many logical tunnels share endpoints.

**Limitation motivating the next layer,** Classic GRE runs **directly over IP as protocol
47** - not over UDP/TCP:

- **No ports** -> NAT/firewalls handle it poorly (many drop protocol 47).
- **No port-based ECMP:** routers hash on the L4 5-tuple to spread flows over paths;
  portless GRE hashes all endpoint-to-endpoint traffic to a **single path**, losing
  multipath load-balancing.

## Layer 2 - GRE-in-UDP

**What.** GRE-in-UDP (RFC 8086) puts the GRE packet in a **UDp** datagram:
`IP | UDP | GRE | inner`. Well-known dest port **4754** (GRE-in-UDP), **4755**
(DTLS-secured variant).

**Why wrap GRE in UDP:**

- **NAT/firewall traversal:** UDP has ports; NATs/firewalls treat it as an ordinary UDP
  flow. Protocol-47 problems vanish.
- **ECMP via UP source port:** the encapsulator sets the **UDP source port = hash of the
  inner flow** (inner 5-tuple). Core routers hash the outer UDP 5-tuple and spread
  different inner flows across paths - regaining multipath load-balancing raw GRE can't
  get. The source port is an entropy* field, not a real port.
- **Ubiquity:** UDP goes everywhere - the lingua franca for tunnels (VLAN, Geneve,
  WireGuard, QUICchose UDP for the same reasons).

So GRE-in-UDP - GRE's generic multi-protocol encapsulation + UDP's traversal and
load-balancing friendliness.

## Layer 3 - over DTLS (security)

**What.** Plain GRE-in-UP is unencrypted/unauthenticated - anyone on-path can read or
inject inner packets. **DTLS** (RFC 8086 defines DTLS-secured GRE-In-UDP on port **4755**)
adds confidentiality, integrity, and endpoint authentication over the same UDP substrate.
Final stack: `IP | UDP | DTLS | GRE | inner`. DTLS runs between the tunnel endpoints;
GRE-encapsulated inner packets become DTLS-protected application records.

**Why D(not IPsec/LS):**DTLS secures **datagrams** over UP without adding
reliability/ordering - right for a tunnel carrying arbitrary inner traffic. You don't want
the tunnel to impose TCP-style head-of-line blocking on inner flows; DTLS preserves the
per-packet datagram model.

## End-to-end operation

**Encapsulation (ingress):**

1. Inner packet arrives (e.g., a tenant's inner IP packet),
2. Prepend the **GRE headent* (Protocol Type - inner EtherType; **Key** - tenant/tunnel
   id; optional Sequence Number).
3. Hand the GRE packet to **DL** as app data DTLencrypts/authenticates into a
   record.
4. Wrap in **UDP** (dest 4755; **source port = hash of inner flow** for ECHP) and **outer
   Ip**; send.

**Decapsulation (egress):**

1. Receive outer UDP; demux to the DTLS session (peer address / Connection ID).
2. DTLS verifies+ decrypts - recovers the GRE packet.
3. Parse GRE: **Key** selects tenant/tunnel context; Protocol Type gives inner protocol.
4. Strip GRE; Inject the Inner packet into the right inner network/VRF.

## Practical / operational concerns (where products live or die)

- **MTU and fragmentation - the dominant issue.** Overhead stacks: outer IP (20/40) + UDP
  (8) + DTLS (header + AEAD tag, ~13-29+) + GRE (4-16) -**~50-80+ bytes** off the ~1500
  path MTU, so inner MTU must drop. Wrong -> fragmentation or black-holing (DF set +
  broken PMTUD in middleboxes). Mitigate: lower inner MTU, **TCP MSS clamping** for inner
  TCP, careful PTU handling. Compounds the DTLS-MTU concern - tunnel overhead on top of
  DLS overhead.
- **EMP entropy correctness.** UDP source-port-as-hash helps only if computed from the
  inner flow, **stable per flow** (packets of a flow stay on one path -> no reordering)
  yet **varied across flows**. Bugs: constant source port -> no load-balancing;
  per-packet-random -> intra-flow reordering.
- **DTLS overhead & rekey.** Per-record AEAD tag + header per inner packet; small inner
  packets pay high relative overhead. Rekey/epoch transitions must not drop inner traffic
  (epoch trap).
- **Tunnel demux at scale.** Many tenants/tunnels: GRE **Key** for logical demux, DTLS
- **Connection ID** (1.3) for session demux so NAT rebinding doesn't break sessions. Map
  (OTLS session, GRE Key) -> inner context.
- **Performance / offload.** Heavy per-packet work (encrypt + encapsulate). Use NIC
  offloads (UDP G50/GRO, crypto offload), kernel paths (Linux `ip_gre` / FOU
  "Foo-over-UDP" / `fou`), or DPDK/XDP for high rates. Userspace per-packet DTLS is
  CPU-heavy - batch and offload.
- **Keepalives / liveness.** UDP+DTLS has no inherent liveness; NAT bindings time out.
  Tunnel keepalives hold NAT/firewall pinholes open and detect dead endpoints.
- **Fragment/reorder tolerance.** DTLS anti-replay windows and any GRE sequence checking
  must tolerate underlay reordering, or valid inner packets drop.

## Staff / Principal-level framing

- **Each layer earns its place:** GRE - generic multi-protoco1/mult1-tenant encapsulation
  (+ Key); UDP NAT traversal + ECMP (ports/entropy): DTLS - per-datagram security
  without imposing reliability. Remove any layer and lose a specific capability. Being
  able to answer "why not plain IPsec / GRE-over-IPsec?" (UDP glves traversal + ECHP that
  protocol-50/51 IPsec and protocol-47 GRE lack; DTLS matches the datagram model - same
  reason IPsecNAT-T wraps ESP in UDP) is the senior insight.
- **The design is dominated by MTU accounting and per-packet CPU** - not encapsulation
  correctness. Engineer around MSS clamping, offloads, batching, entropy tuning.
- **Know the alternatives/tradeoffs:** VLAN/Geneve-over-UDP (L2 overlay, no built-in
  crypto), WireGuard (UDP+ modern crypto, simpler/opinionated), IPsec/ESP (mature but
  NAT/EMP-hostile without UDP encap), QUIC-based tunnels (MASQUE). GRE-in-UDP-over-DTLS
  fits when you need GRE's *generic multi-protocol/multi-tenant* semantics plus traversal,
  ECMP, and datagram security together.

## Interview Q&A

**Q: Why is the stack three encapsulations deep - what does each layer earn?** A: Each
layer solves a problem the one below can't. GRE provides generic, protocol-agnostic
encapsulation of arbitrary inner traffic (L2/L3) with a Key for tenant/tunnel demux. UDP
adds NAT/firewall traversal (ports) and ECMP load-balancing (source-port entropy) that
protoco1-47 GRE lacks. DTLS adds per-datagram confidentiality, integrity, and
authentication without imposing TCP-style reliability/ordering on inner flows. Remove any
layer and you lose a specific capability. *Follow-up:* What are the well-known UDP ports?
4754 for plain GRE-in-UDP, 4755 for the DTLS-secured variant (RFC 8086). *Red flag:*
Treating the layers as redundant wrapping rather than each solving a distinct problem.

**Q: Why GRE-in-UDP-over-DTLS instead of plain IPsec or GRE-over-IPsec?** A: UDP
encapsulation gives NAT traversal and ECMP that protocol-50/51 IPsec and protocol-47 GRE
can't get natively, and DTLS matches the datagram model - securing per-packet without
adding reliability. It's the same reasoning IPsec itself uses when it wraps ESP in UDP for
NAT-T. You choose this stack when you need GRE's generic multi-protocol and multi-tenant
(Key) semantics together with traversal, ECMP, and datagram security. *Follow-up:* What
would push you toward IPsec instead? Standards/interop mandates, mature hardware
acceleration, or site-to-site VPN environments where ECMP/NAT isn't the constraint. *Red
flag:* "IPsec is always more secure so use it" - ignores traversal/ECMP and the
datagram-vs-stream fit.

**0: What role does the GRE Key field play, and how does it differ from the DTLS
Connection ID?** A: The GRE Key is a 32-bit tunnel/flow discriminator used for *logical*
demux- selecting the tenant/tunnel context/VRF for the inner packet after decryption. The
DTLS Connection ID (1.3) handles *session* demux at the crypto layer, identifying the DTLS
session independent of the IP/port 4-tuple so NAT rebinding doesn't break it. At scale you
map (OTLS session, GRE Key) → inner context. *Follow-up:* Which one survives a client's
NAT rebinding? The DTLS Connection ID - It decouples the session from the 4-tuple. *Red
flag:* Conflating tenant demux (Key) with crypto-session demux (CID).

**Q: How much MUoverhead does this stack cost, and why 1s MTU the dominant operational
issue?** A: Overhead stacks: outer IP (20/40) + UDP (8) + DTLS (header + AEAD tag,
~13-29+) + GRE (4-16) = 50-80+ bytes off the ~1500 path MTU, so the Inner MTU must drop.
Get it wrong and you get fragmentation or black-holing (DF set plus PMTUD broken by a
middlebox). It dominates because it compounds the DTLS-MTU concern - tunnel overhead on
top of DTLS overhead - and black holes are hard to diagnose, *Follow-up:* Standard
mitigations? Lower the inner MTU, TCP MSS clamping for inner TCP, and careful PMTU
handling, *Red flag;* Ignoring overhead accounting and assuming a 1500-byte inner MTU
still fits.

**Q: What makes ECMP entropy "correct," and what are the two classic bugs?** Ai The UDP
source port must be computed from the inner flow, stable per flow (so a flow's packets
stay on one path and don't reorder) yet varded across flows (so different flows spread
across paths). The two bugs: a constant source port gives no load-balancing (everything
hashes to one path), and a per-packet-random source port causes intra-flow reordering by
scattering one flow's packets across paths, *Follow-up:* Why does reordering within a flow
matter for this tunnel? It can trip DS anti-replay windows and hurt inner TCP
performance. *Red flag:* "Just randomize the source port" without the stable-per-flow
requirement.

**Q: Debugging scenario - inner traffic drops clustered right after a DTLS rekey. What's
the likely cause?** A: An epoch transition: records around the rekey belong to a new
epoch, and if the receiver drops records with an unexpected epoch (or discards late
records from the previous epoch) valid inner packets are lost. The rekey/epoch transition
must not drop inner traffic - keep the previous epoch's keys briefly and let the DTLS
library manage epochs; prefer DTLS 1.3 over 1.2 renegotiation. *Follow-up:* What other
reorder-sensitive mechanism can drop valid inner packets? The DTLS anti-replay window if
it's too small for the underlay's reordering. *Red flag:* Blaming the application for loss
that's actually an epoch-handling bug.

**Q: Where does this design "live or die" in production beyond correctness?** A: In MTU
accounting and per-packet CPU, not encapsulation correctness. Per-packet DTLS
encrypt+encapsulate is heavy, especially in userspace, and small inner packets pay high
relative AEAD-tag overhead. You engineer around it with NIC offloads (UDP GSO/GRO, crypto
offload), kernel fast paths (Linux `ip_gre`/FOU), or DPDK/DP and batching - plus
keepalives to hold NAT pinholes and detect dead endpoints. *Follow-up:* Why do keepalives
matter for a UDPDTLS tunnel? UDP+DLhas no inherent liveness; NAT bindings time out and
dead endpoints go undetected without them. *Red flag:* optimizing protocol correctness
while ignoring MTU, offload, and CPU.

## Synthesis

`IP | UDP | DTLS | GRE | inner` is a layered tunnel where each encapsulation adds a
distinct capability. **GRE** gives generic, protocol-agnostic encapsulation of arbitrary
inner traffic (L2/L3) with a **Key** for tenant/tunnel demux, but raw GRE (IP protocol 47)
traverses NAT poorly and can't be ECMP-load-balanced. **GRE-in-UDP** (RFC 8086, port 4754)
fixes both by wrapping GRE in UP: ports restore NAT/firewall traversal, and setting the
**UDP source port to a hash of the inner flow** restores multipath ECMP. **DTLS** (secured
variant, port 4755) adds confidentiality, integrity, and authentication per datagram -
securing the tunnel without imposing TCP-style reliability/ordering on inner flows.
Operationally the design is dominated by *TU accounting*(50-80+bytes stacked overhead
-> inner MTU reduction, MSS clamping, PMTUD care), **ECMP entropy correctness**
(stable-per-flow, varied-across-flows source port), **per-packet DTLS CPU**
(offload/batching), and **liveness/keepalives** to hold NAT pinholes - with GRE Key + DTLS
Connection ID handling multi-tenant demux at scale.

## References

- RFC 2784 - GRE
- RFC 2890 - GREkey and sequence number extensions
- RFC 8886-GRE-in-UDP encapsulation (UDP 4754; DTLS-secured variant UDP 4755)
- RFC 6347 / RFC 9147 - DTLS 1.2 / 1.3
- RFC 9146 - DTLS Connection ID
- RFC 8085 - UDP usage guidelines (applies to UDP tunnels)
- RFC 6935 / RFC 6936 - IPv6 UDP zero-checksum for tunnels
- RFC 4459 - MTU issues in tunneling
- RFC RFC IPv6 flow label for ECMP
