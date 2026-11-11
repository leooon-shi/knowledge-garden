# Feature: Production Hardening
## Purpose
The set of defenses that let a Cox server survive real-world conditions: malicious clients, slow peers, resource exhaustion, and abrupt disconnects.
## Background
A server that only passes an echo test is not production-ready. Mature servers (nginx, HAProxy, Envoy) converge on a common set of defenses against slowloris-style holds, slow consumers, connection floods, fd exhaustion, and SIPIPE. Cox implements the TCP-core subset of these.
## Threat taxonomy （what mature 1ibraries defend against）
The traditional attack/abuse and resource-exhaustion cases that nginx, HAProxy, Netty, Envoy, libevent/ehttp, Redis, Go net/http, and Twisted explicitly guard against, by category, with representative mitigations:
### 1. Connection-establishment exhaustion
- **Slowloris / slow-connect** (open many connections, send headers/data one byte at a time to hold resources) *Mitigations:* per-connection idle/read timeout, header/request time budget, minimum data-rate enforcement. nginx client_header_timeout / client_body_timeout; Haproxy timeout http-request /
"timeout client; Netty "ReadtimeoutHandler /IdleStateHandler.
**SYN flood** (half-open connections). *Mitigations:* SYN cookies (kernel),
'net. core. somaxconn / tep_max_syn_backlog tuning. Mostly kernel-level, but the library should expose the listen backlog.
**Accept-storm / thundering herd**
(many connections at once wake multiple
workers). *Mitigations: * SO_REUSEPORT, "EPOLLEXCLUSIVE, accept-loop draining, accept rate limiting.
* *fd exhaustion** (EMFILE"/ ENFILE" wedges the accept loop into a busy spin).
* Mitigations:* the idle-fd reserve trick, "LIMIT_NOFILE tuning, max-connection cap.
### 2. Connection-count and memory limits
- **Max concurrent connections cap** -- unbounded 'accept) exhausts memory/fds.
*Mitigations:* hard cap with graceful reject. nginx "worker_connections;
Redis maxclients; Envoy "max_connections" circuit breaker.
-**Per-connection and global memory budgets** -- thousands of slow connections = 0оM. *Mitigations:* cap read/write buffer sizes, global memory watermark, drop/close when exceeded.

### 3. Slow-reader / write-side backpressure （the dangerous inverse of slowloris）
-   *#Slow-consumer / unbounded write buffer** -- the server generates data faster than a slow client reads it, so the output buffer grows without bound (often worse than slowloris because it is driven by your own output). *Mitigations:* high-water-mark callback + write-buffer cap; pause producing (backpressure), optionally force-close if it keeps growing. muduo HighwaterMarkCallback +"WriteCompleteCallback; Netty Channel. iswritable) + writeBufferwaterMark ;Envoy connection buffer limits + watermark buffers.### 4.Read-side flooding
-   **Unbounded input / large-message Dos** （a huge or never-ending message grousthe input buffer). *Mitigations:* max input buffer / max message size, protocol framing limits. ngin "large client_header buffers"" client_max_body_size.
*  *Pipelining / request flooding** (many requests crammed into one connection).
*  Mitigations:* per-connection request rate limits, max in-flight requests.
### 5.Timeouts （the backbone of Dos resistance）
A hierarchy of time-based limits, all built on timers (the "TimerQueue): idle timeout, read/request timeout (budget to receive a complete request), write timeout (budget to flush to a slow client), connection lifetime / max age, and TLS handshake timeout. HAProxy's timeout matrix (connect, client, server, http-request, http-keep-alive, tunnel) is the canonical reference.
### 6.Graceful degradation & lifecycle
I
- **Graceful shutdown / draining**
-stop accepting, let in-flight finish, then
close. *Mitigations: * drain mode, "SO_LINGER" policy, close notification.
*  *TIME_WAIT accumulation** from many short connections. *Mitigations:*
*  SO_REUSEADDR, keep-alive/connection reuse, tuning.
*  *RST / abrupt close** -- handle "ECONNRESET, "EPIPE" (SIGPIPE!) without crashing. *Mitigations:* ignore SIGPIPE globally (a write to a closed socket raises SIGPIPE and kills the process by default), treat EPIPE / ECONRESET as a normal close.
### 7. Correctness under partial I/O (prerequisites, not attacks)
Short reads/writes (never assume one syscall moves everything), "EINTR"/ EAGAIN (retry vs defer correctly), half-close semantics ( shutdown (SHUT_WR)" vs full close), and Nagle vs latency (TCP_NODELAY").
### 8. Protocol / application layer (a layer above raw TCP)

### 8. Protocol / application layer （a layer above raw TCP）
Malformed/oversized frames, decompression bombs, header injection, HTTP request smuggling, TS renegotiation attacks. These belong to the protocol codecs (HTTP/TLS), not the TCP core -- but the TCP layer must give them the hooks (size limits, timeouts, backpressure) to enforce policy.
## Priority (risk-to-reward ordering)
When sequencing a hardening pass, the ROI order the popular libraries converge on is:
* *Ignore SIGPIPE** -- a one-liner that prevents a trivial remote process
* *Idle/read/write timeouts** -- directly defeat slowloris and slow-write;
* *Output high-water-mark + write-complete callbacks** -- defeat slow-consumer
* *Max-connections cap + max-input-buffer** -- bound memory/fd exhaustion.
Protocol-level limits (request smuggling, framing) land with the HTTP/TLS codecs, not the TCP core.
## Design & technology choices （the journey）
I
### SIGPIPE ignored (critical, one-time)
writing to a socket whose peer closed the read end raises SIPIPE, whose default disposition kills the process - a remote peer could crash the server by closing at the wrong moment. initNetRuntime() installs "SIG_IGN' (idempotent, invoked from Acceptor construction), turning such writes into a normal EPIPE the I/O
Layer already handles.
### Idle timeout （defeats slowloris）
setIdleTimeout(seconds) : a connection with no inbound data for N seconds is force-closed. Reset on every read. Defeats attackers that hold connections open while sending little/nothing. Built on the TimerQueue.
#** Write timeout (defeats slow consumers)
• setwriteTimeout(seconds): if buffered output cannot drain within Nseconds (peer not reading), force-close. Armed when output backs up, cancelled when it 
drains. Bounds how long a slow reader ties up resources.
### Output high-water-mark (egress backpressure)
*sethighwaterMarkCallback(cb, bytes)": fires once when queued output crosses the threshold, so the application can stop producing. Prevents a slow consumer from growing the output buffer toward ooM. "WriteCompleteCallback" signals drain (Low-water).
### Ingress backpressure (stop/resume read)
stopRead() / startRead(): disabling read interest lets the kernel receive buffer fill, shrinking the TCP window to zero and throttling the *peer's* writes without dropping data - TCP flow control as a lever. The ingress mirror of the high-water-mark.
### Max input buffer （read-side memory bound）
• setMaxInputBufferSize(bytes) : checked after the message callback; if data still exceeds the cap (flood or oversized/unframed message), force-close. Bounds read-side memory.
### Max connections cap
*setMaxConnections(n): connections beyond the cap are accepted-then-closed immediately (keeps the accept queue moving, bounds fd/memory).
### Connection max-age
*setMaxConnectionage(seconds): force-close after a lifetime regardless of activity, to recycle long-lived connections.
### EMFILE idle-fd guard
on fd exhaustion during accept, a reserved /dev/null fd is closed so the pending connection can be accepted+closed (signaling the peer), then reopened - avoiding an accept busy-loop. (In the Acceptor.)
## API surface (Tepserver applies to all connections)
```cpp
server. setIdleTimeout (60); server. setwriteTimeout (30);
server. setmaxInputBufferSize(1<<20); server. setMaxConnectionAge (3600);
server. setMaxConnections (10000);
// per-connection:
conn->setHighwaterMarkCallback(cb, 64<<20); conn-›stopRead(); conn-›startRead();
```
## Caveats
- Timeouts use whole-second granularity.
- Max-input-buffer fires only if the app leaves data unconsumed beyond the cap; precise per-message limits belong in a protocol codec.
- These bound damage, and are now **validated under adversarial load** (2026-11-03)via the load-validation scenarios S1-S8(docs/performance/hardening-validation.md"): each defense was driven by a matching attack (slowloris, flood, slow-read, unframed flood, big-body, max-age soak, heavy in-flight, fd exhaustion) and its CONTAINMENT (bounded connections/memory/fds/in-flight, via "/metrics" + /proc" + "ss") and LIVENESS (a legitimate probe still served) measured, not asserted. A reduced, non-blocking CI smoke tier (scripts/hardening_smoke.sh") guards the containment invariants. Findings that surfaced while validating are recorded in the scenario doc and the roadmap (eg. the global accept-rate limiter is blind to legitimacy under flood - containment holds, per-client fairness does not; the write timeout is a one-shot; close reasons are now metric labels).
## References
- Source: net/net_runtime.*-
,"net/tcp_connection.*,
"net/acceptor.*,
1 *net/tcp_server.**
- features/flow-and-overload-control.md (rate limiting / shedding).

