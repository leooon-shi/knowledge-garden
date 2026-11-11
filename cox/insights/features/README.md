
‹!-- cox/docs/insights/features/README.md -->
# Cox Feature & Design Docs
Each document explains one feature: its purpose, the background/problem it solves, the design and technology choices made (the journey, including alternatives considered and rejected), the API, usage, and caveats. Together they record *why* Cox is built the way it is.
## Document template
Every feature doc follows the same section order, so a reader always knows where to look:
1. **Purpose** - what the feature is, in one or two sentences.
**Background / problem** - the problem it solves and the relevant context.
1. **Design & technology choices (the journey)** - the decisions made, including alternatives considered and why they were rejected.
2. **API surface** - the public types and functions.
**Usage** - a short, runnable-style example.
1. **Caveats** - limitations and sharp edges.
2. **References** - source files and related docs.
The split is deliberate: these docs capture the *why* (design journey); the roadmap tracks *status*, the troubleshooting docs record *diagnosed defects*, and the user guide covers *how to use*.
I
For usage-oriented instructions, see "docs/user-guide.md".
For project status and deferred/discarded items, see docs/insights/roadmap.md.
For worked diagnoses of real defects, see docs/insights/troubleshooting/.
## Core
- [reactor-eventloop. md](reactor-eventloop.md) - event loop, Channel, Poller, eventfd wakeup, when queueInLoop must wake the loop
- [poll-timeout-strategy.md](poll-timeout-strategy.md) - poll(-1) vs bounded
I timeout, build-mode policy
- [timers.md](timers.md) - timerfd-based TimerQueue
- [threading-model.md](threading-model.md) - one loop per thread,EventLoopThreadPool
## Concurrency & work
-[thread-pool.md](thread-pool.md) - bounded worker pool for blocking work
- [reactor-blocking-work.md](reactor-blocking-work.md) - the blocking-in-a-reactor problem and full solution space
## Data & addressing
- [buffer. md](buffer.md) - growable application buffer
- [addressing-md](addressing.md) - InetAddress (IPV4/IPV6/Unix)
# Networking
- [tcp-layer.md](tcp-layer.md) - sockets, Acceptor/Connector, Tepconnection,TepServer/Client
- [dns-resolution.md](dns-resolution.md) - async Resolver, thread-pool and c-ares backends
- [production-hardening.md](production-hardening.md) - SIGPIPE, timeouts, backpressure, caps
- [flow-and-overload-control.md](flow-and-overload-control.md) - rate limiting, concurrency limiting, shedding
- [flow-controller-design.md](flow-controller-design.md) - detailed flow-control design (algorithms, math, edge cases)
- [graceful-shutdown.md](graceful-shutdown.md) - drain sequence
## Design proposals (not yet implemented)
- [metrics-design.md](metrics-design.md) - metrics/observability module
Counter/Gauge/Histogram + Registry + Prometheus exposition), P1
- [load-validation-design.md](load-validation-design.md) - adversarial load validation plan for the hardening features, P1
## Protocols
- [http.md](http.md) - HTTP/1.1 + REST: parsers, HttpServer/HTTPS, Router, URLand query helpers, Httpclient, connection pooling
- [http2-design.md](http2-design.md) - HTTP/2 via nghttp2 (server, client,
GOAWAY lifecycle): design and implementation status
- [http-streaming-design.md](http-streaming-design.md) - async handlers and streamed bodies ("ServerStream"), HTTP/1.1 and HTTP/2
## Security & operations
- [tls.md](tls.md) - TLS via memory BIOS (context/engine/connection)
- [cert-rotation.md](cert-rotation.md) - Filewatcher + CertReloader (K&s)
- [logging•md](logging.md) - logger, formatters, structured fields
- [benchmarks. md](benchmarks.md) - throughput/latency measurement suite
