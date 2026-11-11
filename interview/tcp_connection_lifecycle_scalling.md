<!-- interview/tcp_connection_lifecycle_scaling-md -->

# TCP Connection Lifecycle at Scale: Accept Queues, TIME_WAIT, Port Exhaustion, S0_REUSEPORT, Backpressure

Complements `close wait_surge.md ` (the passive-close side) and `epoll_edge_triggered.md`.
Linux-specific; values checked on the study host (kernel 6.8): `somaxconn=4096`,
`tcp_max_syn_backlog=2848`, `ip_local port_range=32768 60999`, `tep_tw_reuse=2`.

## 1. The state machine in one picture

```
Active opener (client)                      Passive opener (server)
 CLOSED --SYN--> SYN_SENT                    LISTEN --SYN--> SYN_RECV (SYN queue)
          <--SYN/ACK--                       --SYN/ACK-->
 ESTABLISHED --ACK-->                        ESTABLISHED (accept queue) -> accept()

Active closer                               Passive closer
 FIN_WAIT_1 --FIN-->                         CLOSE_WAIT (until the app calls close())
 FIN_WATT_2 <--ACK--                         LAST_ACK --FIN-->
 TINE_MAIT  <--FIN--, --ACK-->               CLOSED <--ACK--
 (2*MSL) -> CLOSED
```

- **Whoever closes first gets TIME_WAIT.** The side that receives FIN first gets
  CLOSE_MAIT (and stays there until the application closes - see `close walt_surge.md`).
- `FIN_WAIT_2` for an orphaned socket (app already closed) times out after
  `tcp_fin_timeout` (69 s default). It's *not* the TIME_WAIT duration, despite the common
  misreading.

## 2. Accept path: SYN queue and accept queue

- **SYN queue** (half-open, `SYN_RECV`): bounded by `tcp_max_syn_backlog`. Under SVN
  flood, **SYN cookies** (`tcp_syncookies=1`) encode state in the ISN so no queue entry is
  needed (at the cost of some TCP options in older kernels).
- **Accept queue** (fully established, waiting for `accept()`): length =
  `min(backlog arg of listen(), net.core.somaxconn)`. Default `somaxconn` was 128 until
  kernel 5.4, now 4896.
- **When the accept queue is full**, Linux by default **drops the final ACK** (client
  thinks it's connected; server retransmits SYN/ACK later). Clients see latency spikes or
  timeouts at the first request. `tcp_abort_on_overflow=1` sends RST instead (fail fast).
- **Diagnose:** `ss -lnt` for listening sockets `Recv-Q` - current accept-queue length,
  `Send-Q` = its maximum. `nstat -az TcpExtListenOverflows TcpExtListenDrops` count
  overflows. Slow `accept()` loops (single-threaded acceptor, accepting only one per epoll
  wakeup in ET mode) are the usual cause.

## 3. TIME_WAIT

## Why it exists

1. **Reliable close:** if the final ACk is lost, the peer retransmits its FIN; the
   TIME MAIT socket can re-ACk it instead of answering with RST.
2. **Prevent old duplicates:** delayed segments from the old connection must not be
   accepted by a new connection using the same 4-tuple. Waiting 2xMSL lets them expire.

Linux uses a fixed **60 s** ("TCP_TIMEWAIT_LEN), not configurable via sysctl. A TIME_WAIT
socket is a small `inet_timewait_sock` (~a few hundred bytes), not a full socket; the
system cap is tcp max_tw buckets" (excess are destroyed immediately with a log warning).

### When it hurts

- **It's a 4-tuple problem, not a socket-count problem.** Tens of thousands of TIME_WAIT
  entries on a server are usually harmless.
- It hurts the **client side making many short connections to the same destination**
  (src IP, dst IP, dst port® fixed): only the ephemeral port varies. With 28,232 ports
  (32768-68999) and 60s TIME_MAIT, the ceiling is **~470 new connections/s per
  destination** before `connect()` fails with `EADDRNOTAVAIL`. Typical victims: a service
  calling a database or a proxy calling one upstream without keep-alive, load-testing
  tools, NAT gateways.

### Fixes (best first)

1. **Reuse connections**: HTTP keep-alive, connection pooling, HTTP/2 / gRPC multiplexing.
   Removes the problem at the source.
2. **Let the server close** where protocol allows, so TIME_WAIT lands on the side with a
   fixed listening port (server-side TIME_WAIT doesn't consume ephemeral ports).
3. **`net.ipv4.tcp_tw_reuse`**; allows outgoing* connections to reuse a TIME_WAIT 4-tuple
   if TCP timestamps make it safe (new timestamp > last seen), Values: 0 off, 1 global,
   **2 = loopback only (the default since 4,x)**, Setting 1 is a common, generally safe
   fix for client-heavy hosts.
4. **Widen the port range**(`ip_local_port_range = 1024 65535`, minding reserved ports
   via `ip_local_reserved_ports`) and **add source IPs** / destination ports to multiply
   the 4-tuple space, `IP_BIND_ADDRESS_NO_PORT` + `bind()` to a source IP lets the kernel
   choose ports per 4-tuple rather than per local address.
5. **`SO_LINGER` with timeout 0**-> close sends RST and skips TIME_WAIT. Loses in-flight
   data and the protections above; a last resort.

- ***`tcp_tw_recycle`** - removed in kernel 4.12 because it broke clients behind NAT
  (per-host timestamps aren't monotonic across hosts). Interviewers st1l1 ask about it.
- **`SO_REUSEADDR`** is a different thing: lets a *server* `bind()` Its listening port
  while old connections on that port are in TIME_WAIT (restart without Address already in
  use"). Set it on every server.

### Diagnose

```bash
ss -tan state time-wait | wc -l
ss -tan state time-wait ( dport = :5432 ) | wc -l   # per destination
ss -s                                               # summary incl. timewait
nstat -az | grep -i -E 'TW|PortsExhausted'
```

## 4. SO_REUSEPORT

Lets **multiple sockets bind the same IP:port** (same effective UID). For TCP listeners,
the kernel **hashes each incoming connection's 4-tuple** to pick one listener; for UDP,
each datagram's 4-tuple.

Why it matters:

- **Per-thread /per-process listeners**, each with its own accept queue and epoll
  instance - no shared accept lock, no thundering herd, better cache locality. Nginx
  (`reuseport`), Envoy, HAProxy, and most high-performance servers use it. Alternative to
  a single listener with `EPOLLEXCLUSIVE`.
- **UDP scaling** (QUIC servers, DNS): one socket per core instead of one hot socket.
- **Zero-downtime restarts**: start the new process listening on the same port, then drain
  the old one. Caveat: closing a listener drops connections still in its accept queue -
  drain with `SO_INCOMING_CPU`-aware handoff, `tcp_migrate_req` (5.14+), or hand over the
  listening FD (`SCM_RIGHTS`) instead.

Pitfalls:

- **Hash-based, not load-aware.** A blocked worker still receives its share of new
  connections, which sit in its queue. Long-lived connections can skew load.
- Group membership changes (a worker starts/stops) rehash, breaking UP flow affinity -
  QUIC needs connection-ID-based routing (eBPF) to survive this.
- **Steering with eBPF:** `SO_ATTACH_REUSEPORT_EBPF` / `BPF_PROG_TYPE_SK_REUSEPORT` choose
  the socket programmatically (by CPU, connection ID, or app-level load).
  `SO_INCOMING_CPU` aligns sockets with the RX queue's CPU for full locality with RSS/RFS.
- Security: any process with the same UID can join the group and steal traffic.

## 5. Backpressure and flow control

**Backpressure** = slowing producers when consumers can't keep up, instead of buffering
without Limit.

- **TCP does it for you, if you let it:** the receiver's window shrinks as its socket
  buffer fills; the sender's `send()` blocks or returns `EAGAIN` when the send buffer
  fills. A server that keeps reading from clients while its downstream is slow, and queues
  in user space, **breaks the chain** and runs out of memory.
- **Rules for event-loop servers:**
  - Bound every queue (per-connection output buffer, work queues, channels). When a
    connection's output buffer exceeds a high-water mark, **stop reading** from the
    upstream that feeds it (remove `EPOLLIN`), resume at a low-water mark.
  - Propagate across hops: a proxy should stop reading from the client when the upstream
    write buffer is full (Envoy/nginx buffer limits; Node.js `stream.pipe` /
    `highwaterMark`).
  - Use `TCP_NOTSENT_LOWAT` to keep the kernel send queue short (latency-sensitive
    streaming, HTTP/2 prioritization).
- **When you can't slow the producer:** **load shedding** - reject early with a cheap
  error (HTTP 429/503, gRPC *RESOURCE_EXHAUSTED) based on queue length or queueing delay
  (CoDel-style "time in queue > target"), rather than letting latency grow for everyone.
  Prioritize by request class.
- **Credit-based flow control:** receivers grant credits (HTTP/2 and QUIC
  `WINDOW UPDATE`/`MAX_DATA`, gRPC streaming, Reactive Streams `request(n)`, AMQP
  prefetch). Per-stream *and* connection-level windows prevent one stream from starving
  others.
- **Retry storms:** retries amplify overload. Use exponential backoff with jitter, retry
  budgets (e.g. retries ≤ 10% of requests), and circuit breakers.
- **Little's law** for sizing: `concurrency = throughput × latency`. If latency doubles
  under load and concurrency limits are fixed, throughput halves - adaptive concurrency
  Limits (Netflix ^ `concurrency-limits`, Envoy adaptive concurrency) adjust dynamically.

# Likely interview questions

- *"Your server has 50k TIME_WAIT sockets - is that a problem?"* - Usually not; it's a
  client-side 4-tuple/ephemeral-port issue.
- *"Clients get `EADDRNOTAVAIL` on connect - why and what do you do?"* - Port exhaustion
  to one destination; pool connections, `tcp_tw_reuse=1`, widen range, more source IPs.
- *"Why does TIME WAIT exist? Why not shorten it?"*
- *"What happened to `tp_tw_recycle`?"* - Removed (NAT breakage).
- *"`SO_REUSEADDR` vs `SO_REUSEPORT`?"*
- *"Clients see 1 s or 3 s connect delays under Load; the server looks idle."* -
  Accept-queue overflow dropping the ACK / SYN retransmits; check `ListenOverflows`, slow
  accept 1oop.
- *"How do you implement backpressure in an epoll proxy?"* - Bounded buffers, stop reading
  upstream at high-water mark.
- *"How does `SO_REUSEPORT` distribute connections and what's the downside?"* - 4-tuple
  hash; not load-aware.

## Synthesis

The side that closes first holds **TIME_WAIT** for 60 s on Linux, to ACK retransmitted
FINs and keep stale segments away from a reused 4-tuple, It rarely hurts servers; it hurts
**Clients opening many short connections to one destination**, where the ephemeral range
caps the new-connection rate (~470/s with defaults), Fix it with connection reuse first,
then `tcp_tw_reuse`, wider port ranges and more source IPs; `tcp_tw_recycle` is gone. On
the accept side, overflowing the **accept queue** (`min(backlog, somaxconn)`) silently
drops ACKs and causes connect-time latency; watch `ListenOverflows`. **SO_REUSEPORT**
shards a port across per-core listeners by 4-tuple hash removing accept contention and
enabling restarts - but it isn't load-aware without eBPF steering. Systems stay stable
under overload through **backpressure**: bounded buffers, stop-reading at high-water
marks, credit-based flow control, and early **load shedding** instead of unbounded queues.
