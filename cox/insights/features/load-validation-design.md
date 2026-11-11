# Design: Load & Adversarial Validation of Hardening (P1)
> **Status:** IMPLEMENTED. This was the design/plan; all eight
> scenarios S1-S8 are now built and reproducible. The executable companion -
> exact commands, tools, pass/fail criteria, observed numbers, and findings -
> lives in [`docs/performance/hardening-validation.md`](../../performance/hardening-validation.md).
> The `production-hardening.md` caveat is cleared ("validated under load"). This
> document is kept as the rationale (the *why*); the performance doc is the *how*.

## 1. Purpose

The hardening knobs - idle timeout, write timeout, max-input-buffer, connection max-age, max-connections, accept-rate shedding, concurrency limit - are each covered by a unit test that proves the mechanism *fires*. That is necessary but not sufficient: a unit test fires one timer against one mock client. Production asks a different question: **under thousands of concurrent adversarial clients, does the defense hold, and does the server stay responsive to legitimate traffic while it does?** This plan validates that.
# 2. what "validated" means （the thesis）
For each defense, two properties:
1. **Containment** - the targeted resource stays bounded (memory, fds, in-flight work) no matter how hard the adversary pushes.
2
**Fairness / liveness** - a well-behaved client still gets served (bounded latency) while the attack is in progress. A defense that protects memory by freezing all legitimate traffic has failed property 2.
Both must be *measured*, not asserted. This is where the P1 **metrics module** (see metrics-design.md) pays off: the pass/fail signals (live connections, rejected total, RSS, p99) come from "/metrics" + /proc, not from log reading.
## 3. Harness
Build on the EXISTING 'examples/c10k/* tooling rather than new infrastructure:
- "clek_server" - the target; already exposes " --idle , --max-conns
- -accept-aps
* --threads/--reuseport •
- "c10k_client" - the legitimate-load generator (connect rate, inflight, src-ips).
"slow_sink" / "backpressure_proxy" - slow-consumer shapes.
ew pieces needed (small, test-only, under "examples/c10k/ or tests/load/"):
- **An adversary generator** with modes: "slowloris" (open N conns, dribble 1 byte / few seconds, never complete a request), slow-read' (send a request, then read the response at x bytes/sec or not at all), flood (open at max rate), "big-body" (stream an oversized body). Reuse c10k_client"'s non-blocking epoll core; add a '--attack <mode› flag rather than a new binary•
**A probe client** - a single legitimate client issuing one request/second throughout the attack, recording success + latency. This measures property 2.

- **A sampler** - scrapes /metrics and /proc/‹pid›/status" (VmRSS), "ss -s" (fd/socket counts) once per second into a CV for the pass/fail check.
These are measurement tools; keep them out of the core library (same rule as tr load generators already being outside it).
## 4. Scenarios & pass/fail criteria
Each scenario: a defense, the attack that targets it, the containment bound, and the liveness bound. Numbers are starting thresholds to calibrate on the study host, then freeze per-environment.
### S1 - Idle timeout vs slowloris
- Attack: *--attack slowloris --conns 20000, idle dribble every 9s against | °c10k_server --idle 10°.
- Containment: live connections plateau near the arrival rate × 10s, NOT monotonically grow; cox_connections _closed_total reason="idle"}" advances.
- Liveness: probe p99 ‹ 50 ms throughout.
### S2 - Write timeout vs slow-read consumer
- Attack: *--attack slow-read* (request sent, response never read) x10k against a server with "setwriteTimeout(5)*
- Containment: aggregate output-buffer memory bounded; VmRSS plateaus;
"closed(reason="write_timeout"} advances.
- Liveness: probe unaffected.
### S3 - Max-input-buffer vs unframed flood
- Attack: a client that sends an endless body the handler never consumes, against "setMaxInputBufferSize(1MiB)"
I
- Containment: per-connection input buffer never exceeds the cap;
connection is
force-closed; RSS flat.
- Liveness: other clients served.
### S4 - HTTP body 1imit vs big-body （ties to P1 #5）
- Attack: •--attack big-body POSTing 1 GiB to a server with
* setMaxBodySize(8MiB)*
- Containment: server reads no more than the cap before responding 413" and closing; no 1 GiB buffered. 'cox_http_rejected_total(reason="too_large"}" advances.
- Liveness: concurrent small requests keep 2xx-ing.

### S5 - Max-connections / accept-rate vs flood
- Attack: *--attack flood at 50k conn/s against *--max-conns 10000
- -accept-qps 5000*.
- Containment: live connections ≤ cap; "rejected_total advances; fd count bounded; accept queue keeps moving (no permanent ListenOverflows growth).
- Liveness: probe still connects within a bounded retry.
### SG - Connection max-age recycLing
- Attack: 10k long-lived idle keep-alives against "setMaxConnectionage(30)"•
- Containment: each connection is recycled near its age; no unbounded-lifetime growth; timers all cancelled on close (cross-check with ASan run - no leaked TopConnection, the exact class of bug the sanitizer saga found).
### S7 - Concurrency Limiter vs heavy in-flight
- Attack: many simultaneous slow handlers (simulated work) exceeding the
"ConcurrencyLimiter capacity.
- Containment: in-flight gauge ≤ capacity; excess sheds with the configured cheap reply.
- Liveness: admitted requests complete at expected latency.
### S8 - fd exhaustion （EMFILE guard）
- Attack: flood under ulimit -n 1024.
- Containment: server logs the EMFILE path, keeps serving existing connections, does NOT spin on a permanently-readable listen socket (CPU stays low - measurable regression signal).
## 5. Running & automation
**Local, manual first.** These need a tuned host (fd limits, ss, /proc,
often two machines for server-bound numbers. Document the exact commands per scenario (like "examples/README.ind"'s exercise table), so a run is reproducible.
**CI: a reduced smoke tier, non-blocking.** A full 20k-connection attack is not a unit test and is noisy on shared runners. Add a scaled-down variant (e.g. 2k connections, short duration) as a separate, non-blocking job that asserts only the *containment* invariants (bounded live-conn / RSS / fds), not latency numbers (which are meaningless on CI hardware). Mirrors how the benchmark job is kept informational.

- **Sanitizer cross-check.** Run S2/$3/56 under ASan at reduced scale: the
teardown paths these exercise (timer cancellation, buffer release) are where leaks hide. A clean ASan run is part of "validated."
## 6. Known constraints （from the roadmap）
- The socket tests use FIXED ports; load scenarios must pick their own ports and not overlap a running suite. A proper fix (ephemeral ports) is a separate harness change.
- Two timing-sensitive hardening tests already widen their bounds under parallel load; the load scenarios must run on a quiescent host or dedicated runner to avoid the same slippage.
## 7. Deliverables
1. --attack ‹mode›*
added to "c10k_client (slowloris, slow-read, flood,
big-body) .
1. A probe client + a /metrics +/proc sampler (CSV output).
2. A documented scenario table (S1-S8) with commands and thresholds, in examples/README.md" or a new docs/performance/hardening-validation.md.
3. A reduced, non-blocking CI smoke job asserting containment invariants.
4. production-hardening.md caveat updated to "validated under load (seeLoad-validation)"
## 8. Dependencies / ordering
- **Best done AFTER the metrics module (P1 #4):** the pass/fail signals come from metrics. Without them, the harness falls back to log scraping and /proc, which is workable but weaker. Recommend: metrics first, then this.
- Independent of HTTP/2 and the remaining P2 items.
I
## 9. References
- "net/tcp_connection.* (idle/write/max-age timers, input cap, backpressure),
* net/tcp_server.* (max-conns, accept-rate, overload cb),
* net/acceptor. * (EMFILE guard), flowcontrol/* (limiter, token bucket),
* http/http_context.* (body limit), examples/c10k/* (harness base).
- features/production-hardening.md (the mechanisms under test),
features/metrics-design.md (the measurement), features/flow-and-overload-control.md.

