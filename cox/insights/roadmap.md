
# Cox Roadmap & Status

## Progress snapshot (2026-11-03)
where things stand; details in the sections below.
**Done this round**
-   TLS session resumption (tickets + rotation, origin-keyed client cache) •
-   Metrics: every metric in the design table is emitted adds thread pool,
HTTP rejections, TLS handshakes, DNS lookups).
-   Installable package (" find_package(cox)"), coverage, CI/CD hygiene, tree fully clang-format conformant (format job deliberately non-blocking).
-   HTTP/2 (opt-in, "COX WITH_HTTP2): server with configurable SETTINGS and limits and graceful two-step GOAWAY; multiplexing client with GOAWAY / stream-ID retry, several connections per origin, DNS origins.
-   Async handlers + streamed responses, phase 1 of 4 (server, HTTP/2).
*  *In progress** - streaming bodies / async handlers (gap list #1 and #2): phase 2 next (HTTP/1.1 chunked streaming), then phase 3 (streamed request bodies) and phase 4 (client streaming).
*  *Bugs found and fixed this round** (each has a record in docs/insights/troubleshooting/°):
-   TLS 1.3 sessions stopped resuming once their connection closed.
-   Reactor: "queueInLoop" skipped the wakeup when it was needed (since the start).
-   "TIsConnection hid disconnects from "Httpserver": every HTTPS connection's state leaked until shutdown.
-   HTTP/2: the request cap sent the final GoAwAY too early, so requests ran twice.
-   "Serverstream" leaked when handlers captured their own stream.
*  *Next up, by priority** (HTTP/2 gap list below): finish streaming phases 2-4 → trailers → per-request timeout + cancellation → PING keepalive → client idle policy → h2c → the rest.
*  *Known limitation**: socket tests bind fixed ports. Since 2026-11-09 these sit below the ephemeral range, so ctest -j8 is clean, but overlapping preset runs can still collide. Port-e allocation is a TODO (see TODO - Test infrastructure; CI/CD #3, KNOWN CONSTRAINT).

**Test status**: 219 tests with HTTP/2 off, 260 with it on ("http2" preset); both green, as are ASan/LSan and TSan on the HTTP / TLS / TCP / reactor tests, GCC Release, and clang-tidy.
## Completed
-   **Reactor core**: EventLoop (epoll), Channel, Poller, eventfd wakeup, build-mode-dependent poll timeout (see docs/insights/features/poll-timeout-strate
-   **Timers**: timerfd-based TimerQueue (runAt/runAfter/runEvery/cancel).
-  **Multi-threading**: EventLoopThread + EventLoopThreadP001; repServer setThreadNum(N) distributes connections one-loop-per-thread.
**Blocking-work pool*: bounded worker ThreadPool (concurrent/) with drain-on-stop and load-shedding tryRun (see docs/insights/features/reactor-blockin
-  **Buffer**: growable application buffer (buffer/).
-   **TCP layer**: InetAddress (IPv4/IPv6/Unix), Socket + ops, Acceptor, TopConnection (shared_ptr + Channel tie, TepServer, Connector, Tepclient.
-   **Unix domain sockets**: full support reusing the TCP stack.
-   **Production hardening**: SIGPIPE ignore, idle timeouts, output high-water-mark backpressure, max-connections cap.
**Graceful shutdown /Idrain**: stop-accept -> half-close -> deadline force-close -> completion callback; SIGTERM/SIGINT example.
-**Flow control** (flowcontrol/): TokenBucket rate limiter, per-key jittered
Backoff (wired into Connector), ConcurrencyLimiter (see docs/insights/features/flow-controller-design.md).
**Overload control (scoped)**: accept-rate shedding, overload notification callback, ingress backpressure (TcConnection stopRead/startRead), ConcurrencyLimiter for request-level admission. Reject-by-default, never blocks the loop.
Test status: full suite green in Debug and Release via build. sh.
## Build-order rationale (historical)
The order in which the items above were built followed an explicit prioritization (recorded here for the reasoning; every item listed is now either Completed above or Discarded below - none is outstanding):
1.    Graceful shutdown / drain - safety first; unblocks clean deploys.
2.    Blocking-work thread pool - prevents a blocked callback stalling a whole io loop; small.
3.    "flowcontrol/ substrate - token bucket + per-key jittered backoff.
4.    Overload control - policy layered on #3.
5.    Deferred hardening - input-buffer cap, write timeout, connection max-age.
6.    Observability / metrics - needed to tune #4.
7.    TLS - large; sequenced after the core so it inherits #1/#3/#5.UDP (later discarded), Filewatcher -independent features, slot in on demand.
9. Logging improvements - opportunistic polish.
Principles behind the ordering:
- **Safety before features.** Graceful shutdown and the blocking-work pool prevent the two most common production failures - dropped requests on deploy, and a blocked callback stalling an entire io loop.
**Build the substrate, then the policy.** The client-go-style flow-control primitives are the reusable foundation; overload control is policy on top.
This order avoids duplicating rate-limit/backoff logic and let the ad-hoc, un-jittered backoff in "Connector" be retired in favor of the shared per-key jittered "Backoff" (un-jittered backoff is a thundering-herd risk on mass reconnect, so adding jitter was a small, high-value fix).
*  *Metrics before deep tuning.** Overload/flow-control thresholds can't be tuned without observability, so metrics preceded fine-tuning.
*  *TLS after the core is hardened**, so it inherits shutdown/backpressure rather than needing retrofit.
-
**overload control started static** (fixed max-inflight + accept rate); adaptive/latency-based shedding was left for later only if load tests showed it was needed, since adaptive is more code and harder to reason about.
## Deferred
### OAM / operability (deferred by decision, 2026-09-04; metrics since done) Alarms, ISP/network events, audit logging, and tracing are deferred. METRICS were split out and are DONE (see "Completed (metrics)" below).
Rationale: these are cross-cutting operational cahcerns a network library can provide *hooks* for rather than *implement*. Cox already exposes the necessary hooks:
-   setoverloadCallback (shed/overload events),
-   connection/close callbacks (lifecycle events),
-   Logger (event logging).
An application or a dedicated oAM layer can attach real alarms/audit to these hooks. Revisit if/when a concrete operability requirement appears.

### Distributed tracing (deferred; part of OAM)
ull distributed tracing - spans, span export to Jaeger/Zipkin/OpenTelemetry, and cross-service context propagation - is deferred as OAM. It is NOT a logging eature (TRACE-as-a-log-level is separate and in scope for the logging module).
ationale and integration approach when revisited:
-   The library should NOT embed a tracing SDK/exporter (span collection, sampling, OTLP export live in an application/OAM layer).
-  The library SHOULD provide the propagation hook: a per-request/per-connection"context" carrying a trace id, populated from and waitten to standard headers at the HTTP layer (W3C"traceparent/tracestate, or B3). That trace id is then attached to structured log fields (e.g. "("trace_id": ...}) so logs correlate across services.
-   Concretely: this lands naturally WITH the HTTP work - read "traceparent on inbound requests, propagate it on outbound client calls, expose it in a request context, and surface it to the logger's structured fields. No span machinery in the core.
-   TRIGGER: a concrete requirement to correlate requests across services, or an HTTP layer that needs context propagation.
# Completed (metrics)
**Metrics module** ("metrics/', target " cox_metrics; design in docs/insights/features/metrics-design.md): Counter / Gauge / Histogram (relaxed atomics, cumulative Prometheus-style buckets), a "Registry" with labels, a format-neutral Snapshot, scrape-time filtering, sampled (computed-at-scrape) instruments, and Prometheus text exposition ( prometheus (snapshot)
, ve.0.4). OPT-IN adapters wire the core through
public hooks/accessors, so the core libraries do not depend on metrics:
*installserverMetrics() (accepted / rejected / live / closed(reason} / bytes intout), installHttpMetrics) (requests by status class + handler duration histogram + "cox_http_rejected_total(reason}"), "installLoopmetrics()
(loop iterations + poll wait), and - completing the design's instrumentation table (2026-11-03) - "installThreadPoolMetrics()
(cox_threadpool_tasks_totalfresult=run|shed}, cox_threadpool_queue depth),
*  installTlsMetrics) (cox_tis_handshakes_total(result=ok|failed}) and
*  installonsMetrics) ( cox_dns_lookups_total(result=ok|failed}). The new
ones follow the loop-metrics pattern: each subsystem keeps relaxed-atomic counters behind a plain accessor ( ThreadPool:: tasksRun/tasksshed,
*Httbserver::reiectedBadReauest/reiectedBodvTooLarge.
TlsContext:: handshakes0k/Failed’,’
,Resolver::100kupsok/Failed) and the
metrics module samples them at scrape time, so no core library depends on metrics. Details worth knowing: a TLS handshake counts as failed both on a protocol error and when the peer closes before finishing, and is recorded once per connection on the context it was created with; DNS counts live on the Resolver" base so both backends report identically (a cache hit counts as ok); HTTP rejections are counted in both requests_total{4xx} and
" rejected_total.
The "/metrics" endpoint is application-wired (used by c10k_server /
"http_target in load validation). Tests: "tests/metrics_test.cc" (22) and
*tests/metrics_server_test.cc* (9 integration, incl. a neal openssl _client handshake and a plaintext-on-TLS-port failure), ASan/TSan clean; the c-ares DNS backend was checked separately (1 ok + 1 failed recorded). optional and not built: the per-loop sharded Counter (only if profiling shows contention) and the OpenTelemetry adapter (COX_WITH_OTEL, design 59).
## Completed (hardening)
- **Load & adversarial validation (PI #6, done 2026-11-03)**: all eight scenarios S1-S8 implemented and reproducible - the hardening knobs are now
*proven under adversarial load*, not just unit-tested. Harness (under examples/c10k/* measurement tools kept out of the library: c10k_client
--attack (slowloris, flood, slow-read, big-body} " , http_target (HTTP body-limit
target), "probe_client (liveness, with "--no-read/--http modes),
"sample. sh (containment CSV from "/metrics" + /proc VmRSS + "ss*), and
*  c10k_server" hardening flags ('--max-age/--write-timeout/--max-input-buf/
-  -sink/--work-ms/--max-inflight/--metrics-port*). Each defense driven by its matching attack; containment + liveness MEASURED. Scenario commands + pass/fail in "docs/performance/hardening-validation.md"; a reduced, non-blocking CI smoke tier ('scripts/hardening_smoke.sh, wired into ci.yml) asserts the containment invariants. production-hardening.md caveat updated to "validated under load." Findings surfaced and recorded: the global accept-rate limiter is blind to legitimacy under flood (S5 - containment holds, per-client fairness does not; per-source admission is future work); close reasons are now metric labels (below); and two logging features (NullSink, flood-suppression DedupSink) were triggered by the attack-log volume (see logging section). One wrong turn was caught and reverted: an S2 failure was misdiagnosed as a write-timeout defect and the one-shot timer was briefly redesigned on an unproven theory; the real cause was a load-generator bug, so the core change was reverted.
-   **Deferred hardening (done 2026-09-04)**: write timeout (slow-consumer force-close), max input buffer size (read-side memory bound), connection max-age (lifetime recycling). All on Topconnection, exposed at Topserver too.
-   **close-reason attribution (done 2026-11-03, during load-validation #6 / S6)**:"TopConnection: :CloseReason (peer / idle / write_timeout / max_age / input_cap) set at each force-close site and read via closeReason() . "installserverMetrics now emits "cox_connections_closed_totalfreason=...) (labelled, no unlabelled aggregate - the total is sum-by-reason, matching coxhttp_requests_total). Lets the load-validation scenarios prove the *specific* defense fired (S6 asserts {reason="max_age"}; s2/s3 will use write_timeout/input_cap) rather than a coarse "something closed." Verified by metrics_server_test and an ASan-clean S6 recycling run (no leaked Tcponnection/timer).
## Completed (TLS)
-**TS (done 2026-09-04)** (OpenSSL 3, TS 1.2+/1.3): T1sContext (cert/key/CA, peer verification, mTS), TlsEngine (memory-BIO state machine, socket-free and unit-tested), Tisconnection (Layers TLS over TepConnection, inheriting all hardening). Handshake + encrypted I/0 + verification validated end-to-end.
**Cert hot-reload (done 2026-09-04)** for Kubernetes Secret rotation:
Filewatcher (fswatch/, inotify primitive; handles the K&S ..data atomic symlink swap), Tiscontext: :reload() (swaps cert/key/CA on the live SSL_CTX; rejects bad/partial material and keeps the previous cert), CertReloader (tIs/, triggers reload via inotify watch or timer poll). K8s tls.crt/tls.key/ca.crt Secrets are supported directly (useCertificateAndkey + loadCaFile + severifypeer for mTS), now with rotation and no pod restart.
## Completed (TLS enhancements: SNI + ALPN + OCSP, done 2026-09-05)
- **SNI-based certificate selection (done 2026-09-05)**: TlsContext:: addSnicontext(hostname, ct) registers per-hostname contexts; the context it is called on stays the default. Selection runs in OpensSL's tIsext_servername callback via SSL_set_SSL_CT, re-applying options and verify mode from the chosen context (SSL_set_SSL_CT does NOT carry those over, so a per-host setverifypeer would otherwise be silently ignored).
Exact + single-label wildcard matching (RF 6125), case-insensitive, exact beats wildcard; callback installed lazily so non-SNI servers pay nothing.
An UNKNOWN hostname falls back to the default cert rather than aborting the handshake (nginx default_server behavior). Also required building the CLIENT side, which did not exist: Cox never sent SNI, so server-side selection was untestable. TlsEngine/TlsConnection now take a server hostname, send it (suppressing IP literals per RFC 6066 §3),
", not "is the host I meant
to reach". http: ConnectionPool passes its host header through, so the HTTPS client and pools get SNI + name checking for free.
Tests: "tests/sni_test.cc" (12 cases, incl. a negative control that trusts the wrong cert and MUST fail, proving selection is real rather than verification being off).
**ALPN negotiation (done 2026-09-05)** (RFC 7301):
"TIsContext: :setAlpnProtocols(f"h2"
', "http/1.1"}) on both roles.
CLIENT advertises the list directly (SSL_CTX_set_alpn_protos"); SERVER installs a selection callback (SSL_CT_set_alp_select_cb) where SERVER PREFERENCE wins on overlap (SSL_select_next proto, matching nginx/nghttp2).
Protocol names are given as plain strings ("h2", "http/1.1"); Cox encodes the ALP wire format (1-byte length + name) and validates each is 1..255 bytes.
No shared protocol does NOT abort the handshake - it completes with nothing selected and the peer falls back (typically HTTP/1.1); likewise if no ALP is configured. The negotiated protocol is readable via
TIsEngine: :negotiatedprotocol() (both ends), "" when none was agreed.
Tests: "AlpnTest in "tests/sni_test.cc" (4 cases: shared-protocol agreement, server-preference-wins, no-overlap-still-hardshakes, no-ALPN-means-none).
The former gap - ALPN not forwarded on "TIsConnection - is CLOSED (2026-11-03): TlsConnection:: negotiatedProtocol() exists and "Httpserver uses it to route h2" vs "http/1.1" (see HTTP/2 below).
**OCSP stapling (done 2026-09-05)** (tls/ocsp_stapler.**): the server fetches the signed non-revocation proof and attaches it to the handshake, so clients need no round trip to the CA (which costs latency, leaks browsing to the CA, and usually fails soft anyway). Fetch is OUT OF BAND and cached; the handshake callback only copies cached bytes, so TS never blocks on the network.
Responder URL discovered from the cert's AIA extension (overridable).
Validates before caching - rejects non-successful responses, any status that is not 'good, and anything outside its validity window: stapling a revoked response would actively tell clients to reject your own server, and a malformed staple is worse than none. FAILS OPEN: nothing cached means an unstapled handshake, so a responder outage degrades stapling instead of taking the server down. Two deployment shapes: refresh() (blocking HTTP fetch - worker thread or timer, never a loop thread) and loadFromFile() for a DER written by an external agent (certbot/sidecar), which is what most production setups do.
NOTE: nextupdate is OPTIONAL (RFC 6960 §4.2.1), so "secondsUntilExpiry() returns › (time left), 0 (expired/none), or -1 (cached, no expiry hint).
Collapsing the last two made a valid response look stale - OpenSSL's own responder omits nextupdate unless given -min/-ndays, so this is the common case, not a corner. Tests: "tests/ocsp_test.cc" (11 cases; builds a real mini-PKI, runs "openssl ocsp as a live responder, and verifies client-side that the staple actually arrived in the handshake).
**Automatic staple refresh (done 2026-11-03)**: ^OcspStapler: :startAutoRefresh (loop, pool, OspRefreshoptions)* / stopAutoRefresh() drive refresh()
on a
schedule so the staple never goes stale in production (manual refresh was called a footgun). The next delay is derived from "secondsuntilExpiry() : refresh at refresh_ratio of the remaining lifetime (default 0.5, i.e. halfway to nextupdate) clamped to "[min_seconds, max_seconds]'; use fallback_seconds when the response carries no nextupdate hint (-1); retry after retry_seconds when nothing is cached / expired / the fetch failed (0).
Respects Cox's loop discipline: the BLOCKING network fetch is oftloaded to a
"concurrent::ThreadPool" (never the loop thread) and SHEDS a cycle if the pool is saturated (reschedules rather than blocking) - the same tryRun-or-skip pattern used elsewhere. Rescheduling hops back to the loop so the one-shot timer stays single-threaded. Teardown-safe: *~Ocspstapler" calls
* stopAutoRefresh()*
which cancels any armed timer under a stopped flag so a
pending callback cannot fire into a destroyed stapler (loop/pool must outlive it, documented). refresh_now kicks an immediate first fetch on start.
Tests: "OcspAutoRefreshTest" in "tests/ocsp_test.cc (starts/stops cleanly without a responder; destruction while a refresh is scheduled is clean).
NOTE still open: "lsContext: :reload() (cert rotation) does NOT itself re-fetch a staple - pair a rotation with a refresh when both are driven.
# Completed (TLS session resumption, done 2026-11-03)
**Session resumption / tickets (done 2026-11-03)**: returning clients skip the full handshake (certificate exchange + asymmetric key agreement), cutting latency and CPU on high connect churn. Server side: "TicketKeyManager" (tls/ticket_key_manager.**) owns the ticket keys and implements the openSSL EVP ticket-key callback - AES-256-CBC seal + HMAC-SHA256, CSPRNG keys, memory only. It ROTATES (OpensSLauto-generates one key and never rotates it, which breaks forward secrecy on a single key compromise): a fresh current key plus the immediately previous key retained for a grace window so tickets issued just before a rotation still resume (returning 2 from the callback re-issues them under the new key). Tiscontext: :enablesessionTickets(loop, rotate_s)
installs it and arms a rotation timer on the loop (same pattern as CertReloader). Client side: "SessionCache" (*tls/session_cache.**), a bounded LRU cache of resumable sessions keyed by ORIGIN (hostname) - OpenSSL's built-in client cache is keyed by session id, not by the hostname a connection pool reconnects on, so http: :Connectionpool/ MultioriginPool get resumption on a pooled reconnect for free. "lsEngine" offers any cached session for the origin before the handshake and stashes the origin so the new-session callback stores the negotiated session back (works for TS 1.3, whose ticket arrives post-handshake). "TIsContext:: disableResumption) turns both off (no tickets, no caching) for a full handshake every time.
Tests: *tests/tls_session_resumption_test.cc" - TicketKeyManager rotation bound, "SessionCache store/get/LU-evict, and two end-to-end socket-free resumption cases (resume with server tickets; disabled -> full handshake), driven by piping two "Tisengine's together.
SUBTLETY FOUND AND FIXED see
docs/insights/troubleshooting/tls13-session-nonresumable-after-free.md): for
TLS 1.3 a session from the new-session callback is tied to its originating
*SSL*; freeing that connection marks it non-resumable. The cache therefore stores an INDEPENDENT SSL_SESSION_dup() copy, not a shared up-ref, so a pooled ticket survives the close of the connection that minted it (the exact pool-reconnect case). Effort was low-moderate as predicted; the dup detail was the only non-obvious part.
## TODO - TLS enhancements (deferred; add when the triggering use case appears)
- **Per-SNI-context stapling** - "Ocspstapler: :attachTo() installs the callback on ONE context, so a multi-certificate (SNI) server currently needs a separate stapler per context and there is no combined wiring. TRIGGER: a public edge serving several publicly-trusted certs that all need stapling.
## TODO - Test infrastructure
- **Ephemeral (port 0) listen ports in socket tests** - the tests listen on hard-coded ports. Before 2026-11-09 most of them sat inside the Linux ephemeral range (32768-60999), so under ctest -j8' one test's OUTGOING connection could be given another test's listen port as its source port, and that test aborted in sockets: bindorDie ... Address already in use (http2" suite: 10/10 parallel runs failed). INTERIM FIX (2026-11-09): all 71 such ports were shifted down by 25000 (now 20601-31399), below the ephemeral range; ø/10 failures since. What is left: two hard-coded ports
## TODO - Test infrastructure
can still collide, e.g. when two ctest --preset runs overlap on one machine (CI/CD #3, KNOWN CONSTRAINT).
PLAN: bind to port so the kernel assigns a free port, reserved until the socket closes, then read it back with getsockname ()
-   Add "Acceptor: :listenAddress) / TcpServer: :listenaddress()* (valid after 'listen() / start()"), built on the existing sockets:: getLocaladdr) •
-   Tests pass "InetAddress("127.0.0.1", 0) and hand the assigned port from the server thread to the client side (most already sync on" srv_started) .
-   "Nothing listens here" ports ("top_client_test" dead port,"http2_client_test" 31399) and externally spawned servers ('ocsp_test 's openssl ocsp responder, port 18790) cannot use port e directly: use bind-ø / read / close to pick a likely-free port, plus a retry for the external process.
Done when no test hard-codes a listen port, and overlapping preset runs pass.
## Completed (benchmarks & logging)
-   **Benchmark suite (done 2026-09-04)**: *examples/benchmark/^ now has an echo throughput/latency benchmark (real TepServer + Nclosed-loop client connections; reports QPS, MB/s, p50/p90/p99/max) and a buffer micro-benchmark, alongside the existing task-scheduling bench. Indicative loopback numbers (release, dev machine): ~84K QPS single-thread and ~234K QPS with 4 io threads at 128B echo; ~77 MB/s at 1KB single-thread. Numbers are machine-dependent and for relative comparison, not absolute claims.
-   **Logging level filtering (done 2026-09-04)**: " Logger: :10g" now drops messages below the configured level (default Info), so per-connection lifecycle logs were demoted to Debug and no longer flood output/benchmarks. Async logging remains a future improvement.
## Completed (HTTP/1.1 + REST)
- **HTTP/1.1 + REST (done 2026-09-04) ** (http/ module on top of cox_net) :
HetpRequest/HttpResponse, an incremental Httpcontext parser (request line, headers, Content-Length + chunked bodies, keep-alive, pipelining, header-size cap), HttpServer (callback-based full-request model, keep-alive, inherits TCP hardening; per-connection parse context via TopConnection:: setContext), and a Router (method + path patterns with fparam} capture, 404/405). Verified with unit tests and real curl against the rest_server example.
-  **TcpConnection generic context slot (done 2026-09-04)**: setContext/getContext (shared_ptr<void›) so higher-level protocols attach per-connection state.
-   **Httpclient (done 2026-09-04)**: outbound HTTP/1.1 client over cox net's Topclient (request serialization, ResponseParser for status line + headers + Content-Length/chunked/EOF-delimited bodies, callback delivery). Now supports KEEP-ALIVE / connection reuse (setKeepAlive, default on): the connection is kept open after a response and reused for the next request (one in flight at a time; transparent single reconnect if a reused connection was closed). Honors the server's Connection header. Re-entrant send() from the response callback is safe (transport/parser reset before the callback). Verified Cox-client <-> Cox-server GET/POST and two-requests-over-one-connection end-to-end.Also: hardened ~Tcpclient to tear down directly when on the loop thread; added HttpServer: :setConnectionobserver (observe connections without hijacking the internal parse-context callback).
## Remaining / not yet scheduled
Note: despite this section's title, the HTTP/2 items below are largely done (dates in each entry; what is still open is the HTTP/2 gap list.
-   **HTTP/2 server (done 2026-11-03)** - opt-in, "COX_WITH_HTTP2* (defaultOFF; cmake --preset http2*). Wraps nghttp as recommended rather than hand-rolling HPACK/multiplexing. http: :Http2Session"(http/http2_session.**) is a SOCKET-FREE server session (like "TlsEngine"): decrypted bytes in, frames out, one "HttpRequest / HttpResponse per stream."HttpServer switches a TLS connection to it in the handshake-done callback when ALP agreed "h2" (TIsConnection: :negotiatedprotocol), added for this, so ONE handler serves h2 and HTTP/1.1; h2 requests report
*  version() == "HTTP/2". Enable it by advertising
*setAlpnprotocols(f"h2", "http/1.1"}) on the server's Tiscontext.
Containment: SETTINGS cap concurrent streams (setHttp2MaxConcurrentstreams" default 100; excess streams refused with REFUSED_STREAM) and the header list
(64 KB → 431); setmaxBodysize applies (413, then RST_STREAM(NO_ERROR) once the response is sent); output is only pulled from nghttp2 while the socket's output buffer is under 1 MB and resumes on write-complete; connection-specific headers (Connection, Transfer-Encoding, •..) are never forwarded; methods outside cox's model (CONNECT, TRACE) get 405. nghttp2 enforces flow control, pseudo-header validation and RST_STREAM rate limiting (the 2023 "rapid reset" attack). The request observer and "cox_http_requests_total / cox_http_rejected_total metrics count h2 requests too.
DEPENDENCY: system "libnghttp2" (>= 1.43) via pkg-config by default; a pinned third_party/nghttp2 submodule is used instead when checked out.
'third_party/ is now added BEFORE the Cox modules so a vendored target exists when cox http links it.
Tests: "tests/http2_session_test.cc" (11, socket-free against an nghttp2 client session: GET/POST, 10 multiplexed streams, 200 KB response, header filtering, 413, 431, 405, REFUSED_STREAM over the cap, non-h2 preface, backpressure stall + resume) and "tests/http2_integration_test.cc" (5, real curl --http2*: h2 and h1.1 on one handler, 20 parailel requests on ONE connection, 300 KB response, POST, 413, h1 fallback when "h2" is not advertised). ASan/LSan and TSan clean, GCC Release clean, clang-tidy clean.
CI: non-blocking "http2 (nghttp2)" job. With the option OFF the default build pulls in neither nghttp2 nor any h2 code (suite still 219/219).
CONNECTION LIFECYCLE (2026-11-03): "Httpserver::Httppoptions makes every
SETTINGS value and limit configurable - max concurrent streams, header list size, initial (per-stream) and connection window, max frame size, max stream ID, max requests per connection - validated against RFC 9113 bounds (std: : invalid_argument). Planned closes now use the two-step GOAWAY (notice with last_stream_id 2^31-1, then the final GOAWAY with the last stream processed after goaway_notice_ms), so in-flight requests finish and the client knows which ones to retry: server drain (new TcpServer:: setDrainTakeoverCallback), connection max age (new
*TepConnection: :setMaxAgecallback, bounded by a grace period), the request cap, and reaching "max_stream_id" (streams above it are not processed and are retryable on a new connection - HTTP/2 cannot reset stream IDS).
BUG FIXED (troubleshooting/tlsconnection-hid-disconnects-leaked-state.md) :
TlsConnection overwrote the TCP connection callback instead of chaining it, so "Httpserver" never saw any TS disconnect and kept every HTTPS connection's state (HTTP/1.1 too) until server shutdown; caught by the new closedConnectionsAreReleased test. Tests: 19 unit + 9 integration (curl reconnecting on the request cap, a drain that finishes an in-flight request, max age recycling without a failed request). Suites 219 (h2 off) / 239 (h2 on), ASan/TSan, GCC Release and clang-tidy clean.
**HTTP/2 client (done 2026-11-03) ** - "http: :Http2Client" (http/http2_client.*) on a socket-free http: :Httpzclientsession
(http/http2_client_session.*"). One origin over TS with ALP h2 ; send() / get) can be called freely and requests run concurrently as streams (beyond the server's MAX CONCURRENT STREAMS they queue locally).
Lifecycle: on GOAWAY or client stream-ID exhaustion (IDs only increase and cannot be reset) new requests move to a fresh connection while the old one finishes its streams. Every failure is classified: above GOAWAY's Last_stream_id, REFUSED_STREAM, or never sent → retryable, re-sent automatically (max_retries, default 3); in flight when the connection died → not retried (it may have been processed. Also: server push disabled (SETTINGS_ENABLE_PUSH 0), response body cap, connect timeout (default 10s; the connector alone retries an unreachable origin forever), clear error when the server does not negotiate h2, shutdown) with GOAWAY. Tests: "tests/http2_client_session_test.cc" (11, piped into cox's server session, incl. the GoAwAy/new-stream race and a hand-built GOAWAY followed by a close) and "tests/http2_client_test.cc" (6, against a real Cox h2 server: 50 requests on one connection, reconnect on the request cap, a 40-request burst across GOAWAY with none lost or duplicated, stream-ID exhaustion, h2 not offered, unreachable origin). Suites 219 (h2 off) / 251 (h2 on); ASan/LSan, TSan, GCC Release, clang-tidy clean.
AlsO (2026-11-03): several connections per origin (max_connections" default 4: a new connection only when every existing one is at the server's MAX_CONCURRENT_STREAMS, least-loaded first, queue at the cap) and hostname origins through a "net: :Resolver" (one shared lookup for waiting requests, dual-stack failover across the resolved addresses, re-resolve after a connection to the cached addresses fails). 6 more end-to-end tests
(pool growth at the stream limit, max_connections = 1, light load on one
connection, resolve-once, DNS failure, re-resolve after a dead address).
REACTOR BUG FIXED on the way: "EventLoop: : queueInLoop" skipped the wakeup exactly when it was needed (see
docs/insights/troubleshooting/eventloop-queueinloop-missed-wakeup.md).
Later: a deterministic GOAWAY-retry test (InFlightAcrossGoAwayRetriedonNewConnection). Tests now 11 socket-free +
13 end-to-end.
*  *HTTP/2 async handlers + streamed responses, phase 1 (done 2026-11-03)** - http: :Serverstream (http/server_stream.*) and
*  HttpServer: : setstreamHandler. On HTTP/2 a handler may answer after returning and send the body in pieces without a content-length;
"write()*
returns false above stream high water (default 256 KB) and
"setonwritable resumes; "onAbort on client RST_STREAM or connection loss; 500 if dropped unanswered, RST_STREAM if dropped mid-body. HTTP/1.1
works for handlers that finish before returning (a later answer gets 500 until phase 2). setHttpHandler unchanged. Tests: 9 socket-free + 2 end-to-end with curl. Found and fixed with it: early final GOAWAY.
(duplicate requests) and a self-reference leak. Design and phases: features/http-streaming-design.md.
STILL OPEN: see "HTTP/2 - gap list vs. mainstream libraries" below.
**HTTP/2 - gap list vs. mainstream libraries (tracked 2026-11-03)**.
Compared with nghttp2's own apps, Go "net/http, Netty and h2o. The protocol core (framing, HPACK, flow control, stream states, header validation, reset / CONTINUATION / SETTINGS flood limits) comes from nghttp2 and is not a gap.
Ordered by importance; each item names what it unlocks. Status: 1= phase 1
of 4 done (IN PROGRESS), 2 = done on HTTP/2 (HTTP/1.1 pending), 3-12 = OPEN.
1.    **Streaming bodies** - IN PROGRESS. Phase 1 DONE 2026-11-63 (server, HTTP/2: "Serverstream" / setstreamHandler, streamed responses with backpressure). Remaining: HTTP/1.1 chunked streaming (phase 2), streamed request bodies (phase 3), client streaming (phase 4). Design: features/http-streaming-design.md.
2.    **Async handlers** - DONE on HTTP/2; HTTP/1.1 pending (phase 2).Since 2026-11-03 a handler may answer after returning; a slow stream no longer blocks the others on the connection.Also fixed on the way: the request cap / stream-ID limit sent the finalGOAWAY too early, so retried requests ran twice (design doc §7; troubleshooting/http2-early-final-goaway-duplicate-requests.md).
3.    **Trailers** - send/receive trailing HEADERS. Needs 1. Required for gRPC status.
4.    **Per-request timeout + cancellation** - client deadline per request,• cancel()* → RST_STREAM(CANCEL); server sees a client cancel and stops.
5.    **PING keepalive / liveness** - periodic PING with ack timeout, so a silently dead connection is detected before the next request fails.
6.    **client idle-connection policy** - close idle h2 connections after a timeout instead of waiting for the server.
7.    **h2c (cleartext)** - prior knowledge and HTTP/1.1 Upgrade. Internal service traffic behind a TS-terminating proxy, gRPC without TLS.
8.    **Extended CONNECT (RFC 8441)** - WebSockets over h2 (needs 1 + 2).
9.    **priority (RFC 9218) ** - honour "priority hints; sendSETTINGS NO_RFC7540_PRIORITIES.
10.     **Unexposed tuning** - SETTINGS_HEADER_TABLE_SIZE (HPACK table), outbound header-list cap, client-side concurrency cap, nghttp2 max SETTINGS entries / closed-stream retention.
11.     **h2 metrics** - open streams, RST_STREAM in/out, GOAWAY counts, flow-control stalls.
12.     **S9 h2 flood load scenario** - stream / header / rapid-reset floods against cox's chosen limits, in docs/performance/hardening-validation.md.
NOT PLANNED: server push (browsers removed it; nginx deprecated it).
-  **HTTP connection pool (done (done (http: :ConnectionPool"): per-origin pool of reusable keep-alive connections -- dispatches over idle connections, opens new ones up to a cap (default 8), queues when all busy at cap, returns connections to idle after each response, and evicts connections idle beyond a timeout. Single-loop (concurrency via multiple connections, no locking).
-  **TS + multi-origin pooling (done 2026-09-04)**: ConnectionPool accepts a client Tlscontext to pool HTTPS/mTLS connections (handshake amortized across requests; connection poolable only after handshake). "http: MultioriginPool manages per-origin sub-pools keyed by (host, port, tis), created on demand (Go http.Transport-style), with per-origin TLS contexts.
-   **HTTPS server + HTTPS client (done 2026-09-04)**: HttpServer gained a TLS-terminating constructor (server-role TIsContext; each connection wrapped in server TLS, handshake before parsing; per-connection state in a map).HttpClient reimplemented as a single-connection ConnectionPool, so it gained keep-alive AND a TS constructor for free (HTTPS/mTL$). Verified end-to-end:Cox HTTPS server <-> Cox HTTPS client. NOTE: fixed a member-destruction-order bug -- HttpServer's TcpServer member must be declared LAST so it tears down before the per-connection map (~TcpServer fires connectDestroyed callbacks that touch that map).
-   **DNS resolution (done 2026-09-04)** (net/resolver.h): async Resolver interface with two backends. ThreadPoolResolver (blocking getaddrinfo offloaded to a worker ThreadPool + positive TTL cache; always available, the default). CaresResolver (async c-ares, driven by the event loop via channels
-  no worker threads; compiled only when c-ares is detected, gated by COX_HAVE_CARES in Make). Verified: localhost / numeric IP / bogus-host error on both backends.
**REST/HTTP niceties (partly done 2026-09-04)**: query-string parsing and URL decoding are done ("http/url.h°): urldecode/urlEncode, parseQuery, plus HttpRequest:: queryParams)/queryParam)/hasQueryParam) (lazily parsed and
cached) and formParams) for application/x-www-form-urlencoded bodies. The Router now percent-decodes captured path params, decoding AFTER splitting so an encoded "/" (%2F) cannot smuggle an extra path segment; '+' is not treated as space in paths (only in queries/forms). Still open: multipart/form bodies, static-file serving, middleware chain -- add as needed.
**Wire DNS into client/pool (done 2026-09-04)**: ConnectionPool gained hostname constructors (host, port, net: Resolver*), resolving the name once asynchronously on first request; requests issued while resolution is in flight are parked and dispatched on success, or all failed with "DNS resolution failed: ..." on error (a later reauest retries). resolved) exposes the state and queuedRequests) counts DNS-parked requests. Httpclient inherits this via matching constructors (plaintext + TLS), and MultioriginPool: :setResolver() makes its per-origin sub-pools accept hostnames. Numeric origins are unchanged and pay nothing for the DS path. The Host header defaults to the DNS name, not the resolved IP.
### Logging module - status
- **Formatters (done)**: the Formatter abstraction is wired end-to-end -
"Logger" holds a Formatter
and every record goes through it.
DefaultFormatter (human-readable) and JsonFormatter (log aggregation, with string escaping) are both available via "Formatter: :newefaultFormatter() /
*newJsonFormatter() •
-   **Structured fields (done)**: "LogRecord' carries "LogFields" (key/value pairs), each level has a (message, fields) overload, and JSON output emits them as top-level members. This is the future home of trace_id" (see 0AM > distributed tracing). Covered by "tests/logger_test.pc.
-   **Sinks (done 2026-09-05)** ("logging/sink. {h,cc}): a Sink" abstraction separates DESTINATION from APPEARANCE (Formatter), so "JSON to a rotating file" needs no new class - splog's split. Implementations: StdoutSink, FileSink, RotatingFileSink (size-based roll, keeps N files, bounding disk to ~max_bytes × (max_files+1)), MultiSink (fan-out), Memorysink (tests).Formatters emit no trailing newline; the sink owns the terminator.Default-constructed Loggers share ONE process-wide stdout sink, since the common pattern is a static Logger" per TU. Fatal (configurable via" setFlushLevel") flushes immediately, so a crash line is not lost - glog's behavior.
-   **Async logging (done 2026-09-05)** (logging/async_sink. {h, cc}"): muduo AsyncLogging-style DOUBLE BUFFERING - producers append under a very short lock, the writer thread swaps in an empty buffer and does, all I/0 with the lock released. Producer cost is an append plus an occasional swap, never a syscall or a wait on disk. The queue is BOUNDED and DROPS when full with a counter: blocking a producer would stall the event loop (the very thing being avoided) and unbounded growth turns a logging burst into ooM. Consistent with Cox's shedding elsewhere (ThreadPool: tryRun, accept-rate shedding) and Fluent Bit's mem buf_limit. Drops are ACCOUNTED FOR, not hidden: " droppedcount() plus a "dropped N log line(s)" summary when the writer catches up. stop() drains pending lines so shutdown loses nothing.NOTE: this changed the test seam - the old tests captured std: cout, which sinks bypass; "tests/logger_test.cc" now asserts through a MemorySink (19 cases).
- **Remaining (deferred)**: no syslog sink; rotation is size-based only (no daily/time-based, no compression of rolled files); no compile-time-eliminated Log macros, so arguments are evaluated even when the level filter drops the record.
-**Nul1sink （planned; triggered 2026-11-03 by load-validation #6）**： a'Sink'
that discards all output (" Sink: :newNullSink()"), the /dev/null analogue (spdlog's null_sink). Lets "c10k_server --quiet silence the attack-scenario target at the source instead of redirecting a firehose to a file. Trivial; the only design call is deliberately NOT short-circuiting the formatter (would couple Logger to a concrete Sink and break the Formatter/Sink split). See docs/insights/features/logging.md for the when-to-use-which-silencer table (NullSink vs Dedupsink vs stdout redirect). NOTE: the sink writes to STDOUT, so a bare "2>log" does not capture it - redirect •>1og 2>81*
**Flood suppression (planned; triggered 2026-11-03 by load-validation #6)**: a sink-level throttle collapsing duplicate/similar events - keyed on (level, call site) rather than the full string, since a flood of the SAME event carries varying parameters (eg. per-connection "handleError ... reset by peer" lines differ only by connection name. First occurrence per window prints; the rest are counted and summarized ("suppressed N similar '<func›' messages"), reusing the TokenBucket + drop-and-account pattern already in flowcontrol/ and AsyncSink. Implemented as a decorator "Dedupsink" in front of "Asyncsink" so a flood never even enters the bounded async queue. Design detail in docs/insights/features/logging.md. Effort: moderate; the one core change is a call-site key on LogRecord.
-
**TRACE 10g level (add-when-there's-content)**: a level below DEBUG for per-operation verbosity. DELIBERATELY NOT ADDED YET - DEBUG covers current needs and there are no log statements verbose enough to warrant a lower tier.
Trivial to add later (prepend Trace to the enum + a 'trace() method, no migration cost). TRIGGER: hot-path/per-operation diagnostics worth gating, e.g. the HTTP layer (per-request parse steps) or TLS record-level tracing.
## Positioning vs other libraries (muduo, Asio, libevent, Netty, nginx/Envoy)
A grounded comparison of what Cox actually is: a muduo-class C++20 reactor (single-threaded-per-loop, one-loop-per-thread pool, callback-based) with an unusually complete production-hardening story for its size, plus TLS, HTTP/1.1, HTTP/2, DNS, metrics, and a benchmark suite.
› The original assessment (gap analysis) predated several features. Items it
  >listed as missing are now DONE and annotated below: benchmarks, an HTTP /
  >HTTP/2 protocol layer, built-in metrics/observability, and TS SNI / session
>resumption / OCSP. The durable (still-accurate) strengths and weaknesses
>follow.
### Advantages
1. **Production-hardening breadth, unusually complete for the maturity.** Most reactors at this stage give you the event loop and TCP and stop. Cox already has idle/write/max-age timeouts, ingress + egress backpressure, a max-connections cap, accept-rate shedding, an overload-notification callback, graceful drain, SIGPIPE handling, an EMFILE guard, and client-go-style token bucket + jittered backoff + concurrency limiter. That defense set is closer to nginx/Envoy operational thinking than to a typical hobby reactor. This is Cox's clearest differentiator.
**clean modular layering.** Separate reactor/, buffer/, "net/,
" concurrent/, "flowcontrol/, "tIs/. Flow controly and the blocking-work pool are isolated, reusable modules, not tangled into the connection class - where Boost.Asio and muduo do not cleanly separate flow control at all.
*  *Modern C++20, small surface, readable.** No macro soup or TMP maze; readable in an afternoon. An advantage over Boost.Asio (Large, template-heavy) and Netty (JVM, huge).
*  *Testability discipline.** The TLS state machine is tested without sockets (memory-BIO isolation), concurrency invariants are tested under threads, and there are real end-to-end socket tests - broader than many small reactors.
*  *Design decisions are documented.** The insights/ docs capture *why*, not just *what*. Rare for a library this size and valuable for maintainers.
6. **Dual-stack + Unix domain from one code path.** IPV4/IPv6/UDS unified in
"InetAddress"
reusing the entire connection stack.
### Disadvantages
1. **No production track record.** muduo, libevent, Boost.Asio, and Netty have a decadet of battle-testing across countless deployments. Cox has tests (and now adversarial load validation, see "Completed (hardening)") but zero production mileage - the biggest honest gap.
2
**Linux-only, by deliberate choice.** Boost. Asio, libevent, and libuv are cross-platform; Cox commits to Linux primitives (eventfd, timerfd, epoll, inotify). A "KQueuepoller" exists but the rest is Linux-committed.
3. **No coroutine/async-await ergonomics.** Deliberately discarded (see
Discarded). A usability gap vs. Asio (C++20 coroutines), tokio, and Netty futures; complex multi-step protocols are hand-written callback state machines.
4. **Coarse locking in some primitives.** A single std: mutex" per token bucket and per thread-pool queue is correct but would need sharding or lock-free variants under extreme contention. Not yet done.
5.    **protocol ecosystem still narrow.** Cox now ships HTTP/1.1 and HTTP/2, but not the breadth of codes (WebSocket, gRPC, etc.) that Netty/libevent ecosystems offer; application protocols beyond HTTP are DIY.
### Resolved since the original gap analysis
-   **Benchmarks** - DONE (see "Completed (benchmarks & logging)"): an echo throughput/latency benchmark and a buffer micro-benchmark now exist, so the"high performance" claim is measured (relative, machine-dependent) rather than asserted. This was called out as the single highest-value credibility fix and has been addressed.
-   **Protocol layer** - DONE: HTTP/1.1 + REST and HTTP/2 (opt-in) now exist, soCox is no longer raw TCP/TLS only.
-   **observability** - DONE: a metrics module with Prometheus exposition and opt-in adapters now exists (was "deferred, hooks only").
-   **TS depth** - DONE: SNI cert selection, ALP, OCSP stapling, and session resumption/tickets now exist (was "basic: handshake + encrypted I/O only").
### Head-to-head summary
I vs. | Cox advantage | Cox disadvantage |
| muduo | Built-in flow/overload control, TLS, graceful drain, HTTP/2, docs | Less battle-tested; muduo has a book + years of use 1
| Boost.Asio | Simpler, readable, hardening built-in | No coroutines, Linux-onlyl smaller ecosystem, unproven | 1 libevent/libuv | Higher-level (connection mgmt, hardening); modern C++ | Not cross-platform; no C ecosystem/ bindings; unproven |
Netty | Native C++ (no JV), lighter | Smaller protocol ecosystem, no async ergonomics, tiny community | Inginx/Envoy|Embeddable library,muchsmaller|Those are completeservers with adaptive OLP and richer observability |
###
Bottom line
Cox's sweet spot is an embeddable, readable, Linux C++20 networking core for teams who want a muduo-style reactor with production-safety features already built in. Its hardening/flow-control breadth genuinely exceeds most libraries at comparable size. Its remaining weaknesses are the classic young-library ones: no production track record, Linux-only, no coroutines, a still-narrow protocol ecosystem, and coarse locking in a few primitives.
## Discarded
-**UDP**: dropped dropped by decision. Cox focuses on stream transports (TCP + Unix domain). Datagram support can be revisited if a concrete need arises; it would be an independent channel-based UdpSocket, not affecting the stream stack.
**C++20 coroutines**: discarded (2026-09-04). They address async-I/O ergonomics, not blocking/CPU work; dropping them costs no capability (API stays callback-based, as in muduo/libevent/Netty core) and avoids coroutine frame-lifetime hazards. Rationale in docs/insights/features/reactor-blocking-work.mo
# Completed (CI/CD & build hygiene, started 2026-09-05, done 2026-11-03)
*   Detailed reference: [ci-cd.md](ci-cd.md) - the Googletest/CTest/CI layering,
*   MakePresets, sanitizer and coverage presets, and build/test hygiene.
Engineering scaffolding rather than product features. Deliberately parked at the end of the roadmap: none of it adds capability, but items 1-3 protect every capability already built. Findings from a build/infra audit on 2026-09-05; all eight items are done. Small follow-ups noted inline: no coverage threshold is enforced yet (#6), the format job is not blocking by decision (#8), and the socket tests use fixed ports (#3, KNOWN CONSTRAINT).
**warning flags were declared but never applied - FIXED 2026-09-05.**
*  CMakeLists.txt" defined a careful CXX_FLAGS" list ("-Wall -Wextra -Werror
-  wold-style-cast -Wshadow -woverloaded-virtual -wpointer-arith
-  Wwrite-strings") but the line applying it was COMMENTED OUT, so the only warning flags reaching the compiler were -wendantic and
*   -wno-unknown-warning-option
• "-Wpendantic was a TYPO for "-wpedantic that
survived only because -Wno-unknown-warning-option suppressed the complaint
- so pedantic warnings were ON under GCC and OFF under Clang, the compiler
"build.sh actually uses.
Now applied via "add_compile_options, with -Werror" behind a "COX_WERROR" option (default oN) so the policy cannot silently rot again, and the typo fixed. Result: ZERO warnings under clang AND GCC, Debug and Release, with tests and examples.
Fixed 8 Clang warnings (all dead code: unused role members in TIsEngine/ TlsConnection, redundant ttl_s" capture, dead "waitReadable helper, unused
"volatile benchmark vars) and - notably - 4 more that ONLY GCC reported, because of the typo divergence: a real format-string bug in timestamp. cc ("%061d" applied to a "uint64_t', plus a 32-byte buffer GCC could not prove sufficient) and 3"-Wold-style-cast' errors from casts inside OpensSL's own macros （suppressed narrowly with#pragma at the 3 call sites rather than
weakening the flag) .
The two unused "log() helpers were WIRED UP rather than deleted: DNS resolution failures and both malformed-request 400 paths were failing silently and now log.
NOTE: deliberately did NOT enable the other three entries from the old list.
-  march=native™non-portable binaries), -rdynamic (link-time) and
-  stdlib=libc++* (ABI choice) are not warning settings; they belong in a toolchain file or per-target if wanted.
2. **CTest registration - DONE 2026-09-05.** enable_testing() in the root plus a cox_add_test) helper in "tests/cMakeLists.txt replacing 36 hand-written
"add_executable / target_link_libraries pairs. Uses gtest_discover_tests)*
so CTest sees individual TEST() cases (177 entries
from 36 binaries) and can schedule, filter and time out per case.
Binaries that shell out to the 'openssl" CLI or bind fixed ports are registered OPAQUE (one entry, no build-time discovery): the TLS/OCSP/HTTPS tests and doc_snippets_test. Per-test timeouts (120s, 300s for slow ones) are safety nets against hangs, several tests binding real sockets.
Payoff: "ctest -j8" runs the whole suite in ~8 SECONDS vs. minutes for the sequential bash loop it replaces. Also isolated googletest from cox's warning flags (clear "COMPILE_OPTIONS" before "add_subdirectory, restore after) -
•-Werror on vendored code means a compiler upgrade can break our build for reasons unrelated to our sources.
*#Sanitizers - DONE, ALL CLEAN 2026-09-05.**
*  COX_SANITIZE-OFF |address | thread | undefined| leak added (single-valued: ASan and TSan runtimes conflict, so they cannot be combined). Includes
*  -fno-omit-frame-pointer and "-fno-optimize-sibling-calls" for usable stack traces, -fsanitize-address-use-after-scope for ASan, and
-  fno-sanitize-recover=all for UBSan (which otherwise only warns).
Result: **ASan/LSan, TSan and UBSan each 177/177 with zero findings.** The sanitizer CI jobs are therefore BLOCKING, not informational.
This is the item that justified the whole CI/CD effort: it found SIX real defects the 177-test suite passed straight through, because tests verify behavior, not memory safety or the absence of races.
**TSan (was 38 reports / 15 failing tests):**
-**EventLoop wakeup-fd race (real bug).** quit()/ queueInLoop() call * wakeup()
-  > write(wakeup_f_) from any thread, while "~EventLoop"
*   close()is that fd on the loop thread. Writing to a closed descriptor is notmerely useless -- the number can be recycled by the os, so the write could Land on an unrelated socket. Fixed by guarding "wakeup_fd_" with the existing task mutex_" and setting it to -1 under that lock before closing, so a late wakeup becomes a no-op. This one race accounted for 12 of the 15 failing tests.
-   **Connector heap-use-after-free (real bug).**
*  Connector: removeAndResetChannel() used "runInLoop" to "defer" destroying the channel it was currently inside a callback of -- but runInLoop runs the task INLINE when already on the loop thread, which is exactly this case, so nothing was deferred: the Channel was freed inside its own callback and
*   Channel::handleEventsuarded then read revents on freed memory. Fixed by making "queueInLoop" public (it always queues) and using it here. The distinction is now documented on both methods, since this is an easy trap to repeat.
-   **Test bug:** "EventLoopBenchmark.ConcurrencyPerformance" published an"EventLoop*" from the loop thread to main via a plain pointer and a sleep-poll loop. Made atomic.
**ASan/LSan (was 12 leaking tests):** all four remaining findings were
*   shared_ptr REFERENCE CYCLES, not missing deletes. The signature is distinctive and worth remembering: many *indirect* leaks with No direct leak.
-   "PooledConn" owns its "TcpClient, whose callbacks captured a strong " self.
-   Pooledconn owns its TlsConnection, same pattern.
-   "ConnectionPool:: Imp1" owns the connections, and "wirecallbacks" gave each connection a strong ref back to Impl. This was the root that kept the whole pool alive.
ConnectionPool: :Imp1"'s **repeating** eviction timer captured a strong self and was never cancelled, so the pool leaked by construction.
- "TIsconnection owns conn"
whose callbacks captured a strong self -
~900KB across ~12900 allocations in the HTTPS tests, dominated by OpenSSL state hanging off the unreclaimed engine. Fixing this ALSO cut https_test from **6 minutes to 0.23 seconds** under ASan: the leaked TLS state was what made it crawl.
All fixed with "weak_from_this() + "lock() at the point where an owner installs a callback on something it owns, plus explicit eviction-timer cancellation in ~Connectionpool.
- Also: TcpConnection: :connectDestroyed() did not cancel its armed idle / write / max-age timers, whose callbacks hold shared from this();
'handleclose() already did. An armed timer on a stopping loop kept the connection alive. Now cancelled on both teardown paths.
- Also: ~Tcclient tore the connection down but never released its own connection member nor stopped the connector on that path.
NOTE: Test timeouts are raised automatically when a sanitizer is enabled (300s/600s vS 120s/300S) -- instrumented TLS tests are far slower, and CI should not report spurious timeouts.
NOTE: two timing tests were flaky under ctest -j8 (both pass in isolation):
"TepServerTest. IdleTimeoutClosesSilentclient" and
"HardeningTest.MaxConnectionAgeRecyclesConnection check a 1s timeout with a ~1.5-25 bound, which slips when the parallel suite loads the machine. Bounds widened.
KNOWN CONSTRAINT: the socket tests use FIXED ports, so two preset suites run CONCURRENTLY on one machine collide (observed as spurious failures/aborts when several ctest --preset runs overlap). Each preset is fine on its own; CI runs them on separate runners so this does not arise there. Locally, run presets sequentially. A proper fix is ephemeral port allocation per test, which is a larger change to the test harnesses.
1. **Installable / consumable package - DONE.** cmake/CoxInstall. cmake : each module calls cox_install module(‹target› HEADERS ..•)* (BUILD/ INSTALL_INTERFACE include dirs, shared "CoxTargets" export set, headers installed flat under include/cox/ preserving subdirs like "poller/"), and the root calls cox_install_package) once (export set, generated config +
"SameMajorVersion version file). COX_INSTALL-OFF" suppresses it for parent projects embedding cox via "add_subdirectory; in-tree code links the same cox:: <module› ALIAS targets an external consumer does.
NAMING (decided 2026-11-03): the project stays "cox, but the PACKAGE is lowercase so consumers write find_package(cox) and lik cox:: <module› (EXPORT_NAME strips the internal cox prefix: cox_http - cox::http).
Make's package lookup is case-sensitive on the config file/dir, so the install lays down "lib/cmake/cox/coxconfig.cmake (+ coxconfigversion. cmake
"coxTargets.cmake"). The config re-finds Threads + OpensSL before including the targets. (The old note about a cow prefix is obsolete - the prefix is
•cox and is hidden behind the cox:: namespace.)
Verified: make --install to a temp prefix, then an out-of-tree consumer with find_package(cox 0.1 REQUIRED) + cox:: tls cox: net cox:: reactor configured, built and ran.
**Housekeeping.** - see item 8 (done).
## Positioning so
**Coverage measurement - DONE. ** scripts/coverage.sh" + a "coverage" preset: Clang source-based coverage (one"
•profraw"
per test process via
"LLVM_PROFILE_FILE=%p-%m, so forked helpers don't clobber each other), merged with "llvm-profdata, reported per file with "llvm-cov over Cox's own sources only (third_party/tests/googletest excluded); ^--html writes a browsable report under •build/coverage/html. Wired into CI as a NON-BLOCKING coverage (11vm-cov)" job. Still open: no coverage THRESHOLD İS enforced - add one once a baseline is agreed.
**CMakePresets.json + CI workflow - DONE 2026-09-05.**
"CakePresets. json defines the matrix (clang-debug, "clang-release, goc-debug, gec-release
• asan, tsan" , "ubsan"
, "examples"), each with
its own build/‹preset›/ dir so configurations neither clobber each other nor force full rebuilds. •github/workflows/ci.yml" is a thin loop over those preset names and contains No build knowledge, so the same three commands run locally and in CI - and a Jenkinsfile could invoke them unchanged.
"build.sh" rewritten to delegate to presets (it previously did "rm -rf in the build dir, forcing a full rebuild every time while keeping its flag interface.
CI jobs: the 4-config matrix (blocking, "fail-fast: false" so one failure does not hide the others, an examples/benchmarks compile job (examples are outside the default build and can otherwise rot silently), and NON-BLOCKING sanitizer and clang-format jobs. c-ares is installed in CI so CaresResolver is actually exercised rather than only the ThreadPool fallback.
Verified: all four configurations build and pass - 177 tests in Debug, 175 in Release (2 are "#if defined (DEBUG)), under both clang and GCC.
FOUND WHILE DOING THIS: 16 more •-Werror=unused-result failures, visible only in GCC *Release* because "-D_FORTIFY_SOURCE marks "write / read / system warn_unused result in optimized builds. All in test code driving I/O for setup. Added tests/test_util.h (" sendAll/ drainsome / runcd) so each call site states its intent - assert-on-failure for setup writes, explicit tolerate-and-discard for teardown drains - instead of silently dropping a result that would render surrounding assertions meaningless.
8. **Housekeeping - DONE 2026-09-05. ** Added a root •gitignore (per-preset build dirs, sanitizer/coverage artifacts, editor droppings) and a
•clang-format" encoding Google style with the project's actual 4-space indent, so the README's style claim is checkable rather than aspirational.

cox_ev_leftovers. json turned out to be a ~6MB saved AI-assistant conversation transcript, not project data, and was never tracked by git; it is now gitignored rather than deleted (it is not mine to remove).
FORMATTING - DONE 2026-11-03 (guide provided). The local workflow is documented in README > Code Style › "Format on commit": copy the shipped
*  git_hooks_pre_commit.sample to git/hooks/pre-commit. On commit it formats the staged cc/ h files (submodules excluded) and re-stages them;
*  COX_NO_FORMAT=1" skips it for one commit. Verified in a throwaway clone: a badly formatted staged file was committed formatted, and the bypass left it untouched. Also fixed the sample's submodule lookup, which galled a non-existent git config --get-submodule.path option and failed silently; it now uses --get-regexp '^submodule\..*\.paths"HOST NOTE: on machines where "core.hooksPath" is set system-wide (as on the current dev host, /etc/git-setup/git-hooks*), git ignores git/hooks/.That host config must not be changed, so the guide gives the alternatives.CONFORMANCE - DONE 2026-11-03 (clang-format 18): all 160 tracked sources (third_party/ excluded) pass clang-format --dry-run --Werror. The last holdout, "tls/tls_context.cc*, was reformatted (formatting-only; build and full suite 215/215 green afterwards). The 2026-09-05 figure (~3800 replacements / 151 files) is obsolete.NOTE: it's not blocking. The CI "format" job deliberately keeps"continue-on-error: true (decision 2026-11-03), so a formatting regression is reported but does not fail the pipeline; the pre-commit hook above is the primary guard.
