
# Feature: Timers
## Purpose
Schedule callbacks to run after a delay or on a repeating interval, on the loop thread. Timers underpin idle/write/max-age timeouts, reconnect backoff, and any time-based behavior.
## Background
An event-driven server needs time-based actions (timeouts, heartbeats, retries) without a dedicated timer thread. The challenge: the reactor blocks in "poll on fds, so a timer must also be expressible as an fd, or the loop must compute a poll timeout from the next deadline.
## Design & technology choices （the journey）
##* timerfd, not poll-timeout arithmetic
Cox uses Linux **timerfd **: a timer that is itself a pollable fd. The
"TimerQueue owns one timerfd wrapped in a "Channel'; when it becomes readable,
due timers fire. This is the "make everything an fd" philosophy - the 1oop can
block indefinitely and the kernel wakes it exactly when a timer is due, instead of the loop computing timeouts and waking early.
Alternative (user-space heap + computed poll timeout) was rejected because it couples timer precision to the poll call and complicates the loop; with timerfd the timer is just another readable fd.
### Ordered set + single timerfd
Timers are kept in a std::set ordered by expiration. The timerfd is armed to the *earLiest* timer. On fire, all due timers are collected, run, repeating ones re-armed, and the timerfd reset to the new earliest. One timerfd serves any number of timers.
### Cancellation with a sequence number
"TimerId" pairs a "Timer*" with a unique sequence number. Cancellation matches on the sequence so a stale handle cannot accidentally cancel a different timer that reused the same heap address (a classic use-after-free-adjacent hazard).
Self-cancellation during a repeating timer's own callback is handled via a
"cancelling" set consulted before re-arming.
### Thread-safety
runafter / runevery / cancel are safe from any thread: when called off the loop thread they marshal the mutation in via 'runInloop. The returned Timerid is only meaningful when scheduled from the loop thread (documented), matching
muduo's tradeoff.
## API surface
``` cpp
	TimerId runAt(Timestamp when, Task);
	TimerId runafter(uint64_t delay_ms, Task);
	TimerId runEvery(uint64_t intervalms, Task);
void	cancel (TimerId);
```
## Usage
Loop. [K [/* fires once in 5s */});
auto id = 1oop.runEvery（1000，［lf /* every 1s */｝）；
1oop.cancel(id); // from the loop thread
## Caveats
- Callbacks run on the loop thread; keep them non-blocking.
- Cancel from the loop thread for a reliable "TimerId.
## References
- Source: reactor/timer.(h, cc), reactor/timer_queue. (h, cc)",
* logging/timestamp.h.