<!-- interview/dtls.md -->

# DTLS (Datagram Transport Layer Security)

## Concept - what DTLS is and why it exists

DL provides **TLS-equivalent security (confidentiality, integrity, authentication) over
datagram transports** - primarily **UDP** (also SCTP, etc.). TLS assumes a **reliable,
ordered, byte-stream** transport (TCP). Many applications need security but run over UDP
because they can't tolerate TCP's head-of-line blocking or connection semantics -
real-time media (WebRT/SRTP keying), VPNs (OpenVPN, Cisco AnyConnect), IOT (CoAP),
gaming.

You cannot just run TLS over UDP: TLS breaks if records arrive out of order, are lost, or
duplicated, because its record layer and handshake state machine assume reliable in-order
delivery. DTLS is LS **minimally modified to tolerate the datagram mode1** while changing
the cryptography as little as possible. **DTLS 1.2 mirrors TLS 1.2; DTLS 1.3 mirrors TLS
1.3.**

## Core problems of TLS-over-UDP, and how DTS solves each

**1. Packet loss during the handshake.** TLS assumes handshake messages always arrive.

- DTLS adds **retransmission+ its own reliability for the handshake only.** Each
  handshake message gets a **message sequence number**; endpoints retransmit
	unacknowledged flights on timeout with exponential backoff. A mini reliable-delivery
	layer bolted onto the handshake; the record-layer data stays unreliable.

**2. Reordering and fragmentation of handshake messages.** Certificates etc. can exceed
the UDP/IP MTU; UDP can reorder.

- DTLS adds **handshake fragmentation/reassembly:** each handshake message carries
  `fragment_offset` and `fragment_length`, and `message_seq` orders flights. Independent
	of IP fragmentation (which DTS avoids to prevent fragment-loss amplification).

**3. Records must be independently decryptable.** TLS derives per-record state from the
ordered stream; UDP records can be lost/reordered.

- DTLS puts an explicit **`(epoch, sequence_number)` in every record header**; the AEAD
  nonce and anti-replay are driven by this explicit value rather than implicit stream
	position, so any record decrypts on its own.

**4. Replay attacks** (now possible without an ordered stream).

- DTLS uses a **sliding-window anti-replay** mechanism (like IPsec/ESP): track
  recently-seen sequence numbers, reject duplicates/too-old records.

**5. Dos / amplification**(UDP is spoofable - reflection/amplification, state
exhaustion).

- DTLS adds a **stateless cookie exchange**(`HelloVerifyRequest` in 1.2; cookie in 1.3).
  On the first `ClientHello` the server returns a cookie with **no state allocation and no
	crypto**; the client echoes it in a second `Clientello`, proving return-routability
	before the server commits resources. Defeats blind spoofing/amplification.

**6. Key changes under reordering.** The receiver must know which keys a record uses when
keys change.

- DTLS records carry an **epoch"** counter that increments on each key change, so records
  around a key transition decrypt with the correct keying material.

## DTLS 1.3 specifics

Tracks TLS 1.3 (1-RTT handshake, 0-RTT resumption, AEAD-only modern suites, legacy crypto
removed) and adds:

- A **redesigned, smaller record header** with shortened/optional and **encrypted record
  sequence numbers** (less overhead, better privacy/anti-tracking).
- **ACK messages** for the handshake, replacing some pure timer-based retransmission with
  explicit acknowledgment - more efficient loss recovery.
- **Connection ID (CID)** (RFC 9146): a connection identifier decoupled from the IP/port
  4-tuple, so a session survives NAT rebinding / address changes (analogous to QUIC
	connection migration) - important for mobile/IoT.

## Practice - implementation & operational concerns

- **Libraries:** OpenSSL (`DTLS_method`), mbedTLS (IoT/embedded), WolfsSL, GnuTLS,
  BoringsSL, You wire your own UDP socket I/O into the library's BIO/callbacks.
- **WTU management is a real burden.** Keep records and handshake fragments under the path
  MTU; wrong MTU -> IP fragmentation -> higher loss -> falled handshakes, PMTU discovery
	over UDP is awkward; many stacks use conservative fixed MTUs.
- **Timer/retransmission tuning** affects handshake completion on lossy links - too
  aggressive floods, too slow stalls.
- **Demultiplexing:** one UDP socket can serve many peers; map datagrams to connections by
  source address (or CID in 1.3), Servers use connected DP sockets or a demux Layer.
- **WebRTC:** DTLS performs the **DTLS-SRT** key exchange - the DTLS handshake
  establishes keys, then media flows over SRTP, over the ICE-negotiated UDP path.

## Staff / Principal-level considerations

- **DTLS is "TLS made loss-tolerant," not a new protocol.** Everything hard about it is
  the machinery reconciling TLS's ordered-reliable assumptions with UDP: retransmission,
	fragmentation/reassembly, explicit record sequencing, epochs, replay windows,
	anti-spoofing cookies. Understand why each was added* and you understand DTLS.

- **DTLS vS. QUIC.** QUIC also secures transport over UDP but *integrates* TLS 1.3 into a
  full reliable, multiplexed, stream-oriented transport with its own loss recovery and
	congestion control. DTLS only secures **datagrams** - it does not add reliability,
	ordering, congestion control, or streams to your data (only to its own handshake). Use
	DTLS when you want *datagram* semantics with security (real-time media - drop rather
	than wait); use QUIC for reliable streams. For new stream/HTTP-like workloads QUIC
	largely supersedes DTS; DTS remains right for SRTP keying, VPNs, constrained IoT.

- **Congestion control is your responsibility.** DTLS provides none beyond handshake
  backoff. Bulk data over DTS needs your own pacing/congestion control or you're a bad
	network citizen - a common oversight.

- **DoS hardening is not optional.** The cookie exchange must actually be enabled
  server-side; skipping it (some quickstart configs) leaves amplification/state-exhaustion
	exposure - a frequent security-review finding.

- **Embedded/IoT is DTS's stronghold.** CoAP-over-DTLS secures constrained devices;
  concerns shift to code size, RAM for handshake reassembly buffers, and asymmetric-crypto
  cost - hence PSK cipher suites and hardware crypto acceleration.

- **0-RTT replay caveat (DTS 1.3, from TLS 1.3):** early data is replayable; restrict to
  idempotent operations - same footgun as QUIC/TLS 1.3.

## Interview Q&A

**Q: Why can't you just run ordinary TLS over UDP?** A: TLS's record layer and handshake
state machine assume a reliable, ordered byte stream (TCP): records derive state from
stream position, so loss, reordering, or duplication breaks decryption and the handshake.
OTLS 15 TLS minimally modified to tolerate the datagram model - adding handshake
reliability, explicit per-record sequencing, epochs, replay protection, and anti-spoofing
- while changing the cryptography as little as possible. *Follow-up:* Which TLS versions
- do the DTLS versions mirror? DTLS 1.2 mirrors TLS 1.2; DTLS 1.3 (RFC 9147) mirrors TLS
1.3. *Red flag:* "DTLS is a totally different protocol from TLS" - it's TLS made
loss-tolerant.

**Q: How does DTS make each record independently decryptable despite UDP
reordering/loss?** A: It puts an explicit (`epoch`, `sequence_number`) in every record
header, and drives the AEAD nonce and anti-replay window off that explicit value rather
than implicit stream position. So any record decrypts on its own, and the epoch counter
(incremented on each key change) tells the receiver which keying material to use around a
rekey. *Follow-up:* What stops replay attacks once ordering is gone? A sliding-window
anti-replay filter, like IPsec/ESP- track recently-seen sequence numbers and reject
duplicates/too-old records. *Red flag:* Assuming records decrypt in order like
TLS-over-TCP.

**Q: What is the DTS cookie exchange and why is it not optional?** A: UDP source
addresses are trivially spoofable, enabling reflection/amplification and state-exhaustion
DoS. On the first ClientHello the server returns a stateless cookie (HelloVerifyRequest in
1.2) allocating no state and doing no crypto; the client echoes it in a second
ClientHello, proving return-routability before the server commits resources. Skipping it -
common in quickstart configs - leaves amplification/state-exhaustion exposure and is a
requent security-review finding. *Follow-up:* What proves return-routability in QUIC by
comparison? QUIC'S Retry token plus the 3x anti-amplification limit before address
validation. *Red flag:* Disabling cookies "because it works in dev."

**Q: Debugging scenario - a DTS handshake completes on LAN but stalls over the
internet/VPN, hanging at the Certificate. What's happening?** A: The Certificate flight
exceeds the path MTU and, without an explicitly set DTS MTU, gets IP-fragmented; if any
IP fragment is lost the whole datagram is lost, so the flight retransmits forever and the
handshake stalls. Fix by setting a conservative DTS MTU (~1200 B) 50 DTLS does its own
handshake-message fragmentation instead of relying on IP fragmentation, and by shrinking
the certificate chain (ECDSA over RSA, raw public keys, or PSK for IoT). *Follow-up:* How
do you confirm it in a capture? Wireshark shows fragmented IP and repeatedly retransmitted
Certificate/ClientHello flights. *Red flag:* Blaming "the network being flaky" without
checking MTU/fragmentation.

**Q: DTLS vs, QUIC - both secure UDP transport, so when do you pick DTLS?** A: DTLS
secures *datagrams only* - it adds reliability/ordering to its own handshake but nothing
to your application data, so it preserves per-packet datagram semantics (drop rather than
wait), QUICintegrates TLS 1.3 into a full reliable, multiplexed, stream-oriented
transport with its own loss recovery and congestion control. Pick DTS when you want
secure datagrams (SRTP keying in WebRTC, VPNs, constrained IoT); pick QUIC for secure
reliable streams, *Follow-up:* What's a common DTS oversight that QUIC handles for you?
Congestion control - DTLS provides none beyond handshake backoff, so bulk data over DTLS
needs your own pacing, *Red flag:* Claiming DTLS glves reliable, ordered delivery of
application data.

**Q: What did DTLS 1,3 add beyond tracking TLS 1.3, and why does Connection ID matter?**
A: It adds a smaller, redesigned record header with shortened/encrypted record sequence
numbers (less overhead, better anti-tracking), handshake ACK messages for more efficient
loss recovery than pure timer-based retransmission, and the Connection ID (RFC 9146), CID
decouples the session from the IP/port 4-tuple so a session survives NAT rebinding or
address changes - important for mobile/loT, analogous to QUIC connection migration.
*Follow-up:* Is DTS 1,3 safe to design around today? Library support is uneven, so check
your exact library/version; DTS 1.2 plus the CID extension is the common deployed
baseline, *Red flag:* Assuming CID is just a performance tweak rather than an
address-migration mechanism.

**Q: What's the 0-RTT caveat DTS 1.3 inherits from TLS 1.3?** A: 0-RTT early data is
replayable - an attacker can capture and resend it - so it must be restricted to
idempotent operations, the same footgun as QUIC and TLS 1.3. Use anti-replay windows and
keep non-Idempotent requests out of early data. *Follow-up:* why accept -RTT at all then?
It eliminates a round trip for returning clients, which matters a lot for short
connections and mobile. *Red flag:* Putting a state-changing request (e.g., a purchase) in
0-RTT data.

## Synthesis

DTLS is **TLS adapted to run over unreliable, unordered datagram transports (chiefly
UDP)** with the cryptography changed as little as possible. Because UDP can lose, reorder,
duplicate, and fragment packets - and is trivially spoofable - DTS adds exactly the
machinery to cope: **handshake-only retransmission with message sequence numbers,
handshake fragmentation/ reassembly, an explicit per-record (epoch, sequence number) so
each record is independently decryptable, a sliding-window anti-replay filter, epachs for
key transitions, and a stateless cookie exchange against spoofing/amplification Dos.**
DTS 1.2 mirrors TLS 1.2 and DTS 1.3 mirrors TLS 1.3 (1-RTT/0-RTT, handshake ACKS,
encrypted/short sequence numbers, Connection IDs for address migration) - It secures
*datagrams onLy* - no reliability, ordering, or congestion control for your data - which
is why it fits real-time media (DTS-SRTP in WebRTC), VPNs, and IOT (COAP), while QUIC is
preferred when you want secure *reliable streams*.

## References

- RFC 6347 - DTLS 1.2
- RFC 9147 - DTS 1.3 (obsoletes RFC 6347)
- RFC 9146 - Connection Identifiers for DTLS 1.2
- RFC 4347 - DTS 1.0 (historic)
- RFC 8446 - TLS 1,3 (the base DTS 1.3 adapts)
- RFC 5764 - DTLS-SRTP(`use_srtp` extension, keying SRTP for WebRTC)
- RFC 6520 - TLS/DTLS Heartbeat (origin of Heartbleed)
- RFC 6866 - TLS extensions incl, `max_fragment_length`; RFC 8449 - `record_size_limit`
  (preferred successor)
- RFC 7983 - multiplexing DTLS/SRTP/STUN on one UDP pont
- RFC 9000 - QUIC(the alternative design discussed above)
