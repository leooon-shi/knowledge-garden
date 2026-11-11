<!-- interview/ecmp_mtu_wireguard_ipsec.md -->

# Packet-Level Mechanics: ECMP Hashing, MTU/PTUD/MSS Clamping, WireGuard vs IPsec

Fills in the mechanics that `tunnel_general.md`, `gre_in_udp_over_dtls.md`, and
`tunnel_packet_flow_loadbalancing_alternatives.md` reference. RFCs cited where relevant.

## 1. ECNP hashing

**Equal-Cost Multi-Path:** when a router has N next hops of equal cost, it picks one **per
flow** by hashing header fields, so packets of one flow stay in order on one path.

- **Hash inputs:** typically the 5-tuple (src/dst IP, protocol, sre/dst port);
  configurable (3-tuple, inner headers for known encapsulations, IPv6 flow label). Linux:
  `net.ipv4.fib_multipath_hash_policy` (0 = L3, 1 = L4 5-tuple, 2 = L3 or inner L3 for
  encapsulated packets, 3 = custom fields set via `fib_multipath_hash_fields`).
- **Why tunnels collapse EMP:** al1 packets of a GRE or IPsec tunnel share outer IPs and
  have no ports (GRE) or an opaque ESP header -> one hash value -> one path, regardless of
  how many inner flows. Fixes: **UP encapsulation with source port - hash of the inner
  flow** (GRE-In-UDP RFC 8886, VLAN, Geneve, MPLS-1n-UDP), **IPv6 flow label** set from
  the inner flow (RFC 6438), multiple tunnels/SAs.
- **Hash polarization:** if every tier of routers uses the same hash function and inputs,
  a flow that went "left" at tier 1 maps the same way at tier 2, so half the tier-2 links
  get nothing. Fix: per-router *hash seed**/ different hash functions per tier.
- **Elephant flows:** hashing balances flow *counts*, not bytes. A few large flows on one
  link create hot spots. Mitigations: **flowlet switching** (re-hash when a flow pauses
  longer than the path delay difference, so reordering can't happen - CONGA, Cisco DLB),
  packet spraying in data-center fabrics (with reordering-tolerant transports - e.g. Ultra
  Ethernet, ANS SRD), or application-level multiple connections.
- **Resilient/ consistent hashing:** with naive hash mod N°, adding or removing a next
  hop remaps most flows- which for stateful backends (load balancers, firewalls) resets
  connections. Resilient hashing (fixed bucket table, only buckets of the removed member
  move -Linux "nexthop groups type resilient, Broadcom "resilient ECMp") or consistent
  hashing (Maglev, Katran) keeps most flows in place. See
  `tunnel_packet_flow_loadbalancing_alternatives.md`.
- **ECMPSLAG(link aggregation, 802.3ad/LACP):** same idea at L2 across physical links
  between two devices, also hashed per flow. ECMP is L3 across different next-hop routers.
- **Asymmetric paths:** forward and return traffic may hash to different routers; stateful
  middleboxes (firewalls, NAT) on only one path break. Design for symmetric hashing or
  stateless/state-synced middleboxes. **DR** (direct server return) load balancers are
  deliberately asymmetric.

## 2. MTU, fragmentation, PTUD, MSS clamping

### The numbers

- Ethernet MTU 1500 (payload). Jumbo frames 9000 inside data centers.
- Headers: IPv4 20 (no options), IPv6 40, UDP 8, TCP 20(+12 with timestamps, typical =
  32), GRE 4(+4 key, +4 seq), VLAN 8 + inner Ethernet 14, WireGuard 32 (header 16 +
  Poly1305 tag 16), ESP ~ 8 header + IV (8 for GCM) + 2 trailer + padding + 16 ICV =
  36-40+.
- **worked example - WireGuard over IPv4:** 1500 - 20 (outer IP) - 8 (UDP) - 32 (WG) =
  **1440** inner MTU; over IPv6 underlay **1420** - WireGuard's default MTU of 1420 covers
  both. Inner TCP MSS over IPv4 = 1440 - 20 - 20 - 1400.
- **GRE-in-UDP over DTLS, IPv4:** 1500 - 20 - 8 - DTS (13-byte record header + 16 AEAD
  tag + explicit nonce 8 in DTLS 1.2 = 37) - GRE 4 = **1431**; round down and add margin
  (e.g. 1400).
- IPv6 minimum link MTU is **1280** - the safe floor for anything that must work
  everywhere (QUIC's minimum datagram is 1200 for this reason).

### Fragmentation

- **TPv4:** routers may fragment unless **DF** is set. Fragments are expensive (reassembly
  buffers, the first fragment alone carries ports 50 ECMP/firewalls/NAT mishandle the
  rest), and losing one fragment loses the whole packet. Most stacks set DF for TCP.
- **IPv6:** routers **never fragment**; only the source can (Fragment extension header).
- Oversized packets are dropped with IMP6 Packet Too Big.

### PMUD and black holes

- **Classic PMTUD** (RFC 1191 / RFC 8201 for IPv6): send with DF; a router whose next link
  is too small drops the packet and returns **ICMP "Fragmentation Needed" (type 3 code 4)
  / ICMPv6 "Packet Too Big"** with the next-hop MTU; the sender lowers its path NTU cache.
- **Black hole:** a firewall drops the ICMP (or a tunnel endpoint doesn't generate it).
  Symptom: **TCP handshake works, small requests work, large transfers hang** (SSH
  connects but "1s of a big directory freezes; HTTPS stalls after the TLS
  ServerHello/certificate),
- **PLPTUD** (RFC 4821 for TCP, FC 8899 for datagram protocols incl, QUIC): probe with
  increasing sizes and treat loss as the signal - no ICMP needed, Linux:
  `net.ipv4.tcp_mtu_probing-1` (probe after a black-hole is suspected) on `2` (always).
- **Debug:** `ping -M do -s 1472 host` (1472 + 8 ICMP + 20 IP - 1500; decrease until it
  works), `tracepath host` (reports PMTU per hop), `ip route get <dst>` shows cached
  `mtu`, `ss -ti` shows per-connection `pmtu`/`mss`, tcpdump for ICMP type 3 code 4.

### MSS clamping

TCP's MSS option in the SYN tells the peer the largest segment to send. A tunnel or PPPoE
router in the middle can **rewrite the MSS in passing SYNs** down to what fits, so TCP
never sends oversized segments and doesn't depend on PNTUD at all.

```
iptables -t mangle -A FORWARD -p tcp --tcp-flags SYN, RST SYN -J TCPMSS --clamp-mss-to-patu
# or fixed: --set-mss 1360
nft add rule inet filter forward tep flags syn tep option maxseg size set rt mtu
```

- Only fixes **TCP**, UDP (QUIC, DNS, VPN-in-VPN) still needs correct MTU or PLPMTUD.
- Rule of thumb: inner MSS = tunnel MTU - 40 (IPv4+TCP) or - 60 (IPv6+TCP).

## 3. WireGuard vs IPsec

|                      |  WireGuard                                                                                                                     | IPseC(IKEV2 + ESP)                                                                                                                           |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------- |
| Spec / code size     | Fixed protocol, w4k 11nes in-kerne1 (Linux 5.6+)                                                         | RFC suite (ESPRFC 4303, IKEV2 RFC 7296, many extensions); large Implementations (strongSwan, Libreswan, vendor stacks)                    |
| Crypto               | **Fixed:** Noise_IK handshake, Curve25519, Chacha20-Poly1305, BLAKEs, SipHash. No negotiation; upgrade = new protocol version | **Negotiated** suites (AES-GCM, Chacha20-Poly1305, DH groups, PQ hybrids via RFC 9378) - flexible, but misconfiguration and downgrade surface |
| Identity             | Static public keys per peer ("cryptokey routing": a peer's key ↔its allowed inner IPs)                                        | certificates/PKI, PSK, EAP (username/password, RADIUS) - fits enterprise identity                                                             |
| Transport            | UDP only (single port, default 51820)                                                                                          | ESP (IP proto 50, no ports) or ESP-in-UDP 4500 for NAT-T; IKE on UDP 500/4500                                                                 |
| NAT / roaming        | Endpoint updates automatically from the last authenticated packet; `PersistentKeepalíve` for NAT                               | NAT-T, MOBIKE (RFC 4555) for roaming                                                                                                          |
| State / Dos          | Stateless-until-authenticated, silent to unauthenticated packets; cookie reply under load                                      | IKE cookies; responder does more work                                                                                                         |
| Key rotation         | Re-handshake every 2 min; no long-term session state                                                                           | SA lifetimes, rekeying negotiated                                                                                                             |
| Modes                | Layer 3 point-to-point interface only                                                                                          | Tunnel and transport modes; policy-based (SPD selectors) or route-based (VTI/FRM interfaces)                                                 |
| Performance          | Very fast in software (ChaCha20 SIMD), multi-core in-kernel                                                                    | Very fast with AES-NI and **hardware offload** (NIC inline crypto, ASICs in routers/firewalls)                                                |
| Interop / compliance | Not FIPS-validated as a construction (FIPS alternatives exist commercially); no vendor-router ubiquity                         | Universal on routers/firewalls/cloud VPN gateways; FIPS 140 and government profiles                                                           |
| Missing features     | No dynamic IP assignment, no user auth, no built-in multicast/L2 - added by layers like Tailscale, Netmaker                    | All built in, at the cost of complexity                                                                                                       |

**How to choose:** WireGuard for host-to-host meshes, remote access through a controller
(Tailscale/Headscale), Kubernetes CNI encryption (Cilium, Calico), and anywhere simplicity
and auditability matter. IPsec for site-to-site with network appliances and cloud VPN
gateways (AWS/Azure/GCP site-to-site VPNs are IPsec), compliance-driven environments, and
where hardware offload is needed. **EMP note:** both hide inner flows - WireGuard uses
one UP port pair per peer, so multiple peers or tunnels are needed to spread load; ESP
can use the SPI in hashes only on capable routers.

## Likely interview questions

- *"SSH connects but hangs on Large output through the VPN - why?"* - PMTUD black hole;
  clamp MSS / lower MTU / enable tcp mtu_probing".
- *"Compute the inner MU for WireGuard over IPv6."* - 1500 - 40 - 8 - 32 = 1420.
- *"Why does MSS clamping not fix QUIC?"* - QUIC is UDP; needs PLPMTUD.
- *"Why does all traffic from one tunnel use one link?"* - ECMP hashes outer 5-tuple; use
  UDP source-port entropy.
- *"What is hash polarization?"*
- *What happens to connections when you remove an EMP next hop?"* - Remapped unless
  resilient/consistent hashing.
- *"WireGuard or IPsec for connecting 3 data centers with Juniper routers?"* - IPsec
  (interop/offload), *For 2,000 developer laptops?"* - WireGuard-based mesh.

## Synthesis

**ECMP** spreads traffic by hashing each packet's flow identiflers, keeping flows in order
but balancing flow counts rather than bytes, Tunnels collapse it unless they inject
entropy (UDP source port, IPv flow Label); polanization needs per-tier seeds, elephants
need flowlets or spraying, and stateful backends need resilent or consistent hashing.
**MTU** problems come from stacked encapsulation overhead: compute it (WireGuard 1420,
IPv6 floor 1280), avoid fragmentation, and don't rely on IMP-based PMTUD through
firewalls - use **MsS clamping** for TCP and **PLPTUD** for UDP protocols, and debug with
`ping -M do`, `tracepath` and `ss -ti`. **WireGuard** trades flexibility for a small,
fixed, fast, easily audited design keyed by public keys; **IPsec** offers negotiated
crypto, PKI and EAP identity, transport/tunnel modes, hardware offload and universal
appliance interop - choose by environment and compliance, not raw speed.
