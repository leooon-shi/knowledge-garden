<!-- cox/docs/performance/hardening-validation.md -->
# Hardening Validation - Load & Adversarial Scenarios
This is the executable companion to
[docs/insights/features/load-validation-design.md](../insights/features/load-validation-design.md
The design doc explains *why*; this doc is *how to run it* - the exact commands, the tools, and the pass/fail criteria that turn the hardening features from
"implemented and unit-tested" into "proven under adversarial load."
*   Status: **All scenarios S1-S8 implemented and reproducible** (2026-11-03).
*   Runs are manual on a quiescent host (see Host preparation); a reduced,
*   non-blocking CI smoke tier asserts the containment invariants on shared
*   runners. Findings from building these are recorded inline per scenario and in
*   the roadmap.
## The thesis (what "validated" means)
For each defense, two properties, both **measured** (not asserted):
1.    **Containment** - the targeted resource stays bounded (connections, memory, fds, in-flight work) no matter how hard the adversary pushes.
2.    **Liveness / fairness*% - a well-behaved client is still served with bounded latency *while* the attack runs. A defense that protects a resource by freezing all legitimate traffic has failed property 2.
The signals come from three independent sources, so no single tool's blind spot can hide a failure:
**Cox /metrics ** (Prometheus text) - cox_connections_live, "..._closed_total",
•••_rejected total', etc. The server's own view.
**kernel ss*** - socket/fd counts toward the data port. The OS view, a cross-check on the metrics live count.
-**/proc/‹pid›/status VmRsS** - memory, to catch unbounded growth a counter would not show.
## Tooling
All under ®examples/c1øk/, all standalone (Libc/POSIX or shell; no cox library dependency - a measurement tool must not share code with the server it measures, except c10k_server" itself which IS the target):

| Tool Role |
------
c10k_server" | the target; --metrics-port N° exposes "/metrics on a SEPARATE admin port so a scrape is always reachable even while the data port is saturated. Hardening knobs: •--idle/--max-conns/--accept-qps/--max-age/
--write-timeout/--max-input-buf/--sink, and --work-ms/--max-inflight for the s7 concurrency-limited slow-work mode | I "c1ok_client --attack MODE* | the adversary.
"slowloris + flood"+ "slow-read"+ "big-body" all implemented |
I http_target | HTTP analogue of c10k server for 54: "Httpserver" with "--max-body + /metrics (cox_http_requests_total(status_class)"); "POST/sink consumes a within-limit body |
I probe_client*| liveness: one legitimate request/second against the data port, recording success + latency (fresh
connection each time, so it also exercises accept)) --no-read = accept-path liveness only (sink targets, S3);
•--http* = send a GET and accept any HTTP reply (HTTP targets, 54) |
I sample. sh | containment: per-second cs of "/metrics + VmRSS + "ss" socket count |
### Why a separate metrics port
The telemetry plane is kept off the data port on purpose. Under a flood or slowloris the data port is contended; a scraper (or the sampler) must still be able to read /metrics to measure containment. Putting metrics on the attacked port would make the measurement the first casualty of the attack.
I
#*# Log noise during attacks
Under an attack the net layer emits one line per dropped/reset connection, which floods the terminal. Until the planned log flood-suppression lands (see
•docs/insights/features/logging-md" and the roadmap), **redirect the server's output to a file** in every scenario (2›server.log below) - which you want anyway, to keep the server log separate from the sampler cV and probe output.
## Host preparation
These are not unit tests; they need a tuned, quiescent host.
***bash
ulimit -n 200000
* fd headroom for tens of thousands of connections
# optional, for high connect churn: widen the ephemeral range / tw reuse.
Pick ports that do NOT overlap a running test suite (the socket tests use fixed ports). The examples below use data port 19100 and metrics port 19101.
Build the tools once:
‘’’
cmake --build --preset examples --target c10k_server c10k_client probe_client
-一一
## S1 - Idle timeout vs slowloris
*  *Defense:** TcpConnection" idle timeout (" c10k_server --idle S"), plus the connection cap (--max-conns N° that bounds how many slowloris connections can be held at once.
*  *Attack:** ^c10k_client --attack slowloris" opens N connections and dribbles one byte every *--dribble-ms
• never completing a request.
### The two regimes （important）
Running S1 shows idle-timeout has two behaviours, and naming both is the point:
-***--dribble-ms ›'--idle*** (dribble Flower than the timeout): each connection goes silent longer than the idle window, so the server **reaps it**.
"cox_connections_live" collapses to ~o, ".._closed_total' advances at the connection count. *The idle timeout alone contains the attack.*
*  **--dribble-ms' ‹ •--idle ** (dribble faster than the timeout): every byte resets the idle deadline, so connections are **held**. The idle timeout does NOT reap them - containment then comes from "--max-conns. This is why S1 sets a cap: the honest containment claim is * Live" plateaus at a bound*, not
*  everything gets reaped*. (This is the regime the design doc's 9s-dribble /
10s-idle numbers describe.)
A real slowloris tries to stay in the second regime (just under the timeout), so the cap is the load-bearing defense there and the idle timeout handles the clumsy/stalled attacker. Both are validated below.
### Run
‘’’bash
# Target: idle timeout 3s, connection cap 500, metrics on :19101.
*  /build/examples/bin/c10k_server --port 19100 --metrics-port 19101 \
-  -idle 3 --max-conns 500 --quiet 2›server.1og & SRV=S!
# Containment sampler -> CSV (needs the server PID for VmRSS).
*  /examples/c10k/sample.sh --pid $SRV \
-  -metrics-url http://127.0.0.1:19101/metrics \
-  -port 19100 --count 12 --out s1. csv &
# Hardening Validation - Load & A
# Liveness probe: 1 legitimate req/s against the attacked data port.
•/build/examples/bin/probe_client --port 19100 --count 12 &
# Adversary: 480 slowloris connections (just under the 500 cap), held regime.
•/build/examples/bin/c10k_client--port 19100 --conns 489 --rate 960 \ ttack slowloris --dribble-ms 2000 --duration 12
wait; kill $SRV 2>/dev/null
‘’’

For the reaping regime, flip one number: "--dribble-ms 5000° (> --idle 3°).
### Pass / fail
**Containment (from "s1.csv"):**
-   Held regime (dribble ‹ idle"): "live **plateaus at ~conns** and does not grow monotonically; rss_kib" plateaus (no unbounded growth); sockets' tracks"live" (kernel agrees with the metric). With conns ≥ max-conns, excess connections show as rejected advancing and live capped at max-conns.
-   Reaping regime ('dribble › idle): "live drops to ~ within vidle seconds of each connection's last byte; closed advances to ~ conns.
**Liveness (from the probe summary):**
-   Probe success rate **100%** (0 connect/timeout/io failures).
-   Probe "p99* latency **< 50 ms** throughout (loopback is far under this; the bound exists to catch a server that stalls legitimate traffic).
### observed (study host, loopback, illustrative - calibrate per host)
Held regime, 480 conns, dribble 2s, idle 3s, cap 500:
..٠
t_s, rss.
kib, sockets, live, accepted, closed, rejectea
0.0,5632,22, 32, 33, 1, 0
1.1,7424,960,480,482,2,0
•••
# Hardening Validation -Load & Adversa
10.5,7424,960,480,491,11,0
"live flat at 480, "rss_kib" flat at ~7.3 MİB, "sockets" flat at 960 (480 server + 480 client ends on loopback). Probe: **11/11 ok, p50 274us, max 374us**. Both properties hold.
---
## S5 - max-connections / accept-rate vs flood
**Defense:**
the connection cap (*--max-conns N°) and the accept-rate token
bucket (--accept-qps 0°). Both shed by closing the newly accepted fd immediately and firing the overload callback (→ cox_connections_rejected_total"), which keeps the accept queue moving rather than wedging it.
**Attack:** "c10k_client --attack flood churns connections at maximum rate - up to "--conns connects in flight, each recycled into a fresh connect the instant it resolves (accepted, refused, or reset) - for "--duration seconds.
芥持券 Run
*  ** bash
*  /build/examples/bin/c10k_server --port 19100 --metrics-port 19101 \
-  -max-conns 200--accept-qps 500 --quiet >server.10g 2>81 &
SRV=$!
I
*  /examples/c1ok/sample.sh --pid $SRv
-  -metrics-url http://127.0.0.1:19101/metrics\
-  -port 19100 --count 8 --out s5. csv &
*  /build/examples/bin/probe_client --port 19100 --count 8 --timeout-ms
1000 &
*  /build/examples/bin/c10k_client --port 19100 --conns 400 \
-  -attack flood --duration 8
wait; kill $SRV 2›/dev/null
••。

› Note the '›server. log 2›&1: under a flood the server logs one WARNING per shed
>connection, and the logger writes to STDOUT, so BOTH streams must be redirected or the terminal is buried. (This is the motivating case for the
> planned NullSink / flood-suppression work - see logging.md.)
### Pass / fail
**Containment (from 's5. csv') - PASSES:**
-   "cox_connections_live s--max-conns" at all times (stays an order of magnitude under it: the echo connections are closed immediately by the churn).
-   "cox_connections_rejected_total advances steeply (the shed count).
-   "rss_kib and sockets" plateau - no unbounded growth under sustained churn.
**Liveness (from the probe) - DOES NOT PASS, and this is the real finding:**
- Probe success drops to ~25% with io_fail's. A GLOBAL accept-rate limiter is BLIND to legitimacy: it sheds the probe's connections alongside the attacker's, because it admits by rate alone, with no per-source discrimination. The server SURVIVES (containment is perfect) but legitimate clients are collateral damage.
### Interpretation (important)
S5 validates containment and, deliberately, exposes a FAIRNESS limit:
-   --max-conns alone is fair-ish: it rejects only once the cap is reached, so a legitimate client can still take a freed slot. The failure mode is "full, " not"random victim."
*  --accept-qps* is NOT fair under flood: it rejects by arrival rate regardless of who is arriving, so a 1-req/s probe competing with a 17k-conn/s flood is shed most of the time. It protects the server at the cost of the honest client.
Preserving liveness under a flood needs per-SOURCE admission (per-IP rate limiting, SYN-cookie-style teardown, or an upstream L4 filter) - which Cox does not currently implement and which is out of scope for the present defenses. On a real network with the attack arriving from distinct source IPs, a per-source limiter would shed the attacker and spare the probe; on loopback the two are indistinguishable by source, so this scenario is a worst case for the global limiter. The honest claim S5 supports: **the accept-path defenses CONTAIN a flood (bounded live/fds/memory); they do not by themselves GUARANTEE fairness to legitimate traffic during one.** That gap is a candidate future feature (per-source admission), recorded here rather than hidden by a probe threshold chosen to pass.
### observed (study host, loopback, illustrative)
• --max-conns 200--accept-aps 500°, flood 400-wide for 8s:
t_s, rss.
_kib, sockets, live, accepted, closed, rejected
0.0,6144,787,107,308, 201,463
1.1,6400,812,12, 842, 830, 18563
•••
7.4,6400, 794, 12, 4027, 4015, 125915
...
"live pinned ~12 (« 200 cap), "rejected* → ~126k, RSS flat at 6.4 MiB, sockets ~800. Flood: 138,800 accepted-then-dropped over ~17k attempts/s.
Probe: **2/8 ok** - the fairness limit above.
## S6 - connection max-age recycling
*  *Defense: ** connection lifetime cap (--max-age 5'): any connection older than seconds is force-closed (recycled), independent of activity. Distinct from the idle timeout - max-age reaps a *busy* long-lived connection too, bounding worst-case connection lifetime (useful for rebalancing after a scale-out, or forcing periodic re-auth/cert re-check).
*  *Attack / workload:** not an adversary so much as a soak - c10k_client
--msg e* opens keep-alive connections and holds them idle for --duration.
Idle (not dribbling), so the idle timeout is irrelevant; only max-age should reap them.
### Run
***bash
/build/examples/bin/c10k_server --port 19100--metrics-port 19101 \
--max-age 4 --quiet ›server.log 2>&1 &
Sec=$！
 /build/examples/bin/probe_client --port 19100 --count 13 &
 /build/examples/bin/c10k_client --port 19100 --conns 300 -- rate 600 \
| --msg 0 --duration 14
wait; kill $SRV 2›/dev/null
Watch the reason-labelled close counter advance:
*** bash curl-s http://127.0.0.1:19101/metrics |
I grep cox_connections_closed_total(reason="max_age""
### Pass / fail
**Containment:**
-   cox_connections_closed_totalfreason="max_age"}" advances to ~N as connections cross their age mark - and the OTHER reason series do NOT (only " reason="peer" moves, tracking the probe's own closes). This is the precise attribution the close-reason label buys: it is the *max-age* defense firing, not idle or peer.
-   "cox_connections_live" is bounded (recycled near the age, not growing).
*  *Liveness:** probe 100% ok - recycling does not disturb legitimate traffic.
*  *ASan cross-check (required for "validated"'):** S6 exercises timer cancellation + connection teardown - the exact class of bug the sanitizer saga caught (a leaked TcpConnection" held alive by an uncancelled max-age timer). Run it under ASan at reduced scale and confirm zero leaks:
•**bash
cmake --preset asan -DBUILD_EXAMPLES=ON
cmake --build -- preset asan --target c10k_server c10k_client ASAN_OPTIONS=detect_leaks=1 ./build/asan/bin/c10k_server --port 19100 \
-  -max-age 3 --quiet ›srv_asan.log 2>&1 &
*  /build/asan/bin/c10k_client --port 19100 --conns 200 --rate 400 \
-  -msgø --duration 8
kill -TERM %1
# graceful stop so teardown runs
# srv asan. log must contain NO leak/ERROR Lines.
‘’‘
### Observed （study host, 1oopback）
*   - -max-age 4°
*   300 idle keep-alives:
# Hardening Validation - Lo
t_s, live, closed_maxage, closed_peer
1,300,0,2
3,300，②，4
4,262,38,5
5,0,300, 6
‹- age mark reached, recycling begins <- all 300 recycled by max-age
•••
13，②，300,13
closed{reason="max_age"} → 300 (exactly the held set), "live" → e at the age mark,
peer tracks only the 13 probe closes. Probe 13/13 ok. ASan: clean.
## S2 - write timeout vs slow-read
I
*  *Defense:** the write timeout ("--write-timeout s*): a connection whose output buffer stays backed up past the deadline is force-closed, bounding the memory a slow (or malicious non-)consumer can pin server-side.
*  *Attack:** ^ c10k_client --attack slow-read" sends one request of --msg bytes
per connection and then NEVER reads the reply. The server echoes the (large) response; because the client's receive window fills and TCP backpressures, the server's output_buffer grows until the write timeout fires.
*   Sizing matters: the echo must exceed what the kernel socket buffers absorb, or
*   it never backs up. on this host tcp_wmem autotunes to ~4 Mi, so use
*   *--msg 16777216 (16 MİB) - comfortably over the send buffer - so the unread
*   echo actually accumulates in the application output buffer.
### Run
*bash
*  /build/examples/bin/c10k_server --port 19100 --metrics-port 19101 \
-  -write-timeout 3 --quiet ›server.log 2>81 &
SRV=$!
*  /examples/c10k/sample.sh --pid $SRV \
-  -metrics-url http://127.0.0.1:19101/metrics --port 19100 \
-  -count 8--out s2.csv &
*  /build/examples/bin/probe_client--port 19100--count 8--msg 16 &
*  /build/examples/bin/c10k_client --port 19100 --conns 50 --rate 100attack slow-read --msg16777216--duration 7
wait; kill $SRV 2>/dev/null
### Pass / fail
**Containment:**
-   "cox_connections_closed_total(reason="write_timeout"}" advances to ~N at the deadline - the right defense, attributed via the close-reason label.
-   VmRSS SPIKES while the unread echoes buffer, then COLLAPSES when the timeout force-closes the slow readers and frees the buffers. The spike-then-collapse is the containment story: without the timeout that memory would be pinned indefinitely (an OOM vector).
**Liveness:** probe 100% ok - buffering adversarial output does not stall legitimate traffic.
##* observed (study host, loopback)
I
•--write-timeout 3', 50 slow-readers × 16 MiB unread:
٠٠٠
t_s, rss_kib, live, closed wt
1,212208,50,0
2,711808,50,0
3,711808,50,0
4,19964, 0,50
•。
<-~711 MiB of unread echo buffered server-side
<- write timeout fires: all 50 closed, memory freed
• closed{reason="write_timeout"} → 50, RSS 711 MiB → 20 MiB at the deadline.
Probe 8/8 ok.

### A finding worth recording: the bug was in the HARNESS, not the server 
The first s2 run reaped only 2/50. Investigation showed the cause was the LOAD GENERATOR, not the write timeout: in slow-read mode "c10k_client" was not finishing SENDING its (large) request - the EPOLLOUT continuation was gated to non-attack mode, so after the first partial write the request stalled, the server had little to echo, and its output never backed up. Fixing the client to keep flushing its request (while still never reading the reply) made the write timeout reap all 50.
A tempting but wRoNG turn was taken first: the one-shot write timer was redesigned into a periodic continuous-backup watchdog on the theory that transient drainage was defeating it. That theory could not be demonstrated (no discriminating test distinguished old from new), so the core change was REVERTED
- the original one-shot timer is correct for this scenario. The lesson logged here: validate the measurement tool before "fixing" the thing it measures.
(A genuine but narrow one-shot weakness - a consumer that fully drains the buffer just under each deadline to reset the clock - remains noted as possible future hardening, not acted on without a reproducing case.)
## S3 - max-input-buffer vs unframed flood
**Defense: ** the input-buffer cap (--max-input-buf BYTES"): a connection whose
UNCONSUMED input buffer exceeds the cap is force-closed, bounding read-side memory against a peer that floods faster than the app consumes or sends an oversized/unframed message that never completes a frame.
*  *Target shape:** the cap only fires if the handler LEAVES data in the input buffer - the echo handler drains fully each read, so it never trips. S3 uses
*   c10k_server --sink: a handler that never consumes, simulating a protocol parser waiting for a frame that never completes. (A real framed server would consume complete frames and buffer only a partial one; "--sink is the simplest faithful stand-in for "input accumulates.")
*  *Attack: ** ^ c10k_client --attack slow-read --msg < large› streams a payload far larger than the cap and never reads a reply - effectively an endless unframed body relative to the cap.
### Run
*  /build/examples/bin/c10k_server --port 19100 --metrics-port 19101 \
-  -sink --max-input-buf 1048576 --quiet ›server.1og 2›81 &
SRV-$!
# 1 MiB cap
*  /examples/c10k/sample.sh --pid $SRV \
-  -metrics-url http://127.0.0.1:19101/metrics --port 19100 |
-  -count 8--out s3.csv &
# Sink echoes nothing, so a round-trip probe cannot succeed by construction.
# Liveness here = the accept/read path stays responsive: -no-read success is
# connect+send completing in time.
•/build/examples/bin/probe_client--port 19100 --count 8 --no-read &
/build/examples/bin/c10k_client --port 19100 --conns 50 --rate 100 \
--attack slow-read --msg 67108864 --duration 7
wait; kill $SRV 2›/dev/null
...
### Pass / fail
**Containment:*%
- cox_connections
_closed_total(reason="input_cap") advances to ~N - the input
cap fired, attributed via the close-reason label.
- VmRSS stays FLAT and small (bounded by ~cap × live connections), even though each attacker streams far more than the cap. Without the cap, 50 × 64 MiB
*   3.2 GiB would buffer; with it, RSS held under 6 MiB.
*  *Liveness (accept-path):** "probe_client --no-read" 100% ok - a legitimate client can still connect and send while the flooders are being capped. (Round-trip liveness is not meaningful against a --sink target, which serves no one by construction; see the note below.)
##* Observed (study host, 1oopback)
*--sink --max-input-buf 1048576, 50 × 64 MiB unframed floods:
٠٠.
t_s, rss_kib, live, closed_inputcap
1,5948,0,50
•••
8, 5948,0,50
# Hardening Validation - Load & /
• closed(reason="input_cap"} → 50 immediately, RSS flat at ~5.8 MiB. Probe (--no-read) 8/8 ok.
### Note on liveness semantics for a sink target
A first run with a round-trip probe showed 0/8 - not a server defect, but because --sink echoes nothing to ANYONE, so the probe's reply never comes. The honest liveness claim for S3 is accept-path responsiveness (connect+send stay prompt while flooders are capped), measured with probe_client --no-read. A stronger "other clients fully served" claim would need a FRAMED server that consumes complete frames while capping an oversized partial one - more machinery than the containment claim (the primary S3 result) requires.
---
## S4 - HTTP body limit vs big-body
**Defense: ** the HTTP request body cap (HttpServer: :setmaxBodySize, exposed by
"http_target --max-body BYTES"). A declared "Content-Length over the cap is rejected UP FRONT with "413 Payload Too Large" - WITHOUT reading the body - and a chunked body is rejected once its running total exceeds the cap. This is a per-request protocol limit, distinct from the TCP input-buffer cap (S3).
*  *Target:** "http_target* - the HTTP analogue of c10k_server': "Httpserver with "--max-body and a /metrics admin port exposing
*   cox_http_requests_total(status_class=..) (via installhttpetrics). "POST
/sink consumes+discards a within-limit body; GET / returns 200.
**Attack:** ^c10k_client --attack big-body POSTs with a declared 1 GiB
"Content-Length and streams the body. Against the cap the target answers 413 and closes before reading the body, so almost nothing is uploaded.
### Run
***bash
*  /build/examples/bin/http_target --port 19100 --metrics-port 19101 \
-  -max-body 8388608 --quiet ›server.1og 2>&1 &
# 8 MiB body cap
SRV=$!
*  /examples/c10k/sample.sh --pid $SRV \
-  -metrics-url http://127.0.0.1:19101/metrics --port 19100(
-  -count 7 --out s4. csV &
# Liveness: a legitimate HTTP client. --http sends "GET /" and counts any HTTP
# reply as success (the raw echo probe cannot speak to an WTTP target).
/build/examples/bin/probe_client--port 19100--count 7--http &
*  /build/examples/bin/c10k_client --port 19100 --conns 50 --rate 100 \
-  -attack big-body --msg 1048576 --duration 6
wait; kill $Sv 2>/dev/null
### Pass / fail
**Containment:**
-   cox_http_requests_total(status_class="4xx"} advances (the 413s).
-   VmRSS stays FLAT and small - the 1 GiB bodies are NEVER buffered; the cap rejects on the declared length before reading. Without it a single such POST would buffer up to the whole body.
**Liveness:** concurrent small requests keep returning 2xx
( {status_class="2xx"}" advances) and the '--http probe is, 100% ok - legitimate HTTPtraffic is unaffected while oversized POSTs are rejected.
### Observed（study host,1oopback）
• --max-body 8388608*
50 big-body POSTS (declared 1 GiB each):
t_s,rss_kib, req_4xx,req_2xx
1,6260,372,2
•⋯•
6,6260,372,7
‘’‘
*4xx* → 372 (all rejected 413), "2xx" climbs 2→7 (legit requests served), RSS flat at ~6.1 MiB. Probe (--http) 7/7 ok. No gigabyte buffered.
1-
#S7 - concurrency 1imiter vs heavy in-flight
**Defense: ** "flowcontrol: :ConcurrencyLimiter" caps the number of in-flight units of WORk (not connections - many idle keep-alives are cheap; a few connections each doing heavy work saturate cu/memory). elek server --work-ms N
--max-inflight C* admits each request through the limiter and simulates N ms of work by DEFERRING the echo reply via a timer (so the single loop never blocks), holding an in-flight slot for that duration. Over capacity, requests are SHED immediately with a cheap BUSY\n" reply and hold no slot.
**Workload:#* many simultaneous requests exceeding capacity - c10k_client with more closed-loop connections than the limiter's capacity.
### Metrics
-   "cox_requests_in_flight" (sampled gauge from limiter. inFlight()").
-   cox requests_shed_total (counter of over-capacity rejections).
### Run - containment (over capacity)
*  **bash
*  /build/examples/bin/c10k_server --port 19100 --metrics-port 19101 \=-work-ms 500 --max-inflight 20 --quiet >server.10g 2>81 &
SRV=$!
# 200 closed-loop clients > 20 capacity.
*  /build/examples/bin/c10k_client --port 19100 --conns 200 --rate 400 \
-  msg 64 --inflight 1--duration 6
curl -s http://127.0.0.1:19101/metrics
I grep -E 'cox_requests_in_flight| cox_requests_shed_total' kill $SRV 2>/dev/nu11
### Run - Liveness （at capacitv）
***bash
# conns == capacity: every request is admitted; measure its completion latency.
*  /build/examples/bin/c10k_client --port 19100 --conns 20 --rate 100|
-  m 64 --inflight 1 --duration 6
# client prints: request latency p50 ~= --work-ms
### Pass / fail
*  *Containment (200 › 20):** "cox_requests_in_flight" staus "<=--max-inflight™ at all times (pinned at 20), and "cox_requests_shed_total" advances (180 of the 200 shed). In-flight work is bounded regardless of offered load.
*  *Liveness (20 = 20):** admitted requests complete at ~'--work-ms*
(p50 = 500 ms, p99 = 500.6 ms -tight), and the loop never stalls: shed requests
get their cheap reply immediately, admitted ones get the full reply after the work delay.
### observed (study host, loopback)
over capacity (200 conns, cap 20): "in_flight" = 20 flat, "shed_total" = 180.
At capacity (20 conns): requests: 222 in 6.0s = 37 req/s ,
request latency p50=500339us p99=500605us* (= the 500 ms work time), "shed = e.
### Note on the closed-loop generator and the shed reply
c10k_client" is a closed-loop echo client: it expects •--msg bytes back. The shed reply (BUSY\n"
, 5 bytes) does NOT match that length, so a shed client
reads 5 bytes and then waits for the rest of an echo that never comes - it
STALLS rather than retrying. That is why, in the over-capacity run,
"shed_total jumps to 180 and-then holds steady (the 180 shed clients each sent once and stalled) while the 20 admitted clients keep cycling. This is a property of the simple load generator, not the limiter: the limiter's containment (in-flight ≤ cap) and the admitted-latency liveness are both measured correctly. A generator that treated any reply as "request done" would instead show
•shed_total climbing continuously; building one is not needed to validate the defense.
## S8 - fd exhaustion (EMFILE guard)
**Defense:** the "Acceptor" EMFILE guard. When accept) returns "EMFILE /
"ENFILE
(process/system out of fds), a naive reactor busy-loops: the listen socket stays readable, "accept() keeps failing on the same event, and the loop spins at 100% CPU making no progress (the classic libev/muduo trap). Cox's guard keeps a RESERVED idle fd; on EMFILE it closes the reserve, accept()'s the pending connection (clearing the readable event), closes it immediately (signalling the peer), reopens the reserve, and break's - guaranteeing forward progress of one connection per cycle instead of a spin.
**Attack:** flood a server running under a low fd limit.
### Run
***bash
#ulimit must be set BEFORE exec, in a subshell so it does not lower your shell. (ulimit -n 256; exec./build/examples/bin/c10k_server --port 19100 --quiet) \
›server.log 2>81&
SRV=$!
sleep 0.5
# A continuous liveness probe spanning the flood (1s per-probe timeout -> it
# cannot hang even when the server is out of fds).
*  /build/examples/bin/probe_client --port 19100 --count 8--timeout-ms 1000 & sleep 1
*  /build/examples/bin/c10k_client --port 19100 --conns 2000 --attack flood \
-  duration 4
# The discriminating measurement: CPU in a QUIESCENT window AFTER the flood.
# A spin bug keeps burning CP with no new connections; the guard drops to ~0.
read u1 s1 < <(awk fprint $14,$15}' /proc/$SRv/stat); sleep 3 read u2 s2<<(awk print $14,$15} /proc/$sRv/stat)
awk -v a=$u1 -v b=$s1 -v c=$u2 -v d=$s2 -v hz=$ (getconf CLK_TCK) \
'BEGIN{printf "quiescent CPU: %.2f s (near • = no spin) \n", ((c+d) - (a+b))/hz}'
kill $SRV 2>/dev/null
### Pass / fail
- **EMFILE path exercised**:
•server.log" shows Acceptor:: handleRead out of
fds (Too many open files) - the guard fired.
**No spin (the core claim)**: CPU in a quiescent window right after the flood is ~ø (observed **0.01 s over 3 s**). A busy-loop would keep a core pegged even with no connections arriving.
-
**Immediate recovery**: the liveness probe FAILS for new connections *during* exhaustion (expected - there are no fds to accept them; the guard accept-then-closes, so the probe sees "io_fail"), then SUCCEEDS again the instant the flood stops and fds free. No permanent wedge.
### observed (study host, loopback)
*ulimit -n 256, 2000-wide flood for 4 s:
-   "server.log": ~98k "out of fds" lines (one per shed connection during the flood - this log volume is itself the motivation for the planned Nullsink / flood-suppression work; it also dominates the *during-flood* CPU, which is the I cost of the attack, not a spin).
-   Quiescent-window CPU: **0.01 s / 3 s** - no spin.
-   Probe: ok before (0,1), "io_fail during exhaustion (2,4,5), ok after (6,7).
### Honest scoping
the liveness claim
Under fd exhaustion you CANNOT promise new connections are served - there are literally no fds to accept them. The accurate S8 claim is therefore narrower than "keeps serving": the server handles EMFILE WITHOUT SPINNING OR WEDGING, and recovers immediately when fds free. ESTABLISHED connections are unaffected by construction - the guard only touches the accept path; already-registered connection fds keep their channel's and are serviced in the same loop iteration
- so no separate held-connection test is included (it would add a fragile socket-juggling harness to assert a structural invariant already covered by every other scenario's long-lived connections).
#*# Close-reason labels (RESOLVED 2026-11-03, during S6)
"cox_connections_closed_total is now **labelled by 'reason **:
"peer" (default: peer close / EOF / socket error), idle"
"write_timeout,
"max_age, input_cap. The reason is set on 'Tepconnection at each force-close site ("TopConnection: CloseReason, read via closeReason() ), and
*installserverMetrics increments the matching labelled series on disconnect.
There is No unlabelled aggregate (mixing an unlabelled series with labelled ones under one name is malformed Prometheus; the total is sum by (reason) matching how "cox_http_requests_total" is only ever labelled). This is what lets s6 assert (reason="max age" specifically, and what 2/53 will use for
" {reason="write_timeout"}"
freason="input_cap"}". S1 did not need it (slowloris
vs an idle-timeout server has exactly one closer).
## CI smoke tier (planned)
A full 20k-connection attack is noisy on shared runners. The CI job is a scaled-down,
**non-blocking** variant (eg. 2k connections, short duration) that
asserts only the **containment invariants** (bounded "live" / rss / sockets and, for reaping scenarios, "closed advancing) - never latency numbers, which are meaningless on contended CI hardware. This mirrors how the benchmark job is kept informational. "probe_client" exits nonzero if any probe failed, so a liveness assertion can key on its exit code.