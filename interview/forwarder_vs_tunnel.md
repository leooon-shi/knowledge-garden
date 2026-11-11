<!-- interview/forwarder_vs_tunnel.md -->

# Packet Forwarder (NAT/router/VPN) vs. Tunnel

## Untangle four related concepts (often conflated)

- **Routing** = *deciding* the next hop/path (build/consult routing table). The
  control/decision function.
- **Forwarding** = *moving* a packet toward the chosen next hop - the data-plane action.
- **NAT** = *rewriting* addresses/ports in the header (+ tracking translation state) to
  cross address-domain boundaries (private\<->publie). Modifies the packet but keeps it 
  the *same* packet with changed headers.
- **Tunneling** = *encapsulating* the whole packet inside a new outer packet - wrapping,
  not rewriting.

Key distinction: **NAT/forwarding modify or move the original packet; tunneling wraps it
inside another.** That's the heart of the question.

## Core difference: rewrite/forward vs. encapsulate

**Forwarder (NAT/router, proxy-style relays):**

- Operates on the **existing packet** - inspects headers, possibly **rewrites** them (NAT
  changes src/dst IP:port), decrements TTL, recomputes checksums, sends the same packet
  onward.
- Structure unchanged - still `[IP | L4 | payload]`: only header *field values* change.
- Original IP header is **visible and modified** along the path.
- Example: home router NAT - `192.168.x.x:port` rewritten to public IP:port, forwarded;
  reply rewritten back. One packet, mutated headers, no wrapping.

**Tunnel:**

- Takes the **entire original packet as opaque payload** and **prepends a new outer
  header** - `[outer IP | encap | original packet]`.
- Original (inner) header **preserved intact and hidden**; underlay routes only on the
  outer header.
- Decapsulation at the far endpoint recovers the *exact* original packet.
- Example: site-to-site VPN - packet to private `10.0.2.5` (unroutable on the internet)
  wrapped in an outer packet to the remote gateway's public IP; internet routes the outer
  packet; gateway unwraps and delivers to `10.0.2.5`.

**One line:** *a forwarder/NAT moves or rewrites the same packet; a tunnel encapsulates
the packet in a new one and routes on the outer wrapper.*

## Where does a VPN sit? (the nuance)

"VPN" is ambiguous - spans both categories:

- **Tunnel-based VPNs (common):** IPsec, WireGuard, OpenVPN - **encapsulate** (and usually
  encrypt) inner packets inside outer packets to the peer. These are **tunnels**.
- **proxy/forwarder VPNs":** SOCKS/HTTP proxy, NAT relay - **don't encapsulate**; they
  terminate your connection and **forward/rewrite** on your behalf (new connection from
  proxy to destination). These are **forwarders**.

So a VPN *can* be either, depending on build - most IP-layer VPNs are tunnels; app-layer
proxies are forwarders. Reason about the *mechanism* (encapsulate vs. rewrite/forward),
not the label "VPN."

## Comparison

| Aspect                                       | Forwarder / NAT / router                         | Tunnel                                                       |
| -------------------------------------------- | ------------------------------------------------ | ------------------------------------------------------------ |
| Core action                                  | Move / **rewrite** existing packet               | **Encapsulate** in a new outer packet                        |
| Packet structure                             | Unchanged; only field values change              | Outer header added; original kept as inner payload           |
| Original header                              | visible, often modified (NAT)                    | Preserved, hidden inside                                     |
| Routing basis                                | On the (possibly rewritten) packet               | On the **outer** header only                                 |
| Preserves inner addressing?                  | No (NAT changes it)                              | Yes (inner addresses intact)                                 |
| Carry foreign protocols/ private addresses?  | No                                               | Yes (the point)                                              |
| State                                        | NAT keeps translation state                      | Endpoints keep tunnel state; underlay stateless w.r.t. inner |
| Examples                                     | Home router NAT, L3 router, SOCKS/HTTP proxy, LB | IPsec/WireGuard VPN, GRE, VXLAN, GRE-in-UDP                  |

## Why choose one

- **NAT/forwarding** when you just need packets to *reach* an external host across an
  address boundary without preserving inner addressing or carrying foreign protocols -
  lighter (no encap overhead, no MTU hit) but **mangles addressing** (destination sees the
  NAT's address) and can't carry private/unroutable inner addresses or non-native
  protocols.
- **Tunne1** when you must **preserve the inner packet end-to-end** - carry private
  addresses across the internet, join networks as if adjacent, run an unsupported
  protocol, or secure/Isolate traffic. Cost: encapsulation overhead (NTU, CPU) and the
  usual tunnel concerns.

## Staff / Principal-level framing

- **Clean model: rewrite vs. wrap.** NAT/forwarding *rewrttes and moves* a packet
  (destination sees a changed packet from a different source); a tunnel *wraps* it so the
  inner packet arrives byte-for-byte at the far endpoint. Addressing preservation,
  protocol-agnosticism, and overhead all follow from this one difference.
- **Addressing semantics are the practical dividing line.** NAT **breaks end-to-end
  addressing** (inner source hidden/translated - why NAT breaks address-embedding
  protocols like SIP/FTP, needing ALGS). A tunnel **preserves end-to-end addressing** -
  inner src/dst survive, enabling true network-to-network connectivity. Need the far side
  to see real inner addresses -> tunnel, not NAT.
- **They compose, and usually do.** A tunnel's *outer* header often traverses NAT (hence
  NAT-T, UDP encapsulation). "NAT vs. tunnel" isn't either/or; a tunnel's outer packet is
  routinely NATed and forwarded by ordinary routers. Knowing which layer each operates on
  is the senior insight.
- **"VPN" names a goal (private networking over a public path), not a mechanism** -
  realized by tunnels (most) or forwarders/proxies (some).
- **Security is orthogonal to both.** Forwarding/tunneling are about *delivery*; NAT and
  plain tunnels don't encrypt. VPN security comes from encryption layered on the tunnel
  (IPsec/DTS/WireGuard) - the encapsulate-vs-encrypt separation applies here too.

## Interview Q&A

**Q: In one line, what's the core difference between a forwarder/NAT and a tunnel?** A: A
forwarder/NAT moves or rewrites* the same packet (inspects headers, possibly changes
src/dst IP:port, decrements TTL, sends it onward), whereas a tunnel *wraps* the whole
original packet as opaque payload inside a new outer header and routes the underlay on
that outer header only. Rewrite/move versus encapsulate is the heart of the distinction.
*Follow-up:* Which preserves the original inner header? The tunnel - the inner header is
kept intact and hidden, and decapsulation recovers the exact original packet; NAT mutates
the header fields. *Red flag:* Saying both "change the packet" without distinguishing
field rewrite from encapsulation.

**Q: Four related terms get conflated - routing, forwarding, NAT, tunneling. Disentangle
them.** A: Routing is *deciding* the next hop/path (the control-plane decision).
Forwarding is moving* the packet toward the chosen next hop (the data-plane action). NAT
is *rewriting* address/port fields plus tracking translation state to cross an
address-domain boundary. Tunneling is *encapsulating* the whole packet inside a new outer
packet. The key split: NAT/forwarding modify or move the original packet; tunneling wraps
it. *Follow-up:* Does NAT change the packet's structure? No - it stays
`[IP | L4 | payload]`; only field values change. A tunnel adds a whole new outer header.
*Red flag:* Treating "routing" and "forwarding" as synonyms.

**Q: "Is a VPN a tunnel or a forwarder?" - how do you answer?** A: "VPN" names a goal
(private networking over a public path), not a mechanism, so it can be either.
Tunnel-based VPNs (IPsec, WireGuard, OpenVPN) encapsulate and usually encrypt inner
packets inside outer packets to the peer. Proxy/relay "VPNs" (SOCKS/HTTP proxy, NAT relay)
don't encapsulate - they terminate your connection and forward/rewrite on your behalf.
Reason about the mechanism, not the label. *Follow-up:* Which is more common for IP-layer
VPNS? Tunnels - most IP-layer VPNs encapsulate; app-layer proxies are forwarders. *Red
flag:* Insisting a VPN is "always a tunnel."

**Q: Why does NAT break protocols like SIP and FTP while a tunnel doesn't?** A: NAT breaks
end-to-end addressing - it hides/translates the inner source, so protocols that embed IP
addresses or ports inside their payload (SIP, FTP) advertise an address the far side can't
reach, requiring Application Layer Gateways (ALGS) to rewrite the payload too. A tunnel
preserves inner src/dst byte-for-byte, so address-embedding protocols survive and true
network-to-network connectivity works. *Follow-up:* So if the far side must see real inner
addresses, which do you choose? A tunnel, not NAT. *Red flag:* Claiming NAT is transparent
to all protocols.

**Q: Design trade-off - when is NAT/forwarding the better choice than a tunnel?** A: When
you only need packets to *reach* an external host across an address boundary, without
preserving inner addressing or carrying foreign protocols. Forwarding/NAT is lighter - no
encapsulation overhead, no MTU hit, no per-packet crypto - at the cost of mangling
addressing (the destination sees the NAT's address) and being unable to carry
private/unroutable inner addresses or non-native protocols. Use a tunnel only when
you must preserve the inner packet end-to-end. *Follow-up:* What's the main cost of choosing
the tunnel instead? Encapsulation overhead (MTU reduction, CPU) plus the usual tunnel
concerns like liveness and visibility loss. *Red flag:* Defaulting to a tunnel when a
simple NAT/forward would do.

**Q: The question is framed as "NAT vs. tunnel," but is it really either/or?** A: No -
they compose, and usually do. A tunnel's *outer* packet is itself an ordinary IP/UDP
packet that routers forward and NATs rewrite; that's exactly why NAT traversal matters for
tunnels and why ESP gets UDP-encapsulated (NAT-T). The senior insight is knowing which
layer each operates on: the tunnel operates on the inner packet, the underlay
NATS/forwards the outer packet, *Follow-up:* Why does ESP need NAT-T while a UDP-based
tunnel doesn't? Bare ESP is protocol 50 with no ports, so NATs can't track it; wrapping it
in UDP (RFC 3948) gives ports, AH can't cross NAT at all because it authenticates the IP
header NAT rewrites, *Red flag:* Treating the outer tunnel packet as somehow exempt from
ordinary NAT/forwarding.

**Q: Is security a property of forwarding or tunneling?** A: Neither - securtty is
orthogonal to both. Forwarding and tunneling are about delivery®; plain NAT and plain
tunnels (GRE, VLAN, IP-in-IP) provide no confidentiality, VPN security comes from
encryption Layered on* the tunnel (IPsec, DTLS, WireGuard) - the same
encapsulate-vs-encrypt separation that applies to tunnels generally, *Follow-up:* So does
"it's behind NAT" protect my traffic? No -NAT provides address translation and incidental
inbound filtering, not confidentiality or integrity, "Red flag:* Assuming NAT or a bare
tunnel encrypts traffic.

## Synthesis

Routing *decides* the path, forwarding *moves* the packet, **NAT rewrites** address/port
fields (same packet, changed headers, translation state), and **tunneling encapsulates**
the whole packet in a new outer packet. The core difference between a **forwarder
(NAT/router, proxy-style "VPNs")** and a **tunnel** is **rewrite/move vs. wrap**: a
forwarder operates on the existing packet - inspecting and possibly rewriting headers,
sending it onward, so the original header is visible/modified and inner addressing is
*not* preserved; a tunnel treats the original packet as opaque payload, prepends an outer
header, routes the underlay on that outer header only, and recovers the exact inner packet
at the far endpoint, *preserving end-to-end inner addressing** and enabling carriage of
private addresses / foreign protocols / isolated overlays. "VPN" spans both - most
IP-layer VPNs (IPsec/NireGuard/OpenVPN) are **tunnels**, proxy/relay VPNs are
**forwarders** - so reason about the *mechanism*, not the label. The practical dividing
line is addressing semantics (NAT breaks end-to-end addressing and foreign-protocol
carriage; tunnels preserve them) traded against overhead (tunnels add
encapsulation/TU/CPU); and they **compose** -a tunnel's outer packet is itself routed,
NATed, and forwarded by the underlay, which is exactly why NAT-traversal (UDP
encapsulation) matters for tunnels.

## References

- RFC 1812- Requirements for IPv4 routers (forwarding)
- RFC 3022 - Traditional NAT (NAPT)
- RFC 4787 / RFC 5382 - NAT behavioral requirements (UDP / TCP)
- RFC 6888 - Carrier-grade NAT (CGN) requirements
- RFC 5128 - Peer-to-peer communication across NATs (hairpinning, hole punching)
- RFC 3948 - UDP encapsulation of ESP (why ESP needs NAT-T; AH cannot cross NAT because it
  thenticates the IP header)
- RFC 2784 - GRE
- RFC 4301 - IPsec architecture
