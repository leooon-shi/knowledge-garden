<!-- interview/tunnel_general.md -->

# Tunnels: What, When, and Concerns

See also: `ecmp_mtu_wireguard_ipsec.md` (ECMP hashing, MTU/PMTUD/MSS clamping math,
WireGuard vs IPsec).

(General treatment; companion to the product-specific `gre_in_udp_over_dtls.md` and
`tunnel_packet_flow_loadbalancing alternatives.md`.)

## What a tunnel is

A **tunnel** encapsulates packets of one protocol inside the payload of another, so inner
traffic traverses an intermediate network as if through a virtual point-to-point link.
**Tunnel endpoints** encapsulate on entry (wrap inner packet in an outer header) and
decapsulate on exit (strip it); to the network between, the inner packet is opaque
payload.

Mental model: a tunnel creates an **overlay** network on top of an **underlay**. The
underlay routes the outer packets and knows nothing about the inner traffic; the overlay
behaves as a direct link between endpoints despite many hops between them.

```
inner --encapsulate--> [ outer header | inner packet ] --underlay--> --decapsulate--> inner
 (A)                                                                                   (B)
```

## When to use a tunnel

1. **Carry an unsupported protocol / connect disjoint networks**- IPv6 over IPv4 (6in4),
   L2 over L3, link two private networks across the internet (site-to-site VPN). Makes
   separated/incompatible networks look adjacent.
2. **Add security/privacy over an untrusted path** - encrypt + authenticate across a
   hostile network (IPsec, WireGuard, OpenVPN). The tunnel is the trust boundary
3. **Overlays / network virtualization** - multi-tenant DC/cloud (VXLAN, Geneve),
   container/K8S overlays (Flannel, Calico): isolated virtual L2/L3 over shared physical
   infra.
4. **Traverse NAT/firewalls**- wrap in a protocol/port that gets through middleboxes
   (UDP/443, HTTPS). Why modern tunnels favor UDP (ports give traversal).
5. **Enforce policy / steer traffic** - force a path for inspection, filtering, Qos,
   traffic engineering (MPLS, policy tunnels).
6. **preserve addressing/mobility**- stable inner address while the outer path changes
   (Mobile IP, roaming).

**Don't tunnel** when networks are already connected, compatible, trusted, and reachable -
it's pure overhead and complexity.

## Concerns (costs and risks)

**1. MTU & fragmentation (perennial #1).** Each encapsulation adds header overhead (outer
IP + tunnel header + encryption - often 40-80+ bytes), shrinking usable inner MTU. Inner
packet + overhead> path MU-fragmentation (slow, loss-amplifying) or black-holing when
DF set + PMTUD broken by middleboxes. Mitigate: lower inner MTU, **TCP MSS clamping**,
careful PMUD. Bites nearly every deployment.

**2. Performance overhead.** Encap/decap is per-packet CPU; encryption adds more.
Throughput down, latency up vs. native. Mitigate with NIC offloads (GSO/GRO, crypto),
kernel fast paths, DPDK/XDP.

**3. Small-packet inefficiency.** Fixed per-packet header overhead is proportionally huge
for small inner packets (VoIP, telemetry) - headers can exceed payload.

**4. Security.**

- **Unencrypted** tunnels (GRE, VLAN, IP-in-IP) give *no* confidentiality - on-path
  actors read/inject inner traffic; layer encryption (IPsec/DTLS).
- Tunnels can **bypass security controls** - traffic hidden inside evades firewalls/IDS
  seeing only outer headers (DNS/ICMP tunneling are exfiltration/c2 techniques).
- **Endpoint trust / key management** - endpoints are high-value; compromise exposes all
  tunneled traffic,

**5. Visibility / observability loss,** Monitoring sees only outer headers; inner traffic
is opaque, Debugging, Qos classification, security inspection all harder - need
endpoint-side visibility,

**6. Operational complexity.** More moving parts: endpoint config, keepalives, failover,
key rotation, MTU tuning. A tunnel can fail independently of the underlay.

**7. Load-balancing / ECMP.** Tunnels can collapse flow entropy - identical outer headers
for all Anner flows (protocol-47 GRE, no ports) prevent path spreading. Fixed by UDP
encapsulation + source-port entropy.

**8. Connectionless/liveness gaps.** UDP tunnels have no bullt-in connection state; NAT
bindings time out, dead endpoints undetected without keepalives.

**9. Nesting (tunnel-in-tunnel),** Compounds overhead + MTU problems; misconfig can create
routing loops.

## Staff / Principal-level framing

- **A tunnel is an overlay abstraction with a real tax** - trades *capability*
  (connectivity, security, virtualization, traversal) for *overhead* (MTU loss, CPU,
  visibility loss, operational complexity). Use only when you need the capability; don't
  tunnel reflexively.
- **MTU almost always breaks first** and is hardest to diagnose (mysterious
  hangs/black-holes: large packets fail, small ones work). Account for overhead and set
  MTU/MSS deliberately from day one.
- **Separate encapsulation from encryption** - orthogonal: GRE/VLAN encapsulate without
  securing; IPsec/DTS/WireGuard add security. Many designs Layer them; be explicit about
  which problem each layer solves.
- **Visibility loss is double-edged** - the point (isolation/privacy) and a risk (bypasses
  inspection, enables covert channels). Security architecture must account for what
  tunnels hide.
- **Modern tunnels converge on UDP encapsulation** for NAT traversal + ECMP entropy;
  inability to load-balance or NAT-traverse is usually the reason to choose UDP-based.
- **Match layer/type to need:** L2 overlay (VLAN/Geneve) for DC virtualization; L3 secure
  (IPsec/WireGuard) for VPNs; generic/multi-protocol (GRE) for routing-protocol +
  multi-tenant carriage; QUIC/MASQUE for modern proxying.

## Interview Q&A

**Q: What is a tunnel, in one sentence, and what are its two defining operations?** A: A
tunnel encapsulates packets of one protocol inside the payload of another so inner traffic
crosses an intermediate network as if over a virtual point-to-point link. The two defining
operations are encapsulation at the entry endpoint (wrap the inner packet in an outer
header) and decapsulation at the exit endpoint (strip it); to the underlay in between, the
inner packet is opaque payload. *Follow-up:* What's the difference between the overlay and
the underlay? The overlay is the virtual link between endpoints; the underlay is the real
network that routes the outer packets and knows nothing about the inner traffic. *Red
flag:* Conflating a tunnel with a VPN or assuming a tunnel implies encryption.

**Q: Why do modern tunnels (WireGuard, VLAN, Geneve, GRE-in-UDP) almost all encapsulate
in UDP?** A: UDP gives two things raw encapsulations lack: NAT/f1rewal1 traversal (it has
ports, so middleboxes treat it as an ordinary flow) and ECMP load-balancing (routers hash
the outer L4 5-tuple, so varying the UDP source port per inner flow spreads flows across
equal-cost paths). Protocol-47 GRE and protocol-50 ESP get neither without a UDP wrapper.
*Follow-up:* How do you supply ECMP entropy correctly? Set UDP source port - hash of the
inner 5-tuple: stable within a flow (no reordering) but varied across flows (spread). *Red
flag:* Thinking the UD source port is a real service port rather than an entropy field.

**Q: Why is MTU "the thing that almost always breaks first" with tunnels, and how do you
diagnose a black hole?** A: Each encapsulation adds 40-80+ bytes of header/crypto
overhead, shrinking the usable inner MTU; when inner packet + overhead exceeds the path
MTU with DF set and PMUD broken by a middlebox that silently drops ICMP "Too Big," large
packets vanish while small ones succeed. Diagnose by probing with `ping -M do -s <size>`
to find the largest packet that crosses unfragmented, or `tracepath`/`tracepath6` to
locate where the MTU drops. *Follow-up:* What's the standard mitigation for inner TCP? TCP
MSS clamping on the tunnel interface, plus deliberately lowering the inner MTU. *Red
flag:* "Just enable fragmentation" - fragmentation amplifies loss and may be blocked, and
doesn't fix a PMTUD black hole.

**Q: Encapsulation and encryption are often conflated - why insist they're orthogonal?**
A: Encapsulation is about carriage (GRE, VLAN, IP-in-IP wrap packets but provide zero
confidentiality); encryption is about securing that carriage (IPsec, DTLS, WireGuard). An
unencrypted tunnel lets any on-path actor read or inject inner traffic, so you must be
explicit about which layer solves which problem and layer encryption when the path is
untrusted. *Follow-up:* Give an example that encapsulates without securing. GRE or XVLAN -
hence designs like GRE-over-IPsec or VLAN-over-IPsec/MACsec when security is needed. *Red
flag:* Assuming "it's tunneled, therefore it's secure."

**Q: Tunnels cause visibility loss - why is that both the point and a risk?** A: The point
is isolation/privacy: the underlay sees only outer headers, which is exactly what
multi-tenant overlays and VPNs want. The risk is that firewalls/IDS also see only outer
headers, so tunnels can bypass inspection and enable covert channels (DNS/ICMP tunneling
for exfiltration/C2). Security architecture must account for what tunnels hide and add
endpoint-side visibility. *Follow-up:* Where do you regain visibility? At the endpoints,
after decapsulation - monitoring, los classification, and inspection must live there. *Red
flag:* Treating opacity purely as a feature with no security downside.

**Q: Design trade-off - when should you NOT use a tunnel?** A: When the networks are
already connected, compatible, trusted, and mutually reachable. In that case a tunnel is
pure overhead (MU loss, per-packet CPU, operational complexity, visibility loss) with no
offsetting capability. Tunnel only when you actually need connectivity across
disjoint/incompatible networks, security over an untrusted path, virtualization,
traversal, policy steering, or address/mobility preservation. *Follow-up:* What's the
cheaper alternative when you only need to cross an address boundary? NAT/forwarding - it
rewrites and moves the packet without encapsulation overhead, at the cost of breaking
end-to-end addressing, "Red flag:* Reaching for a tunnel reflexively for any connectivity
problem.

**Q: Why does raw GRE collapse ECMP, and what fixes it?** A: Raw GRE runs directly over IP
as protocol 47 with no ports, so routers hashing the L4 5-tuple see identical outer
headers for every inner flow and pin all endpoint-to-endpoint traffic to a single path.
Wrapping GRE in UP (RFC 8086) and setting the UP source port from the inner flow
restores per-flow path spreading, *Follow-up:* Does this also help NAT traversal? Yes -
the same UP wrapping gives ports that NATs and firewalls handle normally, where protocol
47 is often dropped, *Red flag:* Claiming ECMP "just works" over any tunnel regardless of
encapsulation.

## Synthesis

**tunne1** encapsulates one protocol's packets inside another's to form a virtual
point-to-point link - an **overlay** over an **underlay** that routes the opaque outer
packets while endpoints encapsulate/decapsulate the inner traffic. **Use one** to carry an
unsupported protocol or connect disjoint/incompatible networks, add security over an
untrusted path, build multi-tenant overlays/virtualization, traverse NAT/firewalls,
enforce traffic policy, or preserve addressing under mobility - and *don't when networks
are already connected, compatible, and trusted. **Concerns** are led by
**MTU/fragmentation** (overhead shrinks inner MTU -> fragmentation/black-holing: mitigate
with MSS clamping + deliberate sizing), plus **per-packet CPU/encryption overhead**,
**small-packet inefficiency**, **security** (unencrypted tunnels expose traffic; tunnels
bypass inspection and enable covert exfiltration; endpoints are high-value), **visibility
loss**, **operational complexity**, **ECMP entropy collapse** (fixed by UDP + source-port
entropy), and **nesting** compounding all of it. Principal view: a tunnel buys
connectavity/security/virtualization/traversal at the cost of overnead and opacity - use
it only when you need that capability, keep encapsulation vs. encryption distinct, plan
MTU from day one, and prefer UP-based designs for traversal and load-balancing.

## References

- RFC 2784 / RFC 2890 - GRE and GRE key/sequence
- RFC 8086 - GRE-in-UDP
- RFC 2003 - IP-in-IP
- RFC 4381 / RFC 4303 / RFC 7296- IPsec architecture, ESP, IKEV2
- RFC 3948 - UDP encapsulation of ESP (NAT-T)
- RFC 7348 - VLAN
- RFC 8926 - Geneve
- RFC 7510 - MPLS-in-UDP
- RFC 4459 - MTU and fragmentation issues with in-the-network tunneling
- RFC 1191 / RFC 8201 - PMTUD for IPv4 / IPv6
- RFC 4821 / RFC 8899 - Packetization-layer PMTUD (TCP / datagram)
- RFC 6438 - IPv6 flow label for ECMP in tunnels
- WireGuard: whitepaper (Donenfeld, NDSS 2017) - no RFC
- See also `ecmp_mtu_wireguard_ipsec.md` for MTU math, MSS clamping and WireGuard vs
  IPsec.
