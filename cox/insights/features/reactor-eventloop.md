

# Feature: Reactor Event Loop > ##
## Purpose
The event loop is the heart of cox: a single thread that waits for I/o readiness on many file descriptors and dispatches events to handlers. It is the engine every other feature (timers, TCP, TLS) plugs into.
## Background
A network server must handle many connections concurrently. The three classic models:
Cox uses the **Reactor** model: it fits Linux "epoll', keeps memory low, and is the well-understood foundation used by muduo, libevent, and Netty.
## Design & technology choices (the journey)
### Component split (Reactor pattern, POSA2)
- **EventLoop** - owns the loop, drives poll -> dispatch -> run tasks.
- **Channel** - binds one fd to its interested events and callbacks. Not owned by the loop; owned by whoever owns the fd (e.g. TcpConnection).
**Poller** - abstract readiness backend; "EPollPoller" on Linux (a
"KQueuePoller" exists for macos/BSD but Cox is Linux-focused).
This separation keeps the os-specific polling isolated behind "Poller, and lets "channel stay platform-neutral (it speaks abstract IO_READ/IO_WRITE/ IO_ERROR
, which each poller translates to native flags).
### One loop per thread
Each "EventLoop" runs in exactly one thread; all of a connection's callbacks run on that thread. This removes per-connection locking: state touched only by the 1oop thread needs no mutex. Cross-thread work is marshaled in via 'runInLoop.
### Cross-thread wakeup: eventfd (not a pipe)
other threads schedule work with 'runinLoop; if the loop is blocked in poll, it must be woken. Cox uses a Linux ** eventfd ** (an 8-byte counter fd) rather than a self-pipe: one fd instead of two, a single 'read drains it, lower
overhead. See also features/poll-timeout-strategy.md.
### Poll timeout policy
The loop blocks in "poll with a timeout that is build-mode dependent: *-1" (block forever) in Debug for maximum bug visibility, a bounded backstop (10s) in Release so a missed wakeup degrades to latency rather than a permanent hang. Full rationale in features/poll-timeout-strategy.md.
##* Task queue + exception safety
runInLoop(task)* runs immediately if called on the loop thread, else enqueues and wakes the loop. queueInLoop(task always enqueues. Queued tasks are drained each iteration under a swap-lock (minimal critical section), and each task is wrapped in try/catch so a throwing task cannot kill the loop.
### When queueInLoop must wake the 1oop
Queued tasks run in dopendingtasks), once per iteration, right after channel dispatch. A queued task therefore needs a wakeup() unless the loop is guaranteed to reach doPendingTasks) before it next sleeps in "poll:
I caller | Wakeup needed? | why I
ا---/--
I Another thread | yes | the loop may be asleep in poll l
I Loop thread, inside a channel callback (incl. timers: timerfd is a channel) | no | dopendingTasks) runs right after dispatch, same iteration |
| Loop thread, before "1oop()* starts | **yes** | nothing else may wake the first poll / I Loop thread, inside a pending task |
**yes** | doPendingTasks) swapped the queue out; the new task waits for the next
batch, after the next poll |
The rule in code is: wake unless on the loop thread AND
I
processing_channel_ != nullptr (the loop is dispatching).
* *Bug fixed 2026-11-03.** Until then the condition was inverted: it woke only
* during* dispatch (where the wakeup is unnecessary) and skipped it in the two bold cases. A task queued there waited for some unrelated event; in Debug, with poll(-1), possibly forever. Most callers queue from other threads (always woken) or have more I/O coming, which is why it stayed hidden. It surfaced when an HTTP/2 client test's fake resolver answered via
•queueInLoop before "1oop()" started and the test "passed" at its 15 s safety timeout. Full write-up:

troubleshooting/event1oop-queueinloop-missed-wakeup.md.
### Data-race fix
processing_channel_ (used to decide whether a queued task needs a wakeup) is read across threads; it is std: atomic<Channel*» to avoid a data race the original code had.
## Hot-path performance
The poll -> dispatch -> run tasks cycle runs on every event batch, so a few early choices mattered disproportionately. The optimizations below were made in priority order (highest impact first) with no API change:
- **No per-event logging in the dispatch loop.** An early version formatted a timestamp ( gmtime_r" + "snprintf" + a heap "std: istring) and wrote a log line for *every* channel on *every* iteration. This dominated the hot path; it was removed (or gated behind a compile-time/debug level). This was the single largest win- any formatting/allocation per event is a trap.
**Reuse "active_channels_
_ across iterations. ** The active-channel list is a
member cleared ( clear()
than a fresh std: vecton" keeping capacity at the top of each turn rather
constructed and destroyed every iteration. Steady
state does zero allocations for it (muduo does the same).
* *Timer-derived / blocking poll timeout.** The original fixed 50 ms timeout woke the idle 100p~20x/sec doing nothing and prevented the CPU from sleeping. Because timers are 'timerfd" -driven and wake the loop through the poller anyway, the fixed timeout was pure overhead; it became -1" (block until an fd is ready), with the build-mode policy above. See features/poll-timeout-strategy.md.
* *Task-queue allocation (noted, lower priority).** pending_tasks is drained with the swap-under-lock trick, but the container ( std: :queue" defaults to a chunk-allocating std:: deque) and std: function type erasure still allocate. A reserve-friendly std:: vector<Task›" swapped out, and a small-buffer-optimized callable, are options only if profiling shows allocation pressure - not done speculatively.
* *eventfd wakeup drains in one read.** With the earlier self-pipe,
* handlewakeup() looped "::read to drain bytes; the 'eventfd 64-bit counter is drained by a single 'read' (see the wakeup section above).
## API surface

## API surface
```cpp
EventLoop loop; loop. loop ();
loop. quit ();
loop. runInLoop(task);
loop. queueInLoop(task);
loop. isInLoopThread();
loop. updatechannel(chan);
loop. removechannel(chan);
```
// run until quit()
// stop (thread-safe; wakes the loop)
// run on the loop thread (now or queued)
// always queue (never runs inline)
// (internal) register/modify a Channel
## usage
```cpp
reactor: :EventLoop Loop;
// ... create servers/clients bound to &loop ...
1oop.1oop(); // blocks, serving events, until loop.quit()
```
## Caveats
- An EventLoop" must be created and run on the same thread; enforced by assertInLoopThread() in Debug.
- Callbacks run on the loop thread and must not block (see features/reactor-blocking-work.md).
- Test fakes that stand in for async components (resolvers, clients) should answer through "queueInLoop, like the real ones. A fake that answers synchronously hides wakeup and re-entrancy bugs; the async fake is what exposed the missed-wakeup bug above.
## References
- Source: reactor/event_loop. (h, cc), reactor/io_channel.(h,cc), reactor/poller/.
- features/poll-timeout-strategy.md, features/reactor-blocking-work.md.
- troubleshooting/eventloop-queueinloop-missed-wakeup.md (the wakeup-rule bug).

