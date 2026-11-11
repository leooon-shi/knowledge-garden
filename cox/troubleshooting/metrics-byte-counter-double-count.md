
<!-- cox/docs/insights/troubleshooting/metrics-byte-counter-double-count.md -->
# Troubleshooting: metrics byte counters double-counting on close
A worked record of diagnosing why "cox_connection_bytes_in total read **20** for a single 10-byte echo connection. Kept because the root cause - the disconnect connection-callback is **not exactly-once** - is a trap any one-time-accounting code can fall into, and because the diagnosis path shows how to separate a test artifact from a real defect.
## Symptom
A new integration test ("MetricsServerTest.ByteCountersSummedonClose"): one client connects, sends 10 bytes to an echo server, reads the I0-byte echo, closes. The adapter folds each connection's final "bytesRead()/byteswritten()
into server-wide counters on disconnect. Expected bytes_in == bytes_out = 10 .
observed, inconsistently at first:
cox_connection_bytes_in_total = 20
cox_connection_bytes_out_total = 20
(expected 10)
(sometimes 10)
The "in=20, out=10" variant was the key clue (see below).
I
## Diagnosis path (including the dead ends)
### 1. First hypothesis: timing / scrape-before-fold - WRONG
Guess: the close-time fold had not run before scrape). Ruled out: the test sleeps 50 ms after close() before scraping, and the values were too HIGH, not too low. A missing fold undercounts; this overcounted.
### 2. Second hypothesis: stale port / TIME_WAIT reuse - WRONG
The failure looked intermittent across rapid reruns, suggesting a leftover connection on the fixed port. Ruled out: moved the test to a fresh unused port
(46213 -> 46313); still failed. Also confirmed ss -tan | grep ‹port› showed no lingering sockets. The "intermittency" was actually just TIME_WAIT delaying the bind between back-to-back runs, not the cause of the wrong value - in isolation the test failed **consistently** at 20.
**Lesson:** "intermittent" under rapid reruns on fixed ports is often the known fixed-port constraint, not the bug under investigation. Run the suspect test ALONE, spaced out, to see its true determinism.

### 3.Reproduce in a standalone probe - isolates "test vs 1ibrary"
Built a tiny standalone binary with the SAME server setup (installserverMetrics" + echo + one raw-socket client). It printed the
**correct** 10/10. So the production code was correct *on that path* and the difference lived in the test's execution, not the adapter logic. This split the search space in half.
### 4.The decisive clue： “in=20,out=10'
This value is impossible from:
-   one echo connection (echo implies in = out), or
-   folding one connection's final totals twice (would give " 20/20).
It is explained by **two fold events**
where one occurred before the echo write
completed: fold #1 sees read=10/written=10, fold #2 (a split-second earlier in some runs) sees read=10/written=0. Two folds → the disconnect path runs twice.
### 5. Ground truth: instrument the actual callback - CONFIRMED
Stopped guessing and printed every connection-callback invocation inside the failing test's own setup:
[dbg] conn ...#1 connected=1 read-0 wr=0 [dbg] conn
•.•#1 connected=0 read=10 wr=10
[dbg]
conn
•••#1 connected=@ read=10 wr=10
‹- connect
<- disconnect (fire #1)
<- disconnect
(fire #2)
！！
Two "connected=0" fires for ONE connection. The fold ran twice. Root cause found.
### 6. Root cause in the core
"TepConnection notifies the connection callback on disconnect from **two** teardown paths:
-   "handleClose() (peer closed the socket), and
-   connectestroyed() (server/loop teardown, e.g. "~TcpServer ).
*   connectestroyed() guards with if (state == kConnected), which normally suppresses the second fire after handleclose() sets kisconnected. But in the test's shutdown ordering both paths reach the callback for the same connection, so the disconnect notification is **not guaranteed exactly-once**.
Any code doing one-time work in the disconnect callback (metrics folds, resource release, audit events) can double-execute.

## Fix
Make the disconnect accounting **idempotent and bounded** in the adapter, rather than assume exactly-once:
- Track Live connection NAMES in a set: insert on connect, "erase() on disconnect. 'erase() returns 1 only for the FIRST disconnect, so the fold (and the closed/live accounting) runs exactly once; a duplicate disconnect finds the name gone and is ignored.
*  *Bounded memory:** because names are inserted on connect and removed on disconnect, the set tracks live connections, not lifetime totals - it does not grow without bound on a long-running server.
*  *Thread-safe:** connection callbacks run on different io-loop threads in a multi-threaded server, so the set is guarded by a mutex (the metric atomics were already thread-safe).
See "metrics/server metrics.cc" (installserverMetrics").
## Verification
-  The integration test now reads 10/10 and passes consistently in isolation.
-   Full suite green; "metrics_test" and "metrics_server_test" are ASan/TSan clean (the shared dedup set across loop threads is race-free under its mutex).
## Takeaways
1.    **Impossible-looking values encode the mechanism.** "in=20, out=10° could not come from double-folding one connection; it demanded "two events at different moments, " which pointed straight at a double callback.
2.    **Reproduce in a standalone probe** to decide "is this the library or the test?" before digging deeper.
3.    **stop guessing after two wrong hypotheses** and instrument the actual code path for ground truth. The print of two connected- fires ended the investigation immediately.
4
**Do not assume callback exactly-once** across multiple teardown paths.
One-time accounting in a disconnect callback must be idempotent.
5. **"Intermittent on fixed ports"** is usually the TIME_WAIT/bind constraint, not the defect - confirm by running the test alone, spaced out.
## Open follow-up
The double-fire is a core-level trap, not specific to metrics. Options:
(a) document the disconnect callback as "may fire more than once; make one-time work idempotent", or (b) enforce exactly-once at the source (guard the callback so only the first disconnect notifies). Deferred as a separate decision to avoid changing core teardown semantics inside the metrics work.