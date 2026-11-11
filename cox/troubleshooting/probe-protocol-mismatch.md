
# Troubleshooting: "liveness failed" that was really a probe/target protocol
A worked record of a pattern that recurred three times while building the load-validation scenarios (docs/performance/hardening-validation.md"): a scenario's LIVENESS check reported failure (0% success, stalls), looking Like the server could not serve legitimate traffic under attack - but in every case the real cause was that the measurement client spoke a DIFFERENT protocol than the target under test. Kept because the symptom ("liveness broken") points at the server, while the fault is in the probe, and the same trap recurs whenever a generic probe meets a specialized target.
## The recurring symptom
A liveness probe (fresh connection, send a small request, expect a reply, measure latency) returns all failures while the containment side of the scenario looks perfect:
containment: PASS (bounded live/RSS/closed-by-reason as designed) liveness:
••、
probe 0/N ok <-- looks like the server stopped serving
The instinct is "the defense is starving legitimate clients." Sometimes that is a real finding (see S5 below). often it is not.
#* Three instances, same root pattern
### S3 - sink target, round-trip probe (NOT I defect)
S3's target runs c10k_server --sink: a handler that NEVER consumes or echoes input (simulating a parser awaiting a frame that never completes), so the input-buffer cap can fire. The probe sent bytes and waited for an echo. But a sink echoes NOTHING to anyone - the probe's reply never comes, so it times out. ø/8, but not because the server is overloaded: it serves no one BY CONSTRUCTION.
Fix: probe_client --no-read - success = connect+send completes in time (the
accept/read path stays responsive), which is the only liveness claim meaningful against a sink. A "fully served" claim would need a framed server that consumes complete frames while capping an oversized partial one.
#** S4 - HTTP target, raw-echo probe (NOT a defect)
S4's target is "http_target (a real HTTP server with a body-size cap). The probe sent 16 raw bytes and waited for them echoed back. "http_target" parses the raw bytes as HTTP (malformed) and never echoes - the probe times out. 0/7,

again not a server problem: the probe wasn't speaking HTTP.
Fix: "probe_client --http - send a minimal GET / HTTP/1.1 and count ANY HTTP response as success. 7/7 once the probe spoke the target's protocol.
### S7- closed-Loop echo client, mismatched shed reply （NOT a defect）
S7's server admits work through a
"Concurrencylimiter" and SHEDS excess with a
cheap "BUSY\n" (5 bytes). The load generator is closed-loop: it sends * --msg bytes and waits for exactly *--msg bytes back. A shed client receives 5 bytes (BUSY\n"), decrements its expected-bytes counter, and then WAITS FOREVER for the remaining bytes of an echo that will never come - it stails instead of retrying. Symptom: " shed_total jumps once and then freezes, which looks like shedding stopped working.
Not a limiter defect: in-flight stayed <= cap and the admitted requests completed at the expected latency. The frozen "shed_total" is the generator's byte-exact accounting meeting a reply of a different length. Documented as a generator property; the two real properties (containment + admitted-latency) were measured correctly by splitting into an over-capacity run and an at-capacity run.
#* The contrast case: S5, where "liveness failed" WAS real
Not every liveness failure is a mismatch. In S5 (flood vs accept-rate limit) the
*  --http-agnostic probe DID connect and send fine, and still only 25% succeeded
-   because the global accept-rate limiter sheds indiscriminately and drops the probe's connections alongside the attacker's. That is a genuine fairness finding, recorded as such. The discriminator: in s5 the probe's connect/send themselves failed/were-reset (the server actively shed them); in S3/S4/S7 the connect/send SUCCEEDED and only the reply-matching failed.
## How to tell them apart (the diagnostic)
When a liveness probe fails, before blaming the server ask: **did connect+send succeed, and is the probe speaking the target's actual protocol?**
1. Does the target reply to THIS probe's bytes at all? (echo vs sink vs HTTP VS a cheap shed reply of a different shape.) If the target by design does not echo what the probe expects, the probe is wrong, not the server.
2. Split the probe's success condition: connect-ok, send-ok, reply-ok are three different claims. A sink fails only reply-ok; a starved acceptor fails connect/send too.
3.  Match the probe protocol to the target: •--no-read for non-echoing sinks,
•--http* for HTTP targets, and treat "any reply (even a cheap rejection)" as served when the defense's correct behavior is a cheap reply.
## Fix (summary)
"probe_client gained protocol-matching modes rather than assuming every target echoes raw bytes:
-  -no-read: accept-path liveness (connect+send), for sink targets (S3).
-   --http: send a GET, accept any HTTP reply, for HTTP targets (S4).
Default remains the raw echo round-trip for the echo server (S1/S6).
## Takeaways
1.    **A failing liveness probe accuses the server but often indicts the probe.**
2. Check protocol match before concluding the defense starves legitimate clients.
3.    **Split "served" into connect / send / reply.** which sub-step fails tells you whether it's a starved acceptor (real) or a reply-shape mismatch (measurement artifact).
**A generic probe is wrong for a specialized target by default. ** Sinks, HTTP servers, and shed-with-cheap-reply handlers each need the probe to speak their shape; otherwise 0% success is meaningless.
4. **Keep the contrast case in mind (S5).** The same symptom can be a real fairness defect - the diagnostic (did connect/send succeed?) is what separates the real finding from the artifact.