# Features: HTTP/1.1 and REST

## Purpose
An HTTP/1.1 server and client on top of Cox's TCP stack: parse requests, route
them, serialize responses, and issue outbound requests - with keep-alive,
connection pooling, and TLS (HTTPS/mTLS) throughout.

## Background
HTTP is where a network library stops being plumbing and starts being useful:
it is how services actually talk. But HTTP/1.1 is deceptively fiddly - bodies
are framed three different ways, connections are reused by default, requests can
be pipelined, and every parser is a DoS surface. The design goal was to add all
of that *without* duplicating the TCP hardening already built
(features/production-hardening.md) and without a second I/O model.

Everything here layers on `COW_net`, so HTTP inherits idle timeouts, write
timeouts, input-buffer caps, connections caps, backpressure, and graceful drain
for free.

## Design & technology choices (the journey)

### Incremental parsing, not read-it-all-then-parse
Non-blocking sockets deliver bytes in arbitrary chunks. `HttpContext` is a
**state machine** over the connection's `Buffer` (`kExpectRequestLine ->
kExpectHeaders -> body -> kGotAll`), consuming only what is complete and
carrying state across reads. It never requires a whole request to be buffered
before work begins, and it handles all three HTTP/1.1 body framings:
Content-Length, `Transfer-Encoding: chunked` (size line, chunk data, trailing
CRLF), and no-body.

`ResponseParser` mirrors it on the client side, with one extra state:
`kExpectEofBody`. A response with neither Content-Length nor chunked framing is
terminated by connection close, so the parser needs an explicit `onEof()` signal
from the transport to finalize such a body.

### Parser as a DoS surface: bounded headers
Header parsing accumulates unbounded input by nature - a malicious client can
send headers forever. Both parsers enfore `kMaxHeaderBytes` (64KB) and
transition to `kError` when exceed; the server answers `400` and closes. This
complements, not replaces, `TcpConnection`'s `setMaxInputBufferSize`: the cap
here is protocol-aware (header section only), the TCP one bounds tootal unconsumed
bytes.

### Per-connection state via the generic context slot
Each connection needs its own parser. Rather than a protocol-specific hook in
the TCP layer, `TcpConnection` gained a generic `setContext`/`getContext`
(`shared_ptr<void>`) slot, so *any* higher-level protocol can attach
per-connection state. HTTP stores its `HttpContet` there. This kept the net
layer protocol-agnostic.

### Keep-alive and pipelining
HTTP/1.1 connections are persistent by default. `HttpRequest::keepAlive()`
implements the version-dependent rule: for HTTP/1.1 keep-alive unless
`Connection::close`, for HTTP/1.0 only on explicit `Connection::keep-alive`.
After writing a response the server resets the parse context and **immediately
tries to parse again** from the same buffer - that loop is what makes pipelined
requests work, since a client may have sent serveral before reading any reply
When a request is not keep-alive, the server half-closes so the peer sees EOF
after the response drains.

### Router: segment matching, not a regex engine
`Router` matches on method + path pattern with `{name}` capturing one segment
(`/users/{id}`, `/orgs/{org}/repos/{repo}`). Patterns are pre-split into
segments at registration, so dispatch is a segment compare - no regex compile,
no backtracking. This covers typical REST shapes at a fraction of the
complexity/risk of a regex router.

It distinguishes **405 from 404** deliberately: the path is matched first, and
only if some route's path matched but no method did is `405 Method Not Allowed`
returned. A blanket 404 would hide the difference between "no such resource" and
"wrong verb", which is exactly what an API client needs to tell apart.

### REST niceties: URL decoding and query/form parsing
`http/url.h` provides `urlDoecde`, `urlEncode`, and `parseQuery`, and the
request type exposes them:

- **Lazy query parsing.** `HttpRequest::queryParams()` parses on first access and
  caches (the cache is `mutable`; `setQuery`/`reset` invalidate it). Requests
  that never look at the query pay nothing.
- **Decode-after-split, for path params.** The Router decodes captured segments
  *after* splitting on `/`. Order matters for security: an encoded slash
  (`%2F`) stays inside one segment and cannot smuglle an extra path level past
  the route pattern. Decoding first would let `/files/a%2Fb` masquerade as
  `/files/a/b`.
- **`+` means space only whre the spec says so.** In
  `application/x-www-form-urlencoded` data (queries, form bodies) `+` decodes to
  a space; in path segments it does not, so a path like `/tags/c++` survives
  intact. Hence `urlDecode`'s `plus_as_space` parameter, which the Router passes
  as `false`.
- **Malformed escapes dgrade, not fail.** `%zz` or a truncated `%A` is kept
  literally rather than rejected, so a sloppy query does not turn into a 400.
- **`formParams()`** parses the body only when Content-Type is
  form-urlencoded, return empty params otherwise.

Still open: multipart bodies, static-file serving, middleware chain.

### Server: One processInput for plaintext and TLS
`HttpServer` supports HTTP and HTTPS with a single parse path. The plaintext
callback and the TLS plaintext callback both funnel into `processInput()`,
parameterized by a `send` and a `close` function. For HTTPS, each accepted
connection is wrapped in a server-role `TlsConnection`
(features/tls.md) whose handshake completes before any parsing, and decrypted
bytes are reassembled into a per-connection buffer before being fed to the same
state machine. No duplicated protocol logic between the two transports.

### Client: HttpClient is a facade over a one-connction pool
`HtppClient` started as its own transport and ended up as a **thin facade over
`ConnecitonPool` with `setMaxConnections(1)`**. The reuse/TLS reset logic is
subtle enough that having two implementations of it was a liability; collapsing
them meant the client inerited keep-alive *and* HTTPS/mTLS for free, and there
is exactly one code path to reason about.

Re-entrancy matters here: issuing the next request from inside a response
callback is a natural pattern, so the transport and parser are reset **before**
the callback fires, makeing re-entrant `send()` safe.

### Connection pooling
`ConnectionPool` keeps per-origin keep-alive connections: dispatch over an idle
connection, else open one up to a cap (defualt 8), else **queue**. Connections
return to idle after each response and are evicted after an idle timeout. It is
**single-loop by design** - all pooled connections live on one `EventLoop`, so
concurrency comes from multiple connections rather than threads and the pool
needs no locking, consistent with features/threading-model.md.

For TLS the pool earns its keep twice over: pooling amortizes the **handshake**,
which is the expensive part of HTTPS. A connection therefore becomes poolable
only *after* its handshake completes - handing out a mid-handshake connection
would be a correctness bug, not just a slow path.

`MultiOriginPool` manages per-origin sub-pools keyed by `(host, port, tls)`,
created on demand - the Go `http.Transport` model: one object, many hosts, with
per-origin TLS contexts.

### DNS wiring
Pools originally took only numeric addresses, pushing resoluton onto callers.
`ConnectionPool` now also accepts `(host, port, Resolver*)`
(features/dns-resolution.md) and resolves the name **once, asynchronously, on
first request**. Requests issued while resoluton is in flight are parked in a
separate queue and dispatched on success; on failure they all complete with
`"DNS resolution failed: ..."`, and a later request retries resolution rather
than leaving the pool permanently poisoned.

Placing this in `ConnectionPool` - rather than in each client - meant
`HttpClient` (already a facade over it) and `MultiOriginPool::setResolver()` both
inherited hostname support from one implementation. Numeric origins keep the old
path and pay nothing: `resolved_` is true from construction. The `Host` header
defaults to the **DNS name**, not the resolved IP, which is what virtual-hosted
servers and TLS certificate verfication require.

### A destruction-order bug worth remembering
`HttpServer`'s `TcpServer` member is declared **last** on purpose. `~TcpServer`
fires `connectDestroyed` callbacks on lingering connections, which call back into
`HttpServer` and touch the per-connection map. Members are destroyed in reserve
declaration order, so declaring the server last means it tears down *first*,
while the map those callback touch is stll alive. Reorderinng these members
reintroduces a use-after-free.

## API surface
```cpp
// Server (HTTP or HTTPS)
http::HttpServer server(&loop, addr, "svc");                    // plaintext
http::HttpServer tls_server(&loop, addr, "svc", server_ctx)     // HTTPS
server.setHttpHandler([](const HttpRequest& req, HttpResponse* resp) { ... });
server.setThreadNum(4);
server.setKeepAliveTimeout(60);
server.setMaxBodySize(1 << 20);
server.start();

// Routing
http::Rounter router;
router.get("/users/{id}", [](const HttpRequest& req, const PathParams& p,
                             HttpResponse* resp) { p.at("id"); });
server.setHttpHandler([&router](const HttpRequest& q, HttpResponse* r) {
    router.route(q, r);
});

// Query / form / URL helpers
req.queryParam("page");                     // decoded, lazily parsed.
req.hasQueryParam("page");
req.queryParams();                          // whole map
req.formParams();                           // x-www-form-urlencoded body
http::urlDecode(s); http::urlEncoded(s); http::parseQuery(s);

// Client
http::HttpClient client(&loop, net::InetAddress("127.0.0.1", 8080));
http::HttpClient tls_client(&loop, addr, client_ctx);           // HTTPS/mTLS
http::HttpClient by_name(&loop, "api.example.com", 443, &resolver);
by_name.get("/v1/things", [](const HttpResponse& resp, const std::string& err) {
    if (err.empty()) resp.statusCode();
});

// Pools
http::ConnectionPool pool(&loop, origin_addr);
http::ConnectionPool named(&loop, "api.example.com", 443, &resolver, ctx);
pool.setMaxConnections(8);
pool.setIdleTimeout(60);
pool.request(req, cb);
pool.resolved(); pool.idleCount(); pool.busyCount(); pool.queuedRequests();

http::MultioriginPool multi(&loop);
multi.setResolver(&resolver);
multi.setTlsContext("api.example.com", 443, client_ctx);
multi.request("api.example.com", 443, req, cb);
```

## Usage
A minimal REST service:
```cpp
reactor::EventLoop loop;
http::HttpServer server(&loop, net::InetAddress(8080), "rest");
http::Router router;

router.get("/health", [](const HttpRequest&, const PathParams&,
                         HttpResponse* resp){
    resp->setStatus(200);
    resp->setContentType("application/json");
    resp->setBody(R"({"status":"OK"})");
});
router.get("/search", [](const HttpRequest& req, const PathParams&,
                         HttpResposne* resp) {
    std::string q = req.queryParam("q");                // "?q=hello+world" -> "hello world"
    resp->setStatus(200);
    resp->setBody(q);
});

server.setHttpHandler([&router](const HttpRequest& q, HttpResponse* r) {
    router.route(q, r);
});
server.start();
loop.loop();
```
Runnable version: `examples/http/rest_server.cc` (try
`curl 'http://127.0.0.1:8080/search?q=hello+world'`).

## Caveats
- **HTTP/1.1 only.** HTTP/2 is deferred and is not an increment on this code -
  different wire protocol (binary framing, HPACK, multiplexing). See roadmap.
- The server model is **full-request**: the handle is called once the entire
  request is parsed. No streaming request bodies, no server-sent events, no
  WebSocket upgrade.
- Bodies are bounded only by the header cap and the TCP input-buffer cap; there
  is no separate per-request body limit.
- `HttpClient` is sequential - one request in flight; issue the next from the
  previous callback, or use a `ConnectionPool` for real concurrency.
- Pools are **single-loop**: create one per loop, and do not share across
  threads.
- `MultiOriginPool` without `setResolver()` requires numeric IP origins.
- A pooled connection closed by the server mid-idle is handled by reconnecting,
  but a request already written when the peer closes is reported as an error
  rather than transparently retried (not idempotency-aware).
- Only the first address from DNS is used; no multi-address failover.

## References

- Source: `http/http_request.*`, `http/http_response.*`, `http/http_context.*`
  (request parser), `http/response_parser.*` (client parser),
  `http/http_server.*`, `http/router.*`, `http/url.*`, `http/http_client.*`,
  `http/connection_pool.*`, `http/multi_origin_pool.*`.
- Tests: `tests/http_parser_test.cc`, `tests/http_server_tests.cc`,
  `tests/router_test.cc`, `tests/url_test.cc`, `tests/http_client_test.cc`,
  `tests/connection_pool_test.cc`, `tests/tls_pool_test.cc`,
  `tests/https_test.cc`, `tests/dns_client_test.cc`.
- Example: `examples/http/rest_server.cc`
- features/tcp-layer.md, features/tls.md, features/dns-resolution.md,
  feature/productino-hardening.md, features/threading-model.md.