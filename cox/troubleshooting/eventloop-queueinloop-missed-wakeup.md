

<!-- cox/docs/insights/troubleshooting/eventloop-queueinloop-missed-wakeup.md-->
# Troubleshooting: tasks queued on the loop thread waited for an unrelated event
A worked record of a core reactor bug found while testing the HTTP/2 client's DNS path. "EventLoop: queueInLoop" decided when to wake the loop with its condition inverted, so a task queued from the loop thread outside event dispatch sat in the queue until some other event happened. With the Debug build's infinite poll timeout that could mean forever. Kept because the symptom (a 15s test timeout in the HTTP/2 client) pointed far away from the cause, and because the bug had been in the reactor since the start.
##
Symptom
Two new "Http2ClientTest" cases using a fake resolver hung until their 15 s safety timer:
[FAILED] HttpClientTest.HostnameIsResolvedOnceAndReused (15186 ms)
OK] ]Http2ClientTest.DnsFailureFailswaitingRequests (15039 ms)
The second one "passed", but only because the safety timer quit the loop and the destructor failed the requests. Its real work should have taken milliseconds.
## Diagnosis path
### 1. Suspect the new client code - WRONG
The hostname path in "Httpzclient" was brand new, so it was the obvious suspect: maybe the resolver callback never reached onResolved(), or the parked requests were never re-dispatched. Reading it found nothing wrong.
Logging showed the client never even opened a connection.
### 2.Look at what the fake resolver does
The fake resolver answered asynchronously, like the real ones:
* 100p_->queueInLoop(callback)*. The client called it while handling the first get(), before "loop() had started.
##* 3. Reproduce with nothing but the reactor
A 10-line program: queue one task before loop(), quit from inside it.

```
RESULT queued task ran after 15000 ms (the safety timer, not the task
```

The reactor alone reproduces it. No HTTP/2, TLS or resolver involved.
## Root cause
```cpp
Il before if (lisInLoopThread) ||
processing_channel_•load(std: memory_order_relaxed))( wakeup() ;
```
The comment said "wake if not on the loop thread, or if the loop is dispatching channel events". That has the second half backwards:
- **During channel dispatch** (timers included, since timerfd is a channel) the wakeup is not needed: doPendingTasks) runs right after dispatch, in the same iteration.
**On the loop thread but not dispatching** the wakeup IS needed. Two cases:
-   before *10op() starts;
-   inside a pending task: dopendingtasks) swaps the queue out before running it, so a task queued now lands in the next batch, after the next
*  poll().
Skipping the wakeup here leaves the task waiting for an unrelated event.
The old code woke in the harmless case and skipped it in the two that matter. Nothing failed before because almost every existing caller queued from another thread (always wakes) or happened to have more I/0 coming.
## Fix
```cpp
if (lisinLoopThread() ||
	processing_channel_•load(std:: memory_order_relaxed) = nullptr) { 
	wakeup ();
}
```
That is muduo's rule: skip the wakeup only when the loop is guaranteed to process the queue before sleeping again. The fix is a single condition. It costs at most one extra eventfd write for tasks queued from timers, which was already the safe direction. The rule, with a table of every caller case, is now documented in features/reactor-eventloop.md ("When queueInLoop" must wake the loop"); features/poll-timeout-strategy.md notes this as the real case its Debug "po1l(-1)" policy was meant to expose.
Verified: the 10-line reproducer runs both a pre-loop task and a nested task after 0 ms. "DnsFailureFailswaitingRequests" drops from 15s to 26 ms and HostnameIsResolvedOnceAndReused passes. Both full suites pass (219 / 251), and ASan and TSan report nothing.
## A second finding during sanitizer runs (not this bug)
Running the ASan suite repeatedly showed intermittent
*bindorDie ... Address already in use aborts on different tests (e.g. port 56204, 48002, 46214). These are the KNOWN CONSTRAINT in the roadmap: the socket tests bind fixed ports, and on this host the ephemeral port range is 32768-60999, so a test's fixed port can be held as the outgoing port of some other test's client connection. Runs of the HTTP/2 tests alone (8x) and later full runs were clean, and each aborted test passes on rerun. The fix is ephemeral port allocation per test, already noted in the roadmap.
## Takeaways
1. **Reduce to the bare library before suspecting new code. ** A 10-line reactor program proved the cause was 2 layers below the code under test.
*  *"Should I wake the loop?" is about whether the loop will look at the queue before sleeping.** only the dispatch phase guarantees that. Before
*  100p()* and inside pending tasks it does not.
1.    **A test that passes at its timeout is a failing test.** The 15 s "OK" was the real signal; compare durations against what the test should take.
2.    **Fakes that behave like the real thing find real bugs. ** The fake resolver answered through "queueInLoop, exactly like the real ones; a synchronous fake would have hidden this.
