<!-- interview/dtls_udp_programming_traps.md -->

# DTLS-over-UDP Programming: Traps, Detection, and Resolution

Concrete issues that bite when wiring a DLS 1ibrary (OpensSL/ mbedTLS / WolfSSL) to a
raw UDP socket, grouped by where they hurt: demux, handshake, MTU, timers, lifecycle,
security.

**Version-support trap:** DTS 1.3 (RFC-9147) library support is uneven. wolfSSL has
shipped it for years; mbedTS and OpensSLsupport has lagged behind TS 1.3 - check your
exact library version's release notes before designing around DTS 1.3 features such as
Connection ID-based demux or the shorter handshake. DTLS 1.2 (RFC-6347) plus the CID
extension (RFC-9146) is the common deployed baseline.

## Trap 1 - Wrong UDP socket model for the server (demultiplexing)

One `SSL*`/DTLS context 1s bound to one peer,but an unconnected UDPsocket receives
datagrams from all peers. Feeding peer B's datagrams into peer A's state falls to decrypt
or cross-contaminates sessions.

- **Identify:** handshakes fail only with multiple concurrent clients; MAC/decrypt errors
  only with >1 peer; wrong source in captures.
- **Resolve:** after the cookie exchange, create a **connected UP socket per client**
  (`connect()` so the kernel filters), or keep an explieit `map<peer_addr, connection>`
  demux. OpenSSL's ***DTLSv1_listen()** handles the stateless cookle phase and returns
  the peer address to bind a per-client socket. In DTS 1.3 use the **Connection ID** for
  demux.

## Trap 2 - Misusing `DTLSv1_listen()` / the cookie exchange

`DTLSv1_listen()` must run on a non-connected socket, may need re-entry, and needs correct
`generate_cookie`/`verify_cookie` callbacks keyed by client address. Disabling cookies
(tempting in dev) opens amplification/state-exhaustion Dos.

- **Identify:** server allocates state for spoofed/unfinished handshakes (memory grows
  under flood); `DTLSv1_listen` returning repeatedly; handshake never completing behind
  NAT.
- **Resolve:** cookie callbacks bind the cookie to client IP:port fath a server secret;
  loop `DTLSv1_listen` correctly; allocate the per-connection `SSL` + connected socket
  only after a verified cookie. Never disable cookies in production.

## Trap 3- MTU/fragmentation (the single most common real-world failure)

DTLS records must fit the path MTU. A handshake flight (usually the Certificate) exceeding
it without a set DTLS MTU causes **IP fragmentation**; if any IP fragement is lost the
whole datagram is lost, so the handshake stalls and retransmits forever.

- **Identify:** handshake **hangs at the Certificate message**; works on loopback/LAN,
  fails over internet/VPN; `tcpdump` shows fragmented IP or retransmitted
  ClientHello/Certificate flights; failures correlate with large cert chains.
- **Resolve:**
  - Set the DTLS MTU explicitly: `SSL_set_mtu()` / `DTLS_set_link_mtu()` (OpenSSL),
    `mbedtls_ssl_set_mtu()`, conservatively (~1200 bytes to survive tunnels).
  - Enable handshake-message fragmentation so DTLS splits flights into MTU-sized fragments
    instead of relying on IP fragmentation.
  - Set `IP_MTU_DISCOVER`/DF appropriately, or use a conservative fixed MTU.
  - **Shrink the certificate chain** (fewer/smaller certs, ECDSA over RSA, raw public
    keys/PSK for IoT) - often the cleanest fix.

## Trap 4- Driving the retransmission timer yourself

DTS handshake reliability is timer-based, but the library does not run its own clock -
you must call the timeout function. Blocking forever in `recvfrom` waiting for a lost
packet hangs the handshake.

- **Identify;** handshakes hang on any packet loss; fine on clean networks, fall on
  lossy/wireless; app stuck in a blocking read.
- **Resolve:** integrate into an event loop. OpenSSL: `DTLSv1_get_timeout()` for the next
  timeout, `select/poll/epoll` **with that timeout**, on expiry call
  `DTLSv1_handle_timeout()` to retransmit; loop. mbedTS: set `mbedtls_ssl_set_timer_cb()`
  and honor "WANT_READ/timeout returns. Never plain blocking `recvfrom` during the
  handshake,

## Trap 5 - `SSL_read`/`SSL_write` return-code & datagram-boundary semantics

DTLS is **message-oriented**, not a stream:

- One `SSL_read` returns exactly one datagram's payload; no stream coalescing; a too-small
  buffer **truncates** the record (data lost, not buffered),
- `SSL_ERROR_WANT_READ`/`WANT_WRITE` on non-blocking sockets must be handled, not treated as
  fatal.
- A record may be a retransmit/reordered; app-data can interleave with late handshake
  records.
- **Identify;** truncated/garbled app messages; tearing down on `WANT_READ`; assuming read
  boundaries match write boundaries.
- **Resolve:** buffer >= max record size (bounded by MTU); handle `WANT_READ`/`WANT_WRITE`
  and re-drive I/O; treat each `SSL_read` as one message; frame the app protocol as
  datagrams.

## Trap 6 - Silent packet loss looks like an application bug

DTLS does not recover lost **application** records (only the handshake is reliable). TCP
habits make devs assume delivery.

- **Identify:** app messages occasionally vanish with no error; loss tracks network
  conditions; captures show drops.
- **Resolve:** accept app data is unreliable by design - add app-layer
  sequence/acks/retransmission if needed, or design loss-tolerant (idempotent,
  self-contained) messages. If you truly need reliable ordered delivery, use QUIC or
  DTLS-over-SCTP.

## Trap 7-Epoch /key-change and out-of-order records around rekey

Records around a `ChangeCipherSpec`/rekey may belong to a different **epoch**; naive
handling drops valid records or fails to decrypt reordered ones.

- **Identify:** decrypt failures clustered around renegotiation/rekey/resumption; sporadic
  MAC errors right after handshake completes.
- **Resolve:** let the 1ibrary handle epochs; don't discard records with an unexpected
  epoch during transitions; keep the previous epoch's keys briefly for late records. Avoid
  DTLS 1.2 renegotiation; prefer 1.3.

## Trap 8 - Anti-replay window dropping legitimate traffic

The sliding-window replay filter can drop legitimately **reordered** records if the window
is too small or the path reorders heavily.

- **Identify:** valid records rejected as replays under reordering: loss correlates with
  burst/parallel-path delivery, not real duplication.
- **Resolve:** tune/enable the replay window for your path; understand the tradeoff before
  disabling (disabling opens replay risk). Usually leave on with a sensible window.

## Trap 9 - Connection teardown and "closed" detection over UDP

UDP has no close; a dead peer just goes silent and `close_notify` can be lost.
Half-open/stale sessions leak resources.

- **Identify:** server accumulates dead sessions; no clean gone-peer detection; resource
  growth over time.
- **Resolve:** app-level **keepalive/heartbeat** + idle timeouts to reap dead sessions;
  send/expect `close_notify` but don't rely on it; DTLS 1.3 CID + heartbeats. Bound
  per-connection memory, expire aggressively.

# Trap 10 - Testing only on loopback/LAN

Works on `localhost` (no loss/reorder, huge MTU), fails in production (loss, reorder,
small MTU behind VPN/NAT).

- **Identify:** "works on my machine," fails in the field; failures correlate with real
  paths.
- **Resolve:** test with a network emulator - **`tc netem`** to inject loss, delay,
  reordering, duplication, small MTU; fuzz packet order/loss; soak-test. Surfaces Traps 3,
  4, 6, 8 before customers do.

## General detection toolbox

- **`tcpdump` / Wireshark** - Wireshark decodes DTS; watch retransmitted flights, IP
  fragmentation, ClientHello loops (cookie issues), epoch/sequence numbers. The #1
  diagnostic.
- **`SSL_get_error()` / mbedTLS return codes** - never ignore;
  `WANT_READ`/`WANT_WRITE`/`ZERO_RETURN`/timeout each mean something specific.
- **OpenSSL `SSL_CT_set_info_callback` / `SSLKEYLOGFILE`** - trace handshake state and
  decrypt captures in Wireshark.
- **`tc netem`** - reproduce loss/reorder/MTU deterministically.
- **Counters** - retransmissions, decrypt failures, replay drops, per-epoch stats; alert
  on anomalies.

## Staff / Principal-level themes

- **Recurring root cause: TLS-over-TCP habits applied to datagrams** - blocking reads,
  assuring reliability/stream boundaries, ignoring MTU, forgetting the timer. Onboard the
  team on the datagram mental model explicitly.
- **MTU and the timer loop hold ~80% of real bugs.** A "hung handshake" is usually
  MTU/fragmentation (Trap 3) or a missing `handle_timeout` (Trap 4); they mimic each
  other.
- **Make loss/reorder a first-class test input** (`netem` in CI), not an afterthought.
- **Prefer library built-ins** over hand-rolling epochs, replay windows, fragmentation,
  cookies - hand-rolled versions become security holes.
- **Question whether DTS is the right layer**- 1f you re-implement reliability +
  ordering + congestion control on top of it, you've built a worse TCP/QUIC; switch to
  QUIC or DTLS-over-SCTP.

## Interview Q&A

**Q: Your DTSserverworks with one client but handshakes fal with MAC/decrypt errors
once multiple clients connect concurrently, What's the bug?** At Wrong UP socket model,
One `SSL*`/DTS context is bound to one peer, but an unconnected UP socket receives
datagrams from *all* peers, so peer B's datagrams get fed into peer A's state and fall to
decrypt or cross-contaminate sessions. Fix by demultiplexing: after the cookte exchange.
create a connected UP socket per client (so the kernel filters by peer), or keep an
explicit` map<peer_addr,connection>`; in DTLS 1.3 use the Connection ID, *Follow-up:*
What OpenSSL primitive helps here? `DTLSv1_listen()` handles the stateless cookle phase
and returns the peer address so you can bind a per-client socket. *Red flag:* "Just use
one shared sSLcontext for all clients."

**Q: Why is driving the retransmission timer yourself a trap, and how do you do it
right?** A: DTLS handshake reliability is timer-based, but the library doesn't run its own
clock - if you sit blocked in revfrom waiting for a lost packet, the handshake hangs
forever on any loss. Integrate the timer into an event loop: OpenSSL
`DTLSv1_get_timeout()` gives the next timeout, pass it to `select`/`poll`/`epoll`, and on
expiry call `DTLSv1_handle_timeout()` to retransmit; loop. mbedTLS: set
`mbedt1s_ss1_set_timer_cb()` and honor WANT_READ/timeout returns. *Follow-up:* How does
this failure present versus an MTU problem? Both show a hung handshake; the timer bug
hangs on *any* packet loss (fine on clean nets), while MTU hangs specifically at the large
Certificate flight. *Red flag:* plain blocking `recvfrom` during the handshake with no
timeout handling.

**Q: Application messages occasionally vanish over DTS with no error, Bug or by design?**
A: By design. DTS only makes the *handshake* reliable; application records are not
retransmitted, so lost app data silently disappears - TCP habits make developers assume
delivery. Either tolerate it (idempotent, self-contained messages), add app-layer sequence
numbers/acks/retransmission, or if you truly need reliable ordered delivery switch to QUIC
or DTLS-over-SCTP. *Follow-up:* How do you confirm it's network loss not an app bug?
Correlate the drops with network conditions and check a `tcpdump`/`netem` capture showing
the drops. *Red flag:* Adding ever more app-layer retries unti1 you've rebuilt a worse TCP
on top of DTLS.

**Q: Why does `SSL_read` on a DTS socket behave differently from a TCP stream read?** A:
DLS is message-oriented: one "SSL_read returns exactly one datagram's payload with no
stream coalescing, and a too-small buffer *truncates* the record (data lost, not
buffered). You must size the buffer >= max record size, treat each read as one message,
and handle `SSL_ERROR WANT_READ`/`WANT WRITE` on non-blocking sockets rather than treating
them as fatal. *Follow-up:* Do read boundaries match write boundaries? For datagrams, one
write maps to one read - but you must frame the app protocol as datagrams and not assume
stream coalescing. *Red flag:* Reusing TCP stream-parsing logic and tearing down on
WANT_READ.

**Q: You see clustered decrypt/MAC failures right around rekey or resumption. what causes
it and how do you handle it?** A: Records around a ChangeCipherSpec/rekey may belong to a
different epoch; naive handling drops valid records or fails to decrypt reordered ones
that still belong to the previous epoch. Let the library handle epochs, don't discard
records with an unexpected epoch during transitions, and keep the previous epoch's keys
briefly for late records. Prefer DTS 1.3 and avoid DTS 1.2 renegotiation. *Follow-up:*
What's a related trap with the anti-replay window? Too small a window (or a heavily
reordering path) drops legitimately reordered records as replays - tune the window rather
than disabling it. *Red flag:* Hand-rolling epoch handling instead of trusting library
built-ins.

**Q: How do you reproduce and catch these datagram bugs before production?** A: Don't test
only on loopback/LAN - it has no loss, no reordering, and a huge MTU, hiding Traps
3/4/6/8. Use `tc netem` to inject loss, delay, reordering, duplication, and a small MTU in
CI, and use Wireshark (which decodes DTS) to watch for retransmitted flights, IP
fragmentation, ClientHello loops (cookie issues), and epoch/sequence anomalies. Add
counters for retransmissions, decrypt failures, and replay drops. *Follow-up:* What single
tool is the #1 diagnostic? Wireshark/ tcpdump" for inspecting the actual datagrams on the
wire. *Red flag:* "Works on my machine" with no loss/reorder/MTU testing.

**Q: UDP has no connection close - how do you avoid leaking dead DTS sessions?** A: A
dead peer just goes silent and "close_notify can be lost, so half-open sessions
accumulate and leak resources. Use app-level keepalives/heartbeats plus idle timeouts to
reap dead sessions, send/expect "close_notify but don't rely on it, bound per-connection
memory, and in DTS 1.3 combine CID with heartbeats. *Follow-up:* Why do keepalives matter
beyond liveness detection? They also hold NAT/firewall pinholes open so the binding
doesn't time out mid-session. *Red flag:* Assuming a TCP-style clean close exists over
UDP.

## Synthesis

DILS-over-UDP traps cluster around the gap between TLS's TCP assumptions and UDP's
realities, The highest-frequency, hardest-to-diagnose are **TU/fragmentation stalling the
handshake** (fix: `SSL_set_mtu`/handshake fragmentation + smaller cert chains) and
**failing to drive the retransmission timer** (fix: `DTLSv1_get_timeout`/`handle_timeout`
in a `select` -with-timeout loop) - both present as a hung handshake. Around them: **server
demux** (`DTLSv1_listen` + per-peer connected sockets or CID), **cookie/DoS handling**
(never disable), **message-oriented read/write** (`WANT_READ`/`WANT_WRITE`, no stream
boundares, no truncation), **app data unreliable by design** (add reliablilty or tolerate
loss), **epoch/replay-window subtleties**, and **connectionless teardown** (heartbeats +
idle reaping). Diagnose primarily with **Wireshark** and reproduce with ***`tc netem`**;
resolve by adopting the datagram mental model, leaning on library built-ins, and testing
under real loss/reorder/MTU rather than on loopback.

## References

- RFC 6347 - DTLS 1.2
- RFC 9147 - DTLS 1,3
- RFC 9146 - DTLS Connection ID
- RFC 8085-UDP Usage Guidelines (congestion control, TU, keepalives for UDP
  applications)
- RFC 8899 - Datagram PLPMTUD
- RFC 4787-NAT behavioral requirements for UP (binding timeouts)
- RFC 8449 - `record_size_limit`
