# Feature: Flow Control & Overload Control

## Purpose
Govern *how fast* work is admitted (rate) and *how much* concurrent work exists
(adimission), and shed or slow load early when overwhelmed - so the server
degrades gracefully instead of collapsing.

## Background
Under overload, a server must choose a response: reject early, apply
backpressure, block, or notify. Blocking the loop is unacceptable; the mature
pattern (nginx/Envoy/Netty) is reject-early + backpressure + hoooks, with policy
owned by the application. Cox provides the mechanisms.

Design deep-dive (algorithms, data structures, rate math, abnormal cases):
features/flow-controller-design.md

## Design & technology choices (the journey)

### Token bucket for rate limiting
`TokenBucket` (client-go RateLimiter style): tokens accue at `qps` up to
`burst`; each admission consumes one. Chosen over fixed/sliding windows and
leaky-bucket because it gives a bounded average rate WITH controlled bursting at
O(1) memory/time, using lazy refill (no timer).

### Per-key jittered backoff
`Backoff` (client-go Backoff style): per-key exponential growth, capped, reset
on success, with **jitter** to prevent a thundering herd when many clients
reconnect in lockstep (e.g. after a server restart). Wired into Connector,
replacing an earlier un-jittered backoff.

### Concurrency limiter (in-flight bound)
`ConcurrencyLimiter`: a lock-free counter with an RAII `Permit`. Connection
count is a poor load proxy (idle keep-alives are cheap; a few heavy in-flight
requests are not), so this bounds concurrent *work* directly. `tryAcquire`
returns a falsy permit when at capacity - the app sheds.

### Overload response: reject-by-default, never block the loop
- **Accept-rate shedding**: `TcpServer::setAcceptRateLimit(qps, burst)` sheds
  excess connections early (accept-then-close).
- **Overload notification**: `setOverloadCallback(peer)` lets the app record
  metrics / shape a protocol-level rejection (Cox can't write a 503 at the TCP
  layer - that's the app's job).
- **Backpressure** where the transport allows (ingress stopRead / egress
  high-water-mark).
- **Blocking** only ever off-loop (the worker ThreadPool), never on the loop.

This mirrors how libraries behave: reject early with a signal, backpressure via
TCP/queue, and deletegate policy via hooks.

### Composition with the thread pool
The bounded worker-pool queue's "full" condition (`tryRun` false) is another
admission signal, complementing the token bucket and concurrency limiter.

## API surface
```cpp
flowcontrol::TokenBucket tb(qps, burst);    tb.tryAcquire();
flowcontrol::Backoff bo(initial, max);      bo.nextDelay();
flowcontrol::ConcurrencyLimiter cl(max);
    if (auto p = cl.tryAcquire()) { /* admitted */} else { /* shed */}

server.setAcceptRateLimit(qps, burst);
server.setOverloadCallback([](const InetAddress& peer) {/* metric */});
```

## Usage (request-level admission in a handler)
```cpp
server.setMessageCallback([&cl](const TcpConnectionPtr&, Buffer* buf) {
    if (auto permit = cl.tryAcquire()) {
        handle(conn, buf);              // permit releases on scope exit
    } else {
        conn->send(busyResponse());     // shed early with a cheap reply
    }
});
```

## Caveats
- Limits are only tunable with measurement; observability is deferred (hooks
  exist). Start with static limits; adaptive/priority shedding is future work.
- Primitives use coarse single-mutex locking; per-loop sharding or lock-free
  variants are a future optimiazation if profiling demands.

## References
- Source: `flowcontrol/token_bucket.*`, `flowcontrol/backoff.*`,
  `flowcontrol/concurrency_limiter.h`, `net/tcp_server.*` (accept limit /
  overload cb), `net/tcp_connection.*` (backpressure).
- features/flow-controller-design.md, features/reactor-blocking-work.md.