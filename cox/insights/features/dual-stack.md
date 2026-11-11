# Dual-Stack (IPv4/IPv6) Connectivity

## Problem

Cox is meant to run in environments where a single host name can resolve to
both an IPv4(`A`) and an IPv6 (`AAAA`) address -- most notably a kubernetes
cluster in dual-stack mode (`ipFamilyPolicy:PreferDualStack` /
`RequireDualStack`), where a Service or Pod can carry an address of each family.
In those environments the *reachable* family is not fixed: during rollouts,
partial dual-stack rollout, or when IPv6 is disabled on a particular node, only
one of the two addresses may actually accept a connection at any moment.

Two independent concerns fall under "dual-stack":

1. **Server side** -- Can a Cox server accept connections of both families?
2. **Client side** -- When a name resolves to serveral address, does a Cox
   client reach a working one?

### The server side already works

The socket layer is family-parameterized by design:
`sockets::createNonBlockingOrDie(sa_family_t family)` takes the family
explicitly, and nothing in `Socket`/`Acceptor` forces `IPV6_ONLY`. Binding
the IPv6 wildcard (`::`) therefore yields a dual-stack listener under the Linux
default `IPV6_V6ONLY=0` (`/proc/sys/net/ipv6/bindv6only == 0`): it accepts both
native IPv6 and IPv4-mapped (`::fff:a:.b.c.d`) connections on one fd. No new
capability is required; at most a convenience default. See "Server binding
models" below for the portability caveat.

### The client side is the real gap

The failure that motivated this work: a client resolving `localhost` on an
IPv6-first host (including the CI runners) received `::1` as the first
`getaddrinfo` result, committed to it, and --because the test server was bound
IPv4-only -- got `ECONNREFUSED` and retried `[::1]` forever with exponential
backoff. The suite timed out even though a working address (`127.0.0.1`) was
present in the resolver's result list, one entry further down.

The root cause is in two layers:

- `net::ThreadPoolResolver` (`net/resolver.cc`): `resolveBlocking()` calls
  `getaddrinfo(AF_UNSPEC)` -- which returns a list already sorted per RFC 6274
  (IPv6-first on a v6-capable host) -- and then **keeps only `res`, the first
  entry**, discarding the rest.
- `net::Connector` (`net/Connector.cc`): it holds a single `const InetAddress
  server_addr_` and, on failure, calls `retry()` which re-attempts *the same
  address* after a jiterred backoff. THere is no notion of "try the next
  address."

So a client can never fail over from an unreachable address to a reachable one.
That is the behavior this feature adds.

## Goals

- A client given a host name that resolves to multiple addresses connects
  successfully if **any** of them is reachable, regardless of family ordering.
- Failover between addresses is fast (sub-second), not gated on the
  reconnection backoff that is meant for a *down* endpoint.
- No change to the single-address fast path: numeric origins and
  single-result names behave exactly as before, including existing
  retry/backoff semantics for a genuinely unavailable server.
- Preserve the connector's hard-won lifetime correctness (the weak-capture /
  deferred-channel-reset invariants that ASan/TSan enforce).

## Non-goals

- Full RFC 8305 "Happy Eyeballs" *racing* (starting a second attempt after a
  ~250 ms head-start timer and taking whichever connects first). We adopt the
  simper **sequential** fallback first; the design leaves room to add racing
  later without changing the public surface. Rationale in "Alternatives."
- Re-sorting or re-prioritzing addresses beyond the order the resolver
  returns. We trust the OS's RFC 6724 ordering.
- UDP. This is about `connect()`-oriented stream sockets.

## Design

### 1. Resolver returns the full address list

Add a list-returning path to `net::Resolver` alongside the existing
single-address `Callback`, rather then replacing it (the current callback is
part of the public API and is used by `ConnectionPool` today).

```cpp
// net/resolver.h
class Resolver {
public:
    // Existing: (address, error). Retained for source compatibility; it now
    // delegates to the list form and hands back  the first address.
    using Callback =
        std::function<void(const InetAddress& addr, const std::string& error)>;

    // New: the full, OS-ordered candidate list. On success `error` is empty
    // ad `addrs` is non-empty; on failure `addrs` is empty.
    using ListCallback =
        std::function<void(const std::vector<InetAddress>& addrs,
                           const std::string& error)>;

    virtual void resolve(const std::string& host, uint16_t port,
                         Callback cb) = 0;
    virtual void resolveAll(const std::string& host, uint16_t port,
                            ListCallback cb) = 0;
};
```

`resolveBlocking()` changes to walk the whole `getaddrinfo` result chain
(`ai_next`) and collect every `AF_INET` / `AF_INET6` entry into a
`std::vector<InetAddress>`, preserving order. The positive cache in
`ThreadPoolResolver::Imp` becomes keyed `host -> vector<InetAddress>` (ports
are re-stamped per request as today via `withPort`). `resolve()` is implemented
in terms of `resolveAll()`:

```cpp
void ThreadPoolResolver::resolve(host, port, cb) {
    resolveAll(host, port, [cb](const auto& addrs, const auto& err) {
        if (err.empty()) cb(addrs.front(), "");
        else             cb(InetAddress(), err);
    });
}
```

`CaresResolver` (the optional c-ares backend, `COX_HAVE_CARES`) gains the same
`resolveAll()` -- c-ares already yields a list, so this is natural there.

### 2. Connector accepts an ordered address list

The Connector becomes the owner of the fallback loop, because it already owns
the connect attempt, the `SO_ERROR` check, and the retry/backoff timer.

```cpp
// net/connector.h -- additive
class Connector {
public:
    // Existing single-address ctor retained (delegates to the list form).
    Connector(reactor::EventLoop* loop, const InetAddress& server_addr);
    // New: ordered candidates, tried in order on connect failure.
    Connector(reactor::EventLoop* loop, std::vector<InetAddress> candidates);
    ...
private:
    std::vector<InetAddress> candidates_;   // size >= 1
    size_t current_{0};                     // index being attempted
};
```

Behavioral change, confined to failure handling:

- `connect()` uses `candidates_[current_]` instead of a single
  `server_addr_`, `serverAddress()` returns the current candidate (so logs and
  `TcpClient` reconnection messages stay meaningful).
- On a failed attempt (the `retry()` path, reached from `handleWrite()`'s
  `SO_ERROR != 0`, `handleError()`, and the synchronous error cases in
  `connect()`):
  - **If there is a next candidate** (`current_ + 1 < candidates_.size()`): 
    advance `current_` and attempt it **immediately**, with no backoff delay.
    This is the Happy-Eyeballs failover -- `[::1]` refused → try `127.0.0.1`
    now, not in 500 ms.
  - **If the list is exhaused**: reset `current_ = 0`, advance the jittered
    exponential backoff (keyed by the host, not a single address -- see below),
    and schedule a retry of the *whole list* from the top. This preserves the
    existing "server is down, keep trying with backoff" semantics for the
    reconnect case.
- On success nothing changes except that we remember which candidate won is not
  necessary -- keep-alive/reconnect simply restarts the list.

A bounded **per-attempt connect timeout** is added so a candidate that is
silently dropped (SYN blackholed, common when a route to one family is null)
does not stall the whole list. A single-shot timer (default ~1 s, configurable)
fires if a `kConnecting` attempt neither completes nor errors; on expiry we
treat it as a failure and advance to the next candidate. This timer reuses the
`EventLoop::runAfter` mechanism already used for `retry_timer_` and is cancelled
in `stopInLoop()` alongside it.

#### Backoff keying

`Backoff` is per-key(`net/connector.cc` currently keys it by
`server_addr_.toIpPort()`). With multiple candidates we key by the **host
identity** (the name, or the first candidate's IP for numeric origins) so that
cycling through `::1` then `127.0.0.1` is *one* logical failure, not two. This
keeps backoff growth tied to "the endpoint is down," not to list length.

### 3. Wiring through ConectionPool

`ConnectionPool::Impl` already resolves once on first request and stores a
single `origin_`. The change:

- Call `resolver_->resolveAll()` instead of `resolve()`.
- Store `std::vector<InetAddress> origins_` instead of one `orgin_`.
- `PooledConn` constructs its `TcpClient`/`Connector` from the list; every
  pooled connection to this origin shares the same candidate ordering.
- `onResolved` dispatches the queued requests once the list is known (unchanged
  except it now checks for a non-empty list rather than a single address).

`MultiOriginPool` needs no change: it delegates to a per-origin
`ConnectionPool`, which now carries the list internally.

`HttpClient` needs no surface change -- it is a single-connection
`ConnectionPool`.

### Control flow (client, dual-stack name)

```
request("svc", 8080)
    -> ConnectionPool: resolveAll -> [::1]:8080, 127.0.0.1:8080         (RFC 6274 order)
    -> Connector(candidates=[ [::1]:8080, 127.0.0.1:8080 ])
       attempt current = 0  [::1]:8080
          SO_ERROR=ECONNREFUSED
          next candidate exists -> current=1, connect NOW (no backoff)
       attempt current=1  127.0.0.1:8080
          connected -> newConnectionCallback(fd)
```

```
(server genuinely down: both refused)
      attempt [::1] -> refused -> try 127.0.0.1 immediately
      attempt 127.0.0.1 -> refused -> list exhausted
         backoff.nextDelay(host) -> 543 ms
         runAfter(543ms): current=0, retry whole list
```

## Server binding models (for completeness)

Two ways a server can be dual-stack:

- **Single `::` socket, `IPV6_V6ONLY=0`** (Linux default). One fd, accepts both
  families via IPv4-mapped addresses. Simplest; this is what the DSN tests use.
  Caveat: *not portable* -- some hardened hosts and the BSDs default
  `bindv6only=1`, and IPv4-mapped addresses can interact awkwardly with
  firewalling that only matches native v4.
- **Two sockets: one `AF_INET`, one `AF_INET6` with `IPV6_V6ONLY=1`.** Explicit,
  portable, each family observable independently. Requires the acceptor to own
  two listen fds.

This feature does not force either; the capability already exists. If a
first-class "dual-stack server" convenience is wanted later, the two-socket
model is the portable choice and should be a separate, small addition to
`Acceptor`/`TcpServer`.

## Alternative considered

- **Sequential fallback (chosen) vs. full Happy Byeballs racing.** Racing
  (RFC 8305: start the second family after a ~250 ms timer, take the first to
  connect) lowers tail latency when the first family is slow rather than
  broken. It also doubles in-flight sockets and complicates the connector's
  state machine and its ASan/TSan-verified lifetime handling. Sequential
  failover fixes the correctness problem (reach a working address) with minimal
  state. The list-based interface is deliberately the same one racing would
  need, so racing can be layered on later as a pure connector-internal change.
- **Fix only the resolver (prefer a family).** Picking IPv4 (or IPv6) first in
  `resolveBlocking` would paper over the symptom on the CI host but still fails
  whenever the preferred family is the unreachable one -- exactly the K8s
  rollout case. It also fights the OS's RFC 6724 ordring. Rejected.
- **Fix only in the test (dual-stack bind).** Already applied to
  `tests/dns_client_test.cc` to unblock CI, but it does not make a *client*
  robust against multi-address names. This feature is the product-level fix.
- **Host configuration (`/etc/gai.conf`, `/etc/hosts`).** Not portable, not
  controllable on managed/runners or in-cluster, and does not survive image
  changes. Rejected as a fix.

## Backward compatibility

- `Resolver::Callback` and the single-address `Connector`/`ConnectionPool`
  constructors remain and keep their semantics (first address; same
  retry/backoff for a single endpoint).
- Numeric origins skip resolution entirely and produce a one-element candidate
  list -- identical behavior to today, including immediate `resolved()`.
- Any custom `Resolver` subclass must implement the new `resolveAll()`. Since
  `Resolver` is an in-tree interface with two implementations, this is a
  contained change; `resolve()` can be given a default implementations in terms
  of `resolveAll()` to ease external subClasses.

## Testing

- **Resolver:** `resolveAll("localhost")` returns a non-empty, order-preserving
  list; the single-address `resolve()` still returns the first entry; failure
  yields an empty list and a non-empty error. Cache stores and replays the lsit
  with per-request port re-stamping.
- **Connector failover (the core case):** bind a server on **one** family only
  (e.g. IPv4 `127.0.0.1`), hand the connector a list `[ ::1, 127.0.0.1 ]`, and
  assert it connects -- and that it did so without waiting a backoff interval
  (failover is immediate). Symetrically with an IPv6-only server and the list
  reversed.
- **Per-attempt timeout:** point the first candidate at a blackholed address
  (e.g. a route that drops SYNs) and assert failover to the second within the
  timeout rather than hanging.
- **Exhaustion / backoff:** no listener on either address -> connector cycles
  the whole list and backs off (keyed once per host), and `stop` cancels both
  the retry timer and the per-attempt timer cleanly.
- **Pool/HttpClient end-to-end:** the existing `DnsClientTest` cases, plus a
  variant where the server is bound to a *single* family, to exercise real
  failover rather than relying on a dual-stack listener.
- **Sanitizers:** the new timer and the list storage must be clean under
  ASan/LSan and TSan, matching the connector's existing invariants (no cycles,
  deferred channel reset via `queueInLoop`).

## Rollout / files touched

- `net/resolver.h`, `net/resolver.cc` -- `resolveAll()`, list cache.
- `net/cares_resolver.*` (when `COX_HAVE_CARES`) -- `resolveAll()`.
- `net/connector.h`, `net/connector.cc` -- candidate list, immediate failover,
  per-attempt timeout, host-keyed backoff.
- `net/net_client.*` -- list-accepting constructor (delegating)
- `http/connection_pool.cc` -- `resolveAll()`, `origins_` list, pass-througth.
- Tests as above; `docs/user-guide.md` section 9 gains a note that a hostname
  resolving to multiple addresses is tried in order until one connects.
