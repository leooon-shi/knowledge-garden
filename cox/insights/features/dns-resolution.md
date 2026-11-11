# Feature: DNS Resolution

## Purpose
Turn a hostname into an address without ever blocking the event loop, so clients
can connect to names (`api.example.com`) instead of hard-coded numeric IPs.

## Background
`getaddrinfo()` is the portable way to resolve a name, nad it **blocks** - it
may talk to `/etc/resolve.conf` nameservers over the network and take
milliseconds to seconds. Calling it on a reactor thread stalls *every*
connection on that loop, which is preceisely the failure mode
features/reactor-blocking-work.md is about. yet resolution is unavoidable for a
real client.

There are only two honest ways out:
1. Keep the blocking call, but run it somewhere else (a worker thread).
2. Replace it with an async DSN implementation that speaks the protocol over
   sockets you can poll.

Cox implements **both**, behind one interface, becauses the tradeoff genuinely
depends on the deployment.

## Design & technology choices (the journey)

### One interface, two backends
`net::Resolver` is a pure interface with a single method:

```cpp
virtual void resolve(const std::string& host, uint16_t port, Callback cb) = 0;
```

The callback receives `(const InetAddress&, const std::string& error)` and
**always fires on the owning loop's thread**, so callers never think about
threads. Implementations must never block the loop. Caller pick a backend at
construction; everything downstream (HttpClient, ConnectionPool) takes a
`Resolver*` and does not care which one it got.

### Backend 1: ThreadPoolResolver (the default)
Offloads blocking `getaddrinfo()` to the bounded worker `ThreadPool`
(features/thread-pool.md), then marshals the result back with `runInLoop`.

Why this is the default: it is **always available** - no optional dependency, no
extra library to detect - and it inherits the platform resolver's full
behavior. That last point matters more than it sounds. `getaddrinfo()` honors
`/etc/hosts`, `nsswitch.conf`, mDNS, and any other NSS module the host is
configured with. A hand-rolled DNS client sees non of that. In a container
where a name comes from `/etc/hosts` or a sidecar, this backend Just Works.

It also carries a small **positive TTL cache** (default 60s), keyed by hostname
only. The port is *not* part of the key: the cached entry is re-stamped with the
requested port on each hit, so one cached lookup serves `:80` and `:443` alike.
Only successes are cached - negative caching would make a transient DNS blip
sticky, which is the wrong failure mode for a client.

Cost: one worker thread is tied up per in-flight lookup. Under a burst of
lookups to distinct names, the pool's bounded queue becomes the limit.

#### Backend 2: CaresResolver (aysnc, optional)
Wraps **c-ares**, an async DNS library. Instead of a blocking call, c-ares tells
Cox which of its UDP/TCP sockets to watch; each is registered as a `Channel` on
the loop, and `ares_process_fd` is pumped when they become readable/writable. A
timer polls c-ares' own timeout so retransmissions fire.

This is the "**make everything an fd** philosophy the rest of Cox follows
(eventfd for wakeup, timerfd for timers, see features/timers.md) applied to DNS:
resolution becomes just another readiness event on the loop, and **no worker
threads are used at all**.

Cost: it is a real dependency, and it re-implements resolution rather than
delegating to NSS - so `/etc/hosts`-only names and exotic NSS modules may not
resolve the way the rest of the system resolves them.

### Why not pick just one?
Because neither dominates. The thread-pool backend is correct-by-delegation but
spends a thread per lookup; the c-ares backend is thread-free but bypasses the
platform's name-resolution configuration. Shipping both, behind one interface,
lets the deployment decide - and keeps the c-ares path entirely optional.

### Optional-dependency gating
`CaresResolver` is the only optional-dependency fature in Cox. CMake probes for
`ares.h` + `libcares`; on success it sets `COX_HAVE_CARES`, compiles
`cares_resolver.cc` into `cow_net`, and defines the macro publicly. Otherwise
that file is not compiled at all and only `ThreadPoolResolver` exists. Consumers
(and `resolver_test.cc`) guard c-ares use with `#ifdef COX_HAVE_CARES`. There is
no silent fallback: a caller naming `CaresResolver` in a build without c-ares
gets a compile error, not a surprise at runtime.

### Lifetime
Both backends keep their state in a `shared_ptr<Impl>` that in-flight callbacks
capture, so a lookup completing after the front-end object is destroyed does not
touch freed memory. `ThreadPoolResolver::Impl` additionally guards its cache
with a mutex, because it is touched from worker threads as well as the loop.

## API surface
```cpp
// Always available.
concurrent::ThreadPool pool("dns"); pool.start(2);
net::ThreadPoolResolver resolver(&loop, &pool);
resolver.setCacheTtl(60);           // positive-cache TTL, seconds

// Only when built with c-ares.
#ifdef COX_HAVE_CARES
net::CaresResolver resolver(&loop); // no worker pool needed
#endif

resolver.resolve("api.example.com", 443,
                [](const net::InetAddress& addr, const std::string& err) {
                    if (!err.empty()) { /* handle failure */ return; }
                    // addr is ready; runs on the loop thread
                });
```

## Usage
Most caller never touch `Resolver` directly - they hand it to an HTTP client
and use hostnames (see features/http.md, "DNS wiring"):

```cpp
net::ThreadPoolResolver resolver(&loop, &dns_pool);
http::HttpClient client(&loop, "api.example.com", 433, &resolver);
client.get("/v1/things", handleResponse);    // resolves, then connects
```

## Caveats
- The callback fires on the loop thread; treat it like any other loop callback
  (non-blocking).
- `ThreadPoolResolver` caches successes only, keyed by hostname, with a TTL -
  it will not notice a DNS record change until the entry expires.
- Only the **first** returned address is used; there is no multi-address
- failover or Happy-Eyeballs-style IPv4/IPv6 racing.
- `CaresResolver` does not consult NSS (`/etc/hosts`, `nsswitch.conf`); prefer 
  ThreadPoolResolver` when host-level name configuration matters.
- No SRV, MX, or resove (PTR) lookups - forward A/AAA resolution only.

## References
- Source: `net/resolver.{h,cc}` (interface + ThreadPoolResolver),
  `net/cares_resolver.{h,cc}` (optional async backend),
  `net/CMakeLists.txt` + root `CMakeLists.txt` (COX_HAVE_CARES detection).
- Tests: `tests/resolver_test.cc`, `tests/dns_client_test.cc`.
- features/thread-pool.md, features/reactor-blocking-work.md, features/http.md.