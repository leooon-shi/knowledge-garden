
# Troubleshooting: streamed responses leaked when handlers captured their own stre
A worked record of a memory leak found by LeakSanitizer while adding async / streamed HTTP/2 responses (ServerStream). Handlers naturally capture their own "ServerstreamPtr" inside the stream's callbacks, which made a reference cycle, so finished streams were never freed. A second, similar leak was in a test's self-rescheduling timer. Kept because this cycle comes straight from the natural way to use the API, so users will write it too.
## Symptom
Every streaming test passed, but the ASan run reported leaks in two binaries:
٠٠٠
nttp2_session_test
reports=1 (all tests passed)
http2_integration_test
reports-1
(all tests passed)
Indirect leak of 352 byte(s) ••. in http: :HttpSession: :dispatch ...
Indirect leaks only, no direct leak: the signature of a reference cycle (the same signature the earlier ConnectionPool leak had).
## Diagnosis path
I
### 1. Find the test
Running each test of "http2_session_test" alone under ASan: only Http2StreamingTest.LargeStreamedBodyRespectsFlowControl" leaked.
### 2. Read what that handler holds
cpp
auto pumpwrites = [&, s]() f •.. s-›write(chunk) ... s-›end(); }; s-›setonwritable([pumpwrites] { pumprites); );
٠٠.
The stream owns its 'onwritable callback; the callback holds s, a
"shared_ptr to the stream. Stream → callback → stream: neither is ever freed, even after end() .
### 3.The integration-test leak
*streamedResponseArrivesIncrementally used a timer tick that re-armed
itself: *tick = [s, n, tick, &loop] {.. loop.runAfter(100, [tick](...)); }.

The closure holds tick, the shared_ptr" that owns it: a self-cycle in the test, keeping the closure and the stream it captures alive forever.
## Root cause
1.    **Library:** ServerStream kept its callbacks (onwritable, "onabort) after the exchange was over. Any callback that referenced the stream kept the stream alive indefinitely.
2.    **Test code:** a self-referencing timer closure, a user-level leak that the library cannot prevent.
## Fix
-   "Serverstream" drops its callbacks when the exchange ends: in 'end()
*  respond()and on abort. Capturing the stream inside them is now safe for
any exchange that finishes. Documented in "server_stream.h': a stream that never ends and is held only by its own callback still leaks, so always end() or let the client abort.
- The test's tick captures a weak_ptr" to itself and locks it to re-arm.
The SSE example in the docs uses that pattern.
Verified: both binaries report no leaks under ASan/LSan; TSan clean.
## Takeaways
1.    **"All tests passed" is not "no leaks".** only the sanitizer run showed this; run ASan/LSan over new lifetime-heavy code before calling it done.
2.    **Indirect leaks with no direct leak mean a cycle.** Look for an object that owns a callback which captures the object.
3.    **If the natural way to use an API creates a cycle, the API should break it.** Releasing callbacks at a well-defined end point (here, the end of the exchange is cheaper than asking every user to remember weak_ptr.
4.    **Self-rescheduling closures need a weak self-reference.**
