
# Threadsanitizer: Data Races and How They Were Fixed

A worked record of running ThreadSanitizer (TSan) over the full Cox test
suite for the first time. TSan turned a green 177-test run into 13 failures;
triage collapsed those into one genuine library race, one genuine
single-threaded use-after-free that TSan happened to surface, and one test-only
race. The rest were follow-on reports of the same root causes, plus timing /
port artifacts unrelated to any product race. Kept because the headline race
(a wakeup-fd write racing its own `close()`) is the kind of bug the ordinary
suite passes straight through, and because separating real races from harness
noise was the bulk of the work.

## How TSan is run

The build exposes a `tsan` cake preset (TSan and ASan runtimes conflict, so
they cannot be combined in one build):

```bash
cmake --preset tsan
cmake --build --preset tsan -j8
ctest --preset tsan
```

The preset compiles with `-fno-omit-frame-pointer` and
`-fno-optimize-sibling-calls` so the reported stacks are usable. CTest
timeouts are raised automatically under any sanitizer (300s / 600s instead of
120s / 300s) because instrumented builds are much slower.

## Symptom

The first instrumented run:

```
93% tests passed, 13 tests failed out of 177
== failing tests ===
   97 - TcpClientTest.RetryUntilServerAvailable (Failed)
   98 - TcpServerTest.EchoRoundTrip (Failed)
  106 - TcpServerTest.MultiThreadedEcho (Failed)
  108 - UnixSocketTest.EchoOverUnixDomain (Failed)
  132 - HttpServerTest.GetRequestResponse (Failed)
  ... (socket / http server tests, plus one benchmark)
```

Grouping every `SUMMARY: Threadsanitizer` line by the function it blamed
showed the 13 failures were not 13 distinct bugs:

```
     12 close
      6 reactor::EventLoop::queueInLoop(...)
      3 reactor::Channel::handleEventsGuarded()
      1 reactor::EventLoop::isInLoopThread() const
      ...
```

Two clusters dominated: a `close`/`write` race on the eventfd (12 reports), and
`queueInLoop`. The `close` cluster alone accounted for most of the failing
tests, so it was the first thing to chase.

## 1. EventLoop wakeup-fd race （genuine data race）

### Symptom

Isolating `TcpServerTest.EchoRoundTrip` gave a single clean report:

```
WARNING: ThreadSanitizer: data race
  Write of size 8... by thread T1:
    #0 close
    #1 reactor::EventLoop::~EventLoop()      event_loop.cc:84
  Previous read of size 8... by main thread:
    #0 write
    #1 reactor::EventLoop::wakeup()          event_loop.cc:223
    #2 reactor::EventLoop::quit()            event_Loop.cc:217
  Location is file descriptor 4 created by thread T1 at:
    #1 file_opts::createEventfd()            file_opts. cc:18
    #2 reactor::EventLoop::EventLoop()       event_1oop.cc:57
SUMMARY: ThreadSanitizer: data race ... in close
```

### Root cause

The main thread calls `quit()`, which calls `wakeup()`, which does
`::write(wakeup_fd_, ...)`. Meanwhile the loop thread runs `~EventLoop`, which
does `::close(wakeup_fd_)`. The write and the close race on the same descriptor.

This is a real bug, not a TSan artifact. Writing to a closed fd is not merely
useless: the OS can recycle the fd number, so a late `wakeup()` could write a
`uint64_t` into an unrelated socket that happens to have inherited the number.
`quit()` and `queueInLoop()` are both documented as callable from any thread,
so the cross-thread `wakeup()` is expected; the missing piece was lifetime
coordination with the destructor.

`wakeup_fd_` was a plain int; `quit_` was already `std::stomic<bool>`, which
is why the race was on the fd and not the quit flag.

### Fix

Guard the fd with the `task_mutex_` that already serializes the cross-thread
path, and invalidate it to `-1` under that lock before closing, so a late
`wakeup()` sees teardown and does nothing:

```cpp
void EventLoop:: wakeup() {
    uint64_t one = 1;
    ssize_t n = -1;
    {
        std::lock_guard<std::mutex> lock(task_mutex_);
        if (wakeup_fd_ < 0) {
            return;  // loop is being torn down; nothing to wake
        }
        n = ::write(wakeup_fd_, &one, sizeof (one));
    }
    ...
}
```

```cpp
// ~EventLoop: invalidate under the same lock, THEN close.
int fd_to_close = -1;
{
    std::lock_guard<std::mutex> lock(task_mutex_);
    fd_to_close = wakeup_fd_;
    wakeup_fd_ = -1;
}
if (fa_to_close >= 0) {
    ::close(fd_to_close);
}
```

`queueInLoop()` releases `task mutex_` before it calls `wakeup()`, so this does
not self-deadlock - that was checked before committing.

This single fix took the suite from 13 failures to 2: the `close` cluster was
one race surfacing across every socket/HTTP-server test that tore an
`EventLoop` down from a worker thread.

## 2. Connector use-after-free (real bug, surfaced by TSan)

### Symptom

`TcpclientTest.RetryUntilServerAvailable` reported a *heap-use-after-free*,
with both accesses on the same thread (T1), so it was not actually a race:

```
WARNING: ThreadSanitizer: heap-use-after-free
  Read of size 4 by thread T1:
    #0 reactor:: Channel::handleEventsGuarded()   io_channel.cc:38
    #1 reactor::Channel::handleEvents()           io_channel.cc:31
    #2 reactor:: EventLoop::loop()                event_1oop.cc:113
  Previous write of size 8 by thread T1:
    #0 operator delete
    #1 std::default_delete<reactor::Channel>::operator()  (unique_ptr reset)
SUMMARY: ThreadSanitizer: heap-use-after-free io_channel.cc: 38
         in reactor::Channel::handleEventsGuarded()
```

### Root cause

`Connector::removeAndResetChannel()` tried to defer destroying the `Channel`
it was currently inside a callback of:

```cpp
loop_->runInLoop([self = shared_from_this()]() { self->resetChannel(); });
```

But runInLoop runs the task *inline* when already on the loop thread - which
is exactly the case here. So nothing was deferred: the "Channel" was destroyed
synchronously inside its own callback, and when control returned,
handleEventsGuarded() read revents on freed memory.

### Fix

`runInLoop` runs inline on the loop thread; `queueInLoop` always queues for a
later iteration. `queueInLoop` was private, so it was made public (with the trap
documented on both methods) and used here:

```cpp
// Must be queueInLoop, not runInLoop -- runInLoop runs the task INLINE when
// already on the loop thread, so it did not defer anything at all.
loop_->queueInLoop([self = shared_from_this()]() { self->resetChannel(); });
```

A grep for other `runInLoop` self-destruction patterns found three more call
sites in `tcp_server.cc`/`tcp_client.cc`, but all hold a `shared_ptr` copy of
the connection in the lambda, so nothing is freed inline - only `Connector` had
the bug.

## 3. EventLoopBenchmark test race (test bug, not a product race)

### Symptom

`EventLoopBenchmark.Concurrencyperformance` raced on a stack variable in the
test itself:

```
Write of size 8 by thread T1:  event_loop_test.cc:314  (loop_ptr = &loop)
Read  of size 4 by thread T2:  reactor::EventLoop::isInLoopThread()  event_Loop.h:60
                               via runInLoop   event_loop_test.cc:340
SUMMARY: ThreadSanitizer: data race event_Loop_test.cc:314
```

### Root cause

The test published an `EventLoop*` from the loop thread to the main thread with
a plain pointer and a sleep-poll loop, with no synchronization:

```cpp
reactor::EventLoop* loop_ptr = nullptr;   // written by loop thread
...
while (!loop_ptr) { sleep_for(10ms); }    // read by main thread, no sync
```

This is a defect in the test, not in the library. The fix is to make the
hand-off atomic:

```cpp
std::atomic<reactor::EventLoop*> loop_ptr(nullptr);
loop_ptr.store(&loop);
...
while (!loop_ptr.load()) { sleep_for(10ms); }
reactor::EventLoop* loop = loop_ptr.load();
```

After this fix the full TSan run was clean: **100% tests passed, failed out
of 177, zero TSan warnings.**

## False positives / known-benign

Of the 13 original failures, most were follow-on reports of the wakeup-fd race
above (the same `close`/`write` pair re-detected in every test that destroys an
`EventLoop` from a worker thread), not independent bugs. They disappeared once
that one race was fixed.

Separately, socket/timing tests were observed failing intermittently under
load, and these were diagnosed as test-harness issues, not product races:

- **Fixed-port collisions.** The socket tests bind fixed ports. Running two
  preset suites concurrently on one machine, or several full `ctest` runs
  overlapping, produces spurious failures/aborts when a port is already in use.
  Each preset is clean on its own; CI runs them on separate runners. The proper
  fix is ephemeral port allocation per test - a larger harness change, recorded
  but not attempted here.
- **Load-sensitive timing bounds.** `TcpServerTest.IdleTimeoutClosesSilentclient`
  and `HardeningTest.MaxConnectionAgeRecyclesConnection` check a 1s timeout with
  a tight (~1.5-2s) bound that slips when the parallel suite loads the machine.
  Both pass in isolation (12/12). The bounds were widened; no product change was
  warranted, because the behavior was correct and only the test's tolerance was
  wrong.

None of these three categories were genuine product data races.

## Takeaways

1. **Group TSan reports by root cause before fixing anything.** 13 failing
   tests were really one race (the eventfd close/write), one use-after-free, and
   one test bug. Sorting `SUMMARY` lines by the blamed function made that
   obvious and saved chasing 13 ghosts.
2. **Writing to a closed fd is a real hazard, not a nuisance warning.** The fd
   number can be recycled, so the write can land on an unrelated socket. Coordinate
   fd lifetime with the lock that already guards cross-thread access, and
   invalidate the fd before closing.
3. **TSan also catches single-threaded use-after-free.** The `Connector` bug had
   both accesses on one thread; TSan reported it just as readily as a race, and
   it was the real defect behind `runInLoop` running inline on the loop thread.
4. **A race in a test is still a bug - fix the test.** Publishing a pointer
   across threads needs an atomic even in benchmark code; don't suppress it.
5. **Separate genuine races from harness noise explicitly.** Fixed-port
   collisions and tight timing bounds look like failures under load but are not
   product races; widening tolerances and documenting the port constraint is the
   right response, not a code change to the library.
