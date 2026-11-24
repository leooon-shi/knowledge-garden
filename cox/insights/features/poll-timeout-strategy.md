<!-- cox/insights/features/poll-timeout-strategy.md -->

# Poll Timeout Strategy: `poll(-1)` vs `poll(fixed_timeout)`

> An engineering guide for reasoning about the timeout argument passed to the
> poller (`epoll wait`/`kevent`) in an event-driven reactor.

## TL;DR

- In a reactor where **every wakeup source is a file descriptor** (socket +
  `timerfd` + `eventfd`/`signalfd`），the correct default is **`poll(-1)`**-
  block indefinitely and let the kernel wake you. Zero idle CPU, immediate
  reaction.
- A **fixed timeout** is needed only when some wakeup source is *not* expressed
  as an fd, or as a deliberate **liveness backstop** against missed wakeups.
- A fixed timeout is **never** the correct mechanism for timers if a `timerfd`
  is available. Timers fire via the timerfd, which is itself pollable.

## Mental model

A reactor is **event-driven, not poll-driven**. The loop's job is:

```
block until the kernel says "an fd is ready" -> react -> repeat
```

The timeout argument answers one question: *"If nothing becomes ready, how long
should I wait before returning anyway?"*

```
poll(-1)   -> "wait forever; only return when an fd is ready"
poll(0)    -> "never wait; return immediately (busy poll)" [almost always wrong]
poll(T>0)  -> "wait up to Tms, then return even if nothing is ready"
```

## Decision checklist

Ask these in order:

1. **Is every event source I care about backed by an fd?**
   - Sockets? yes (`EPOLLIN`/`EPOLLOUT`/`EPOLLHUP`/`EPOLLERR`).
   - Timers? yes *if* using `timerfd_create` (Linux) / `EVFILT_TIMER` (kqueue).
   - Cross-thread wakeup? yes *if* using `eventfd` / a self-pipe.
   - Signals? yes *if* using `signalfd`.
   - File system changes? yes *if* using `inotify`.
   - **If all yes -> `poll(-1)` is correct.**

2. **Do I have periodic or time-based work NOT backed by an fd?**
   - e.g. flushing stats every N ms, advancing a timing wheel, polling a
     non-pollable resource.
   - **If yes -> either wrap it in a `timerfd` (preferred), or use a finite
     timeout derived from the nearest deadline.**
3. **Can a "should I keep running?" flag (eg. `quit_`) change without also poking a wakeup fd?**
   - **If yes -> either fix it so `quit()` calls `wakeup()` (preferred), or use a
     finite timeout so the loop periodically re-checks the flag.**
4. **Do I want defense-in-depth against a missed wakeup?**
   - **If yes -> use a large fixed timeout (e.g. 10s) purely as a backstop.**

## When `poll(-1)` is correct (the modern default)

Use `poll(-1)` when the invariant *"everything I wait on is an fd"* holds. This
is the design philosophy of modern reactors: **make everything an fd** -
`timerfd`, `eventfd`, `signalfd`, `inotify` - so the loop can always block
indefinitely and the kernel does all the waiting.

Benefits:

- **Zero CPU when idle.** The thread is truly asleep; the CpU can enter low-power
  states.
- **Immediate wakeup.** The kernel returns the instant any fd is ready.
- **No wasted syscalls.** No periodic "wake up, find nothing, go back to sleep".

This is why, for a TCP server, `poll(-1)` is right: data arrival, new
connections, writability, and peer close are all epoll events; connection
timeouts are `timerfd` events; cross-thread posts are `eventfd` events. Nothing
is left that a timeout would need to catch.

## When you need a fixed timeout

### 1. No timerfd - the poll timeout *is* the timer mechanism

classic/portable reactors keep timers in a user-space heap with no backing fd.
epoll then has nothing to wake on when a timer expires, so you compute the
timeout from the nearest deadline:

```cpp
int timeout = timers_.empty() ? -1 : ms_until_nearest_timer();
poller_->poll(timeout, active_channels);
```

Historical libevent and many textbook reactors work this way. **Not needed when
you have `timerfd`.**

### 2. Periodic work not tied to any fd

A coarse "tick" you deliberately chose not to back with a timerfd: metrics
flush, timing-wheel advance, polling a hardware register or a third-party
library that exposes no fd.

### 3. Graceful shutdown / liveness re-check

If `quit_` can be set without poking a wakeup fd, "po1l(-1)* may block forever
and never notice. A finite timeout bounds the re-check interval. **Better fix:**
have quit() call wakeup() so "poll(-1)" is interrupted immediately.

### 4. Batching / throttling under extreme load

Deliberately capping loop spin frequency to batch work or bound latency
variance. Niche; usually better solved with edge-triggered epoll + backpressure.

### 5. Watchdog / health instrumentation

Guaranteeing a minimum wakeup cadence so a supervisory check ("loop alive?",
"falling behind?") runs predictably even with no I/O.

### 6. Bridging a foreign, non-pollable event source

Integrating a library whose readiness you can only *poll* (call a function to
ask "anything ready?") rather than *wait on*. A finite timeout lets you re-poll
it periodically.

## Case study: why muduo uses a fixed 10-second timeout

muduo passes a fixed timeout, which appears to contradict the "block forever"
guidance:

```cpp
static const int kPollTimems = 10000; // 10 seconds
poller_->poll(kPollTimes, &activechannels_);
```

The crucial point: **muduo has a `timerfd`.** Its `TimerQueue` is built on
`imerfd_create` and registered as a `Channel`, so timers fire precisely via the
timerfd waking the poll - *not* via `kPollTimeMs`. If timers depended on the
10s timeout, every timer would be up to 10 seconds late.

So the 10s is **not** the timer mechanism. It is a **defensive liveness
backstop** (cases #3 and #5 above):

- Guarantees the loop wakes at least every 10s even if some wakeup were somehow
  missed.
- Degrades a hypothetical missed-wakeup bug to "10s of latency" instead of a
  permanent deadlock (which is what `poll(-1)` would produce).
- Costs essentially nothing: one syscall return per 10s of idleness.

muduo still relies on `timerfd` for timers and `eventfd` (`wakeupd_`) for
cross-thread `queueInLoop`/`quit`. The 10s sits on top as insurance.

## The tradeoff,summarized

| Choice          | Idle CPU | Missed-wakeup failure mode    | Posture      |
|-----------------|----------|-------------------------------|--------------|
| `poll(-1)`      | zero     | permanent hang                | purist       |
| `poll(10000)*`  | ~zero    | self-heals within 10s         | defensive    |
| `poll(nearest)` | zero     | timer late (no timerfd)       | portable     |
| `poll(0)`       | 100%     | n/a (busy loop)               | almost never |
  
## Guidance for Cox

Cox is Linux-only and expresses all wakeup sources as fds:

- **Timers** -> `timerfd` (see `reactor/timer_queue.cc`).
- **Cross-thread wakeup** -> `eventfd` (see `EventLoop::wakeup`/`handlewakeup`).
- **I/O** -> socket ads via the epoll poller.

Therefore `poll(-1)` is functionally correct: nothing time-based or cross-thread
is left unrepresented.

### Decision: build-mode-dependent timeout

Cox does NOT hard-code a single policy. A permanent hang in a **production**
service is unacceptable - a missed `wakeup()` under `poll(-1)` would wedge the
service forever, requiring an operator restart. So Cox selects the timeout by
build mode (see `reactor/event_loop.cc`):

```cpp
#if defined (DEBUG)
static constexpr int kpollTimeoutms = -1;      // block forever (dev visibility)
#else
static constexpr int kpollTimeoutMs = 10000;   // 10s backstop (prod safety)
#endif
```

Rationale for splitting by mode:

- **Debug (`-1`, purist).** A missed-wakeup bug surfaces as an obvious hang the
  developer will notice and fix immediately. Maximum diagnostic signal.
- **Release (`10000`, defensive backstop).** The same latent bug degrades to at
  most 10s of latency and self-heals, instead of a permanent outage. Negligible
  idle cost (~one syscall return per interval). This mirrors muduo's
  `kPollTimeMs`.

This is not the timer mechanism - timers still fire precisely via the `timerfd`.
The timeout is purely a liveness backstop.

**The split paid off (2026-11-03)** The "hypothetical" missed wakeup turned
out to be real: `queueInLoop` skipped `wakeup()` when called on the loop
thread outside event dispatch (before `loop()`, or from a pending task). In
Debug the affected test hung until its own 15 s timer - the obvious signal the
`-1` policy is meant to produce. In Release the same bug would only have added
up to 10s of latency, and would have been far harder to spot. See
features/reactor-event1oop.md ("when `queueInLoop` must wake the loop") and
troubleshooting/eventloop-queueinloop-missed-wakeup.md.

**Rule of thumb:** a fixed timeout is usually a sign that some wakeup source
isn't yet an fd - or a deliberate safety net. If you reach for a finite timeout,
first ask "can I make this an fd instead?" If the answer is yes, prefer that.
Here it is deliberately the latter: a safety net for production.

## Connection-level timeouts in TCP (important corollary)

TCP servers genuinely need time-based behavior: idle-connection timeouts,
handshake/write deadlines, keep-alive/heartbeat, retry backoff. **None of these
should use the poll timeout.** Implement them with the `TimerQueue`:

```cpp
// close a connection that has been idle for idlems; reset on activity.
TimerId id = loop->runAfter(idle_ms, [conn] { conn->forceclose(); });
// on activity: loop->cancel(id); id = loop->runAfter(idle_ms, ...);
```

Each such timer fires via the `timerfd`, which wakes the `poll(-1)` loop exactly
when due. This keeps the loop blocking indefinitely while still honoring precise
per-connection deadlines.

## Conclusion

A permanent hang in production is unacceptable - "self-heals within 10s" beats
"wedged forever, page the on-call, restart the service." The allure mode
asymmetry is the waterguments to dissed wake borsorrecontributor into a hard
outage, while a bounded timeout degrades it to a latency blip. For a product
environment, defensive wins.

## References

- muduo, `EventLoop.cc` (kPollTimeMs"), `TimerQueue.cc` (`timerfd`).
- `man 2 epoll_wait`, `man 2 timerfd_create`, `man 2 eventfd`, `man 2 signalfd`.
- Cox course: `reactor/event_loop.cc`, `reactor/timer_queue.cc`,
  `utilities/file/file_opts.cc`.
