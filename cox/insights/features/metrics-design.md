
# Design: Metrics & observability Module (P1)

> **Status:** core + full built-in instrumentation implemented. The `metrics/`
> module (`cox_metrics`) provides Counter/Gauge/Histogram (54), the Registry +
> format-neutral `Snapshot` + scrape-time filtering + sampled instruments +
> Prometheus exposition (55), and adapters: `installserverMetrics()` (accepted/
> live/closed/rejected + bytes in/out summed on close), `installhttpMetrics()`
> (requests by status class + handler-duration histogram + parser rejections by
> reason), `installLoopMetrics()` (loop iterations + poll-wait, sampled live),
> `installThreadPoolMetrics()` (tasks run/shed + queue depth),
> `installlsMetrics()` (handshakes ok/failed) and `installdnsMetrics()`
> (Lookups ok/failed). Every metric in the §6 table is now emitted. All via
> public hooks / core accessors, so the core libraries have no dependency on
> metrics. Covered by `tests/metrics_test.cc` (22) and
> `tests/metrics_server_test.cc` (9 integration), ASan/TSan clean. Rollout
> steps 1-4 (§11) are done. REMAINING: the optional per-loop sharded Counter
> (step 5, only if profiled) and the optional openTelemetry adapter (§9,
> step 6).

## 1.Purpose

Make Cox's behavior measurable. Today the library exposes hooks
(`setoverloadcallback`, connection/close callbacks, the `Logger`) but no
quantitative state: there is no way to answer "how many connections are live",
"what is the accept reject rate", "what is the thread-pool queue depth", or
"what is request latency p99" without the application bolting on its own
counters. The roadmap repeatedly notes the tunable limits (accept rate,
concurrency, buffer caps) are "only tunable with measurement" - this module is
that measurement.

Every limit cox already ships has a knob that cannot be set intelligently
without the matching measurement - you cannot tune what you cannot see:

| Feature | The knob | Question metrics answer |
|---|---|---|
| `setMaxConnections` | the cap | How many connections run? Are we near the cap? |
| `setAcceptRateLimit(qps, burst)` | aps / burst | What is the real accept rate? How often do we shed? |
| `ConcurrencyLimiter` | max in-flight | what is the actual concurrency and its p99? |
| idle timeout | seconds | How many connections get reaped? |
| high-water-mark | backpressure | threshold | How often do we hit it? |
| `ThreadPool` | workers, queue size | How deep is the queue? Are tasks rejected? |


This is why metrics is the prerequisite that makes overload control operable:
setoverloadcallback is effectively a metrics hook waiting for a metrics system.
Of the three observability pillars - metrics, logging (Logger" already exists),
and tracing (a distributed-systems concern, out of scope for a single library) -
metrics is the one that matters for a network library and the one that was
missing.

Scope boundary (unchanged from the OaM decision): Cox provides the *primitives*
and *instrumentation*; it does NOT embed an agent, a push client, or a
time-series database. Exposition (Prometheus scrape, OTLP push) is an optional
thin layer the application opts into.

## 2. Background / prior art

- **Prometheus client model**: Counter, Gauge, Histogram, Summary + a Registry;
  pull-based text exposition. This is the de-facto model and the one most ops
  stacks already consume. We adopt its vocabulary.
- **Opentelemetry metrics**: richer (views, exemplars, async instruments) but
  heavy (protobuf/gRPC). Out of scope for the core; an adapter can map Cox
  metrics to oTel later.
- **folly/Envoy stats**: thread-local sharded counters merged on read, to keep
  the hot path lock-free. We borrow the sharding idea for Counter/Gauge.

## 3. Design principles

1. **Zero cost when unobserved, near-zero when observed.** Incrementing a
   counter on the I/0 hot path must not take a lock or a cache line others
   contend on. Use per-thread (or per-loop) shards summed only at scrape time.
2. **No I/O in the library.** The registry produces a snapshot/string; it never
   opens a socket. The optional HTTP exposition endpoint is wired by the
   application using the existing `Httpserver`.
3. **Pul1 by default.** A `Registry::scrape()` returns the current values;
   exposition formats are serializers over that snapshot.
4. **Explicit registration, stable identity.** A metric is (name, label-set).
   Instruments are created once and held; no map lookup on the hot path.
5. **Match Cox's existing concurrency discipline.** Readers (scrape) run off
   the hot path; writers never block. Consistent with AsyncSink / ThreadPool
   shedding philosophy.
 
## 4. The instrument types (the framework you asked about)

All instruments live in a new `metrics/` module (`metrics/` static lib,
`cox_metrics`, wired like the others and installed via `cox_install_module`).

### 4.1 Counter - monotonically increasing

```cpp
namespace metrics {
class Counter {
public:
	void inc(uint64_t n = 1) noexcept;   // hot path: relaxed atomic add
	uint64_t value() const noexcept;     // scrape path: sum of shards
};
}
```

- Semantics: only goes up (resets only on process restart). Rate is computed by
  the scraper (Prometheus `rate()`), not here.
- Implementation: start with a single `std::atomic<uint64_t>` and
  `memory_order_relaxed` for `inc` (correct for a counter: no ordering needed,
  only atomicity). If profiling shows contention on a hot counter (e.g.
  bytes-in on a busy multi-loop server), upgrade to per-loop shards summed at
  scrape - see §7. Start simple; the atomic is almost always enough.
- Use for: accepted/rejected/closed connections, bytes in/out, requests by
  status class, thread-pool tasks run/shed, DNS failures.

### 4.2 Gauge - a value that goes up and down

```cpp
class Gauge {
public:
	void inc(int64_t n = 1) noexcept;
	void dec(int64_t n = 1) noexcept;
	void set(int64_t v) noexcept;
	int64_t value() const noexcept;
};
```

- `std::atomic<int64_t>`, relaxed. `set` is for sampled gauges (e.g. queue
  depth read at scrape time; inc/dec for tracked gauges (live connections).
- Use for: live connections, in-flight requests (mirrors ConcurrencyLimiter),
  thread-pool queue depth, armed timers.

### 4.3 Histogram - distribution via fixed buckets

```cpp
class Histogram {
public:
	explicit Histogram(std::vector<double> upper_bounds); // ascending, no +Inf
	void observe(double v) noexcept;   // hot path: find bucket, bump counter
	struct Snapshot { std::vector<double> bounds;
					  std::vector<uint64_t> counts;  // cumulative, Prometheus-style
					  uint64_t count; double sum; };
	Snapshot snapshot() const;
};
```

- Fixed explicit buckets (Prometheus classic histogram), NOT a dynamic
  summary/quantile estimator: bucket counters are cheap atomics, mergeable
  across shards, and quantiles are computed by the scraper. Summaries (local
  quantiles) are deliberately rejected - they are not aggregatable across
  instances.
- `observe`: linear/binary search over bounds (bounds are small, ~10-12), then
  a relaxed atomic increment on the chosen bucket plus `count` and `sum`.
- Provide a default bound set for latency in seconds (e.g.
  `{.0005,.001,.0025,.005,.01,.025,.05,1,.25,.5,1,2.5,5,10}`), and a helper to
  build exponential/linear bounds.
- Use for: request latency, handshake duration, accept-to-first-byte.

### 4.4 Labels

A metric identity is name + ordered label set. Two options; recommend **(a)**
for v1:

(a) **pre-declared label values → distinct instruments.** The caller creates
    one instrument per label combination it cares about and holds the pointer.
	e.g. `requests_total{status="2xx"}` is a `Counter*` obtained once from the
	registry. No per-observe map lookup. Simple, fast, and sufficient for the
	low-cardinality labels Cox emits (status class, protocol, result).
(b) A `Family<Counter>` with `.labels({"2xx"})` returning/creating a child.
    More ergonomic but needs a hot-path-safe map. Defer to y2 if needed.

## 5. Registry, snapshot & exposition

The registry exposes a **neutral, structured snapshot** as its single read
seam; every exposition format (Prometheus, OpenTelemetry, a test matcher) is a
pure function of that snapshot. This two-layer split is deliberate: it keeps the
instruments and hot path independent of any wire format, so adding an exporter
(see §9, OpenTelemetry) never touches instrument code.

```cpp
// Neutral, format-independent view of one instrument at a point in time.
struct MetricPoint {
	enum class Type { kCounter, kGauge, kHistogram };
	std::string name;
	std::string help;
	Type type;
	Labels labels;
	// For counter/gauge:
	double value = 0;
	// For histogram (cumulative, Prometheus-style; an exporter may convert):
	std::vector<double> bucket_bounds;   // ascending, excludes +Inf
	std::vector<uint64_t> bucket_counts; // size == bounds. size() + 1 (+Inf)
	uint64_t hist_count = 0;
	double hist_sum = 0;
};
using Snapshot = std::vector<Metricpoint>;

class Registry {
public:
	Counter&   counter(std::string name, std::string help, Labels = {});
	Gauge&     gauge(std::string name, std::string help, Labels = {});
	Histogram& histogram(std::string name, std::string help,
						 std::vector<double> buckets, Labels = {});
	
	// THE read seam: a point-in-time, format-neutral snapshot. Thread-safe;
	// takes the registry mutex only to iterate the instrument list, never on
	// the hot path. All exporters are built on this.
	Snapshot scrape() const;
};

// Exposition = pure serializers over a Snapshot. No I/O, no registry coupling.
namespace expo {
std::string prometheus(const Snapshot&);   // text exposition format 0.0.4
}  // namespace expo
```

- The registry OWNS the instruments (stable addresses - return references).
  Creation takes a mutex; it happens at startup, not on the hot path.
- `scrape()` walks instruments and fills `Snapshot`. `expo::prometheus()` emits
  `# HELP` / `# TYPE` / samples from it - a pure serializer.
- A process-wide default registry (`Registry::global()`) mirrors the Logger's
  default-instance convenience, with explicit registries allowed for tests.
- **why not return a string directly:** a format-specific string would force
  every new exporter (oTel, StatsD, a test assertion) to either re-parse text
  or add a parallel scrape path. The `Snapshot` makes a new exporter ~one
  function, and keeps the hot-path instruments format-agnostic forever.

### 5.1 Scrape-time filtering

`scrape(ScrapeFilter)` selects WHICH instruments a scrape emits. It affects
only the scraper's work (iteration + serialization); the hot path is untouched.
The filter supports a `name_prefix`, an `include_histograms` toggle (histograms
are the costliest to serialize - ~10x a counter), and an arbitrary
`predicate(MetricPoint)` for label matching. Rules are ANded; the no-arg
`scrape()` selects everything.

Motivation: measurement (see §7 - one full scrape is sub-millisecond for
hundreds of instruments, and scrape never contends with the lock-free hot
path, verified at ~300M inc/s with or without a 2ms-interval scraper). So
filtering is NOT needed for performance at normal scale. It exists for two
cases: (a) a frequent lightweight pull (counters/gauges only) alongside an
occasional full pull that includes histograms; (b) multi-tenant endpoints that
expose only a subsystem's `cox_http_*` metrics. Example endpoint honoring a
`match` query parameter:

```cpp
router.get ("/metrics", [&] (const HttpRequest& req, const Pathparams&,
							 HttpResponse* r) {
	metrics::Registry::ScrapeFilter f;
	f.name_prefix = req.queryParam("match");          // e.g. ?match=cox_http_
	f.include_histograms = req.queryparam("hist") != "0";
	r->setContentType("text/plain; version=0.0.4");
	r->setBody(metrics::expo::prometheus(reg.scrape(f)));
});
```

### 5.2 Exposition endpoint (optional, application-wired)

No new server. Document the one-liner:

```cpp
router.get("/metrics", [&](const HttpRequest&, const PathParams&,
						   HttpResponse* r) {
	r->setContentType("text/plain; version=0.0.4");
	r->setBody (metrics::expo::prometheus(registry.scrape()));
});
```

`scrape` runs on the loop thread here; it is o(instruments), bounded and
non-blocking, so that is acceptable. If instrument count ever grows large,
offload to the ThreadPool.

## 6. Instrumentation points (what Cox emits out of the box)

The module ships primitives; a small `metrics/cox_metrics_builtin.*` wires the
core internals to a registry when the application calls
`metrics::installDefault(registry, server)`. Keep instrumentation OPT-IN SO a
plain build pays nothing.

| Subsystem            | Metric                                   | Type      | Source |
|----------------------|------------------------------------------|-----------|--------|			
| Tcpserver accept	   | `cox_connections_accepted_total`         | Counter   | newConnection                |
| TcpServer accept     | `cox_connections_rejected_total`         | Counter   | overload cb / accept limiter |
| TcpConnection        | `cox_connections_live`                   | Gauge     | connect/close cb |
| TcpConnection        | `cox_connection_bytes_in_total`          | Counter   | handleRead |
| TcpConnection        | `cox_connection_bytes_out_total`         | Counter   | send/handlewrite |
| TcpConnection        | `cox_connections_closed_total{reason}`   | Counter   | handleClose / timeouts |
| ThreadPool           | `cox_threadpool_tasks_total{result}`     | Counter   | run / tryun(shed) - `installThreadPoolMetrics` |
| ThreadPool           | `cox_threadpool_queue_depth`             | Gauge     | sampled at scrape |			
| EventLoop            | `cox_loop_iterations_total`              | Counter   | loop() |
| EventLoop            | `cox_loop_poll_wait_seconds`             | Histogram | around poll() |
| HTTP                 | `cox_http_requests_total{status}`        | Counter   | processInput dispatch |
| HTTP                 | `cox_http_request_duration_seconds`      | Histogram | handler enter/exit |
| HTTP                 | `cox_http_rejected_total{reason}`        | Counter   | 400 / 413 paths (reason = bad_request / body_too_large) |	
| TLS                  | `cox_tls_handshakestotal(result)`        | Counter   | handshake done/fail - `installisMetrics` |
| DNS                  | `cox_dns_lookups_total{result}`          | Counter   | resolver - `installonsMetrics` |

Wiring uses the EXISTING hooks where possible (overload cb, connection/close
cb) so most of this needs no change to core classes; the few that do (bytes,
loop iterations, poll wait add a single `inc`/`observe` at a known line.

## 7. Concurrency & performance

- `inc`/`observe` use `memory_order_relaxed` atomics: correct (counters need
  atomicity, not ordering) and cheap. No locks on the hot path.
- Multi-loop contention: the one shared hot counter at risk is per-connection
  bytes if all loops hit the same instrument. Mitigation (only if measured):
  per-loop shards - `Counter` holds `N` cache-line-padded atomics, `inc` picks
  the caller's loop index, `value()` sums. Design the Counter so this is an
  internal change, not an API change.
- Scrape cost is o(instruments), under a short registry mutex that the hot path
  never takes. Histograms snapshot by reading bucket atomics (racy-but-eventually
  - consistent is fine for metrics; no tearing on per-bucket reads).

## 8. Testing

- Unit: Counter/Gauge arithmetic; Histogram bucketing (boundary values land in
  the right bucket, sum/count correct); Prometheus text format golden
  tests.
- Concurrency: N threads incrementing one Counter → exact total (TSan clean);
  the sharded variant if implemented.
- Integration: drive a TcpServer + HTTP server, hit "/metrics, assert
  `cox_connections_accepted_total` and `cox_http_requests_total{status="2xx"}`
  advance.
- Sanitizers: the module must be ASan/TSan clean (hot-path atomics are the
  whole point).

## 9. OpenTelemetry adapter (optional, future)

OpenTelemetry support is an **optional adapter over the Snapshot (§5)**, not a
change to the instruments or hot path - the "thin adapter" model. Cox's own
Counter/Gauge/Histogram stay canonical and cheap; OTel is just another reader +
transport. This is the same pragmatic boundary as elsewhere: the core stays
dependency-free, and the heavy integration is opt-in.

### 9.1 why thin, not deep

Replacing Cox instruments with OTel SDK instruments (Meter'-created) was
rejected: it couples the core to a heavy SDK (protobuf, abseil, gRPC for OTLP),
inherits OTel's background export lifecycle, and compromises principle #1
(zero-cost hot path - OTel instruments are not relaxed-atomic cheap). The thin
adapter keeps all of that out of the core.

### 9.2 Packaging

- Feature-gated: "COX WITH_OTEL* (default OFF), findpackageopentelemetry-cpp
  CONFIG REQUIRED) - vendoring its protobuf/gRPC/abseil tree as submodules is
  a maintenance burden, so prefer a system / vckg / Conan install. This reuses
  the third_party/ feature-gate pattern.
- The adapter lives in its own target (e.g. "cox metrics_otel"), so the core
  cox_metrics never links OTel. Applications that don't want OTel pay nothing.

### 9.3 Model: push loop over pull snapshot

OTel metrics are push/periodic; Cox is pull. Bridge by periodically reading the
Snapshot and handing it to an OTLP exporter:

```
timer (or ThreadPool) every N s:
    Snapshot s = registry.scrape();
    otelExporter.export(map(s));   // MetricPoint -> OTLP data points
```

Run the periodic read off the loop (a timer that offloads to the Threadpool, or
a dedicated thread), exactly like the OCSP auto-refresh pattern - never block
the loop on export I/O.

### 9.4 opentelemetry-cpp architecture (what the adapter targets)

Understanding where the adapter plugs in requires the library's three-layer
split, which mirrors cox's own instrument/snapshot/exporter separation:

- **API ("api/")** - header-only, ABI-stable, no-op by default. A library
  instruments against it without depending on the SDK. Signals: Traces
  (Tracer" / Span*), Metrics (Meter / Counter / Histogram / Gauge'), Logs
  (Logger" / LogRecord"), Context propagation (Baggage, W3C/B3 propagators).
- **SDK ('sdk/")** - the real logic the application links and configures at
  startup: "Meterprovider , processors, samplers, "MetricReader s, "Resource"
  (service.name, host, env), batching, aggregation, config parsing (incl.
  YAML-based declarative setup).
- **Exporters (exporters/')** - serialize and send: OTLP (gRPC/HTTP), oStream
  (console), Prometheus (pull), Zipkin, in-memory (test), ETW, Elasticsearch.

The provider pattern anchors it: a global singleton per signal
(Provider::GetMeterProvider() / SetMeterProvider()), set once at startup
The metrics pipeline is Provider -> Meter -> Instruments -> MetricReader ->
Exporter* where a PeriodicExportingmetricreader" wraps an exporter and pushes
collected metrics on an interval - the push model cox's §9.3 bridge feeds.

The instrument hierarchy the mapping below targets:

```
MeterProvider
  |-- Meter
       |-- Synchronous
       |     |-- Counter<T>        - monotonic (Add)
       |     |-- Histogram<T>      - distribution (Record)
       |     |-- UpDownCounter<T>  - bidirectional (Add, may be negative)
       |     |-- Gauge<T>          - point-in-time (Record) [ABI v2]
       |-- Asynchronous (observable, callback-reported at collection)
             |-- ObservableCounter
			 |-- ObservableGauge
             |-- ObservableUpDownCounter
```

Instruments are created via typed factory methods on `Meter`
(`CreateUInt64Counter`, `CreateDoubleHistogram`, `CreateInt64UpDownCounter`,
`CreateInt64Gauge` [ABI v2], and the `Create*observable*` family). Values are
reported with `Add` (counters/up-down), `Record` (histograms/gauges), or an
`Observe` callback for the asynchronous instruments; all accept attributes as an
initializer list or any key-value iterable. The no-op header (`noop.h`) is what
the API returns until an SDK is installed, which is why instrumenting against the
API alone costs nothing. This typed-factory surface is exactly what $9.1 rejects
for Cox's *core* instruments (it is not relaxed-atomic cheap) but is the right
target for the *adapter* writing OTLP data points.

### 9.5 Semantic mapping (MetricPoint -> OTel)

Cox MetricPoint               | OTel instrument / data point                |
|-----------------------------|----------------------------------------------|
| Counter (monotonic)         | `Counter` / monotonic Sum                     |
| Gauge, tracked (inc/dec)    | `UpDownCounter` (non-monotonic Sum)           |
| Gauge, sampled (`set`)      | async `Gauge` (observed at collection)        |
| Histogram (explicit bounds) | `Histogram` with explicit bucket boundaries   |
| `Labels`                    | attributes (string-keyed)                     |
| (process identity)          | `Resource` ( `service.name`, etc.) set once   |

Notes:
- Cox histograms are cumulative Prometheus-style; OTel wants per-bucket counts
  with bounds. The `MetricPoint` carries both bounds and counts, so the mapping
  is mechanical. Exemplars (trace-linked samples) are out of scope for v1 and
  tie into the deferred tracing work.
- No client-side summaries exist in cox, so there is nothing that fails to map.

### 9.6 Example (what an OTel user writes)

Provide this as a runnable example under `examples/metrics/otel_export.cc`
(built only when `COX_WITH_OTEL=ON`), so OTel users have a copy-paste starting
point:

```cpp
// Build: cmake --preset ... -DCOX_WITH_OTEL=ON
#include "metrics/registry.h"
#include "metrics/otel_exporter.h"   // cox metrics_otel, gated by COX_WITH_OTEL

int main() {
	auto& reg = metrics::Registry::global();
	
	// 1. Configure the OTLP exporter once (endpoint, resource, interval).
	metrics::otelExporter otel({
		.endpoint = "http://localhost:4318",     // OTLP/HTTPcollector
		.service_name = "cox-edge",
		.interval = std::chrono:: seconds (15),
	});
	
	// 2. Start periodic export: reads reg. scrape) every interval on a
	//    background worker and pushes OTLP. Never touches the Loop thread.
	otel.start(reg);
	
	// ... run the server as usual; instruments update on the hot path ...
	
	otel.stop();   // flush + join before the registry/loop tear down
}
```

The same server can *also* expose `/metrics` for Prometheus - the two exporters
are independent readers of one Snapshot, so pull and push coexist.

## 10. Alternatives considered

- **Summaries (client-side quantiles):** rejected - not aggregatable across
  instances; Prometheus histograms + server-side quantiles are the standard.
- **A metrics macro layer** (like the log macros): deferred. `inc()` is already
  terse; macros would add a level guard with no payoff metrics are not level-
  filtered).
- **Deep OpenTelemetry integration** (OTel SDK instruments as the core type):
  rejected - see §9.1. The thin adapter gets OTLP export without the dependency
  and lifecycle coupling.
- **Embedding an exporter/agent：** rejected per the OAM scope decision - the
  library stays I/O-free; exposition is application-wired over `Httpserver`.

## 11. Rollout order (suggested)

1. `metrics/` module + Counter/Gauge/Histogram + unit tests.
2. Registry + `Snapshot` + `expo::prometheus` + golden tests.
3. Built-in instrumentation via existing hooks (accept/overload/connection) -
   no core edits; prove `/metrics` end-to-end.
4. Hot-path counters needing a core line (bytes, loop, poll-wait, HTTP
   latency) .
5. (If measured) per-loop sharded Counter.
6. (Optional, separate) `cox_metrics_otel` adapter behind `COX WITH_OTEL` +
   the `examples/metrics/otel_export.cc` example (§9).

## 12.References

- `net/tcp_server.*` (accept, overload cb), `net/tcp_connection.*` (bytes,
  close reasons), `concurrent/thread_pool.*` (run/tryRun), `reactor/event_1oop.*`
  (iterations, poll wait), `http/http_server.*` (requests, rejects, latency).
- features/flow-and-overload-control.md (the limits these metrics make
  tunable), roadmap.md (OAM deferral this implements).
