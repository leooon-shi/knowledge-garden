<!-- interview/epoll_edge_triggered.md -->

# Edge-Triggered epoll and the Lost-Event Class of Bugs

This topic names the single most dangerous failure mode of edge-triggered (ET) epol1 - a
"missed EPOLLIN" - and connects it directly to the CLOSE_WAIT surge. A missed notification
under ET doesn't just drop one event; it can silently strand a connection forever, which
is exactly how ET bugs manifest as CLOSE_WAIT leaks, hung requests, and mysterious
latency.

## The core mechanism: level vs. edge

The kernel decision that governs everything here:

> **Level-triggered (LT) reports readiness as a *condition*: as long as the socket buffer
> is non-empty, every `epoll_wait` keeps telling you it's readable. Edge-triggered (ET)
> reports readiness as a *transition*: you are notified only when the state *changes* from
> not-ready to ready. If you don't fully drain on that one edge, the kernel has no
> obligation to tell you again.**

Concretely, under ET the kernel arms the notification when data arrives on an empty buffer
(the not-readable → readable edge). It fires once*. If you read only part of the buffered
data and stop, the socket is still readable, but no new edge occurred - so `epoll_wait`
will **not** return that fd again until more* data arrives and creates a fresh edge. If
no more data ever arrives (e.g., the peer sent its final bytes and then a FIN), that fd is
now invisible to your event loop. The bytes sit unread, the request never completes, and -
critically - you never observe the EOF, so you never close() • That stranded fd is a
CLOSE_WAIT entry that will never resolve.

That's the whole trap: **ET makes "I didn't fully drain" a silent, permanent loss of the
fd, not a temporary delay.**

## Why anyone uses ET at all

If ET is so hazardous, why use it? Two reasons that matter at gateway scale:

1. **Fewer wakeups / syscalls.** LT will re-report a readable fd on every `epoll_wait`
   until you drain it. In a busy loop with backpressure, that's repeated wakeups for the
   same fd. ET fires once per edge, reducing syscall and scheduling overhead - meaningful
   when you're handling hundreds of thousands of connections.
2. **It composes with one-shot and multi-threaded designs.** ET (especially with
   `EPOLLONESHOT`) gives precise control over exactly when an fd re-enters the ready set,
   which is valuable when multiple worker threads share an epoll fd and you must avoid two
   threads handling the same connection.

The price is that ET shifts the burden of "have I consumed all readiness?" from the kernel
to you. **ET is a contract: you promise to drain until the syscall says `EAGAIN`.**

## The rules that make ET correct

Every ET bug is a violation of one of these. They are not optional.

**1. The socket must be non-blocking.** Under ET you loop `read()`/`recv()` until it
returns `EAGAIN` /`EWOULDBLOCK`. On a *blocking* socket, the read after the last byte would
block the entire event-loop thread - freezing every other connection on that worker. So ET
without `O_NONBLOCK` is a latent deadlock. Set it at accept time:
`accept4(lfd, ..., SOCK_NONBLOCK | SOCK_CLOEXEC)`.

**2. Drain reads until `EAGAIN`.** You must loop, not read once:

``` cpp
// ET read: consume everything until the kernel says "no more"
for (;;) {
    ssize_t n = ::read(fd, buf, sizeof (buf));
    if (n > 0) {
        consume(buf, n);
        continue;                 // there may be more; keep going
    }
    if (n == 0) {                 // EOF: peer sent FIN
        close_connection(fd);     // <-- prevents the CLOSE_WAIT Leak
        break;
    }
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;                // fully drained; wait for next edge
        if (errno == EINTR)
            continue;             // interrupted, retry
        close_connection(fd);     // real error
        break;
    }
}
```

If you `break` after a single successful `read` (the LT habit), you've written the classic
ET bug. The leftover bytes are invisible until the next edge that may never come.

**3. Drain writes symmetrically,** ET applies to `EPOLLOUT` too. When a large write
returns short with `EAGAIN`, you register for `EPOLLOUT` and, on the next writable *edge*,
continue draining your send buffer until `EAGAIN` again. Miss this and you get
the write-side analog: a half-sent response that stalls forever, the client walting on bytes
you're holding.

**4. Handle all readiness reported in one wakeup.** A single edge can coalesce multiple
conditions. You may get `EPOLLIN | EPOLLRDHUP` together, or data followed immediately by
EOF. If you process `EPOLLIN` and ignore `EPOLLRHUP`/`EPOLLHUP`/`EPOLLERR` in the same
event, you can miss the close. Always inspect the full event mask:

```cpp
uint32_t e = events[i].events;
if (e & (EPOLLHUP | EPOLLERR)) { close_connection(fd); continue; }
if (e & EPOLLIN) handle_readable(fd);   // drains to EAGAIN, may close on EOF
if (e & EPOLLRDHUP) {/* peer half-closed; finish and close */ }
if (e & EPOLLOUT) handle_writable(fd);   // drains send buffer to EAGAIN
```

Note that draining `EPOLLIN` to EOF often *is* how you discover the half-close, so
`EPOLLHUP` is belt-and-suspenders - but under ET you want both, because it lets you
react to a pure half-close even when there's no more payload data to trigger an `EPOLLIN`
edge.

**5. `accept()` must also loop under ET.** On a listening socket in ET mode, a burst of
incoming connections produces one* edge. If you `accept` once per wakeup, you accept one
connectionand leave the rest queued with no further edge until the *next* new connection
arrives - the backlog drains at the rate of new arrivals, connections sit unaccepted, and
clients see latency or timeouts. Loop `accept4` unt11 `EAGAIN`.

# Exactly what happens when you omit the vital event

Trace the failure of an ET loop that under-drains or ignores an EPOLLIN edge:

1. Peer sends a request (or its final bytes) and then FIN.
2. Kernel fills your receive buffer, fires **one** `EPOLLIN` edge.
3. Your handler reads part of it (or the event is dropped by faulty dispatch) and returns
   without reaching `EAGAIN`.
4. Socket still has buffered bytes and a pending EOF, but **no new edge will fire** - the
   peer has nothing more to send.
5. `epoll_wait` never returns this fd again. The request never completes → the client
   times out or hangs. The EOF is never observed → your code never calls `close()`.
6. The fd sits in **CLOSE_WAIT** indefinitely. Multiply across load and you get the
   CLOSE WAIT surge, fd exhaustion, and `EMFILE` on `accept())` - the exact cascade from
   the CLOSE_WAIT topic.

So the ET drain bug and the CLOSE WAIT surge are frequently *the same bug viewed from two
angles*: the missed edge is the cause, the CLOSE_WAIT pileup is the observable symptom.

There's a nastier variant: **partial progress with intermittent traffic.** On a keep-alive
connection that occasionally sends data, under-draining seems to "work" because each new
request creates a fresh edge that happens to also flush the previously stranded bytes. The
bug hides in testing and only manifests as sporadic stalls under specific timing - the
worst kind to debug. This is why ET correctness must be structural, not tested-in.

## Diagnosis

- **Symptom mismatch:** connections stuck in CLOSE_WAIT (`ss -tan state close-wait`) while
  the peer has clearly finished, combined with an epoll-based server, strongly implies an
  ET drain bug.
- **Buffered-but-unread bytes:** `ss -tmi` shows per-socket receive-queue (`Recv-Q`)
  sizes. A socket with persistent non-zero `Recv-Q` that your app isn't reading is a
  smoking gun for under-draining.
  ```bash
  ss -tan 'state established' | awk '$2 > 0' # Recv-Q column non-zero
  ```
- **Syscall trace:** `strace -e trace=epoll_wait,read,accept4,close -p <pid>` - look for
  `read` returning a full buffer with no subsequent `read` until `EAGAIN`, or `accept4`
  called once after an edge with a backlog present.
- **eBPF:** trace `epoll_wait` return counts vs. `read` counts per fd to spot fds that got
  an edge but insufficient reads.

## Mitigations and the robust design

**The cure** is to honor the ET contract: non-blocking sockets, drain-to-`EAGAIN`" on both
read and write, loop `accept`, and inspect the full event mask including
`EPOLLRDHUP`/`EPOLLHUP`/`EPOLLERR`. Encapsulate this in a single, well-tested I/O routine
that every connection uses, so no hand-written loop can get it wrong.

**Pragmatic alternative:** if the team can't guarantee ET discipline across a large
codebase, **use level-triggered mode.** LT is far more forgiving - a partial read just
means the fd is reported again next cycle, so under-draining costs a little efficiency
instead of stranding the connection. Many high-performance servers (including nginx by
default) use LT successfully; the ET efficiency gain is often marginal compared to the
correctness risk, Choosing LT 1s a legitimate senior engineering decision, not a cop-out:
**you trade a small, bounded overhead for the elimination of an entire bug class.**

**Defense in depth regardless of mode:**

- An **idle/lifetime reaper** that force-closes connections with no progress past a
  deadline, Even a stranded ET fd gets reclaimed, converting a permanent leak into a
  bounded one - the same safety net that protects against missed closes generally.
- **`EPOLLONESHOT`** in multithreaded designs so an fd is handled by exactly one thread
  per arming, then explicitly re-armed after full drain - this prevents a different but
  related bug where two workers race on the same, fd.
- Per-connection **read/write deadlines**, so a stalled transfer surfaces as a timeout you
  can 1og and act on rather than an invisible hang.

## The architect's takeaway

ET epoll moves the readiness-tracking contract from kernel to application: **the kernel
notifies you once per state transition, and it is now your job to fully consume that
readiness- drain reads and writes until AGAIN*100p your accepts, and honor the
complete event mask.** Violate that contract and the failure isn't a dropped notification
you'll retry; it's a connection that vanishes from your event loop permanently, surfacing
downstream as hung requests and a CLOSE_WAIT surge. The senior instinct is threefold:
encapsulate ET I/O in one correct primitive so the contract can't be violated ad hoc;
consider LT when you'd rather trade a little efficiency for immunity to this bug class;
and always run a reaper so that any missed edge degrades into bounded resource use instead
of an outage.

_______________________________________________________________________

## Interview Q&A

**Q: Explain level-triggered versus edge-triggered epoll in one precise sentence each.**
A: Level-triggered reports readiness as a *condition* - as long as the socket buffer is
non-empty, every `epoll_wait` keeps reporting the fd as readable. Edge-triggered reports
readiness as a *transition* - you're notified only when the state changes from not-ready
to ready, and if you don't fully drain on that one edge the kernel has no obligation to
tell you again. *Follow-up:* So what does ET require of your code that LT doesn't? ET is a
contract: you must loop until the syscall returns `EAGAIN`, because partial draining
silently strands the fd. *Red flag:* Describing ET as "fires on every event" or confusing
which side owns the drain obligation.

**Q: What is the single most dangerous ET bug and how does it manifest downstream?** A:
Under-draining - reading once and stopping instead of looping to `EAGAIN`. If the peer
sent its final bytes and a FIN, there's no future edge, so `epoll_wait` never returns that
fd again: the request never completes, the EOF is never observed, and you never `close()`.
The fd sits in CLOSE_WAIT indefinitely, so at scale the ET drain bug and a CLOSE WAIT
surge are frequently the same bug viewed from two angles. *Follow-up:* why is this worse
than a dropped LT notification? Under LT a partial read just gets re-reported next cycle
(a little inefficiency); under ET it's a permanent, silent loss of the fd. *Red flag:*
Treating a missed ET edge as a retryable delay rather than a permanent strand.

**Q: What are the non-negotiable rules for correct ET usage?** A: The socket must be
non-blocking (`accept4(..., SOCK_NONBLOCK|SOCK_CLOEXEC)`) or the drain loop deadlocks the
whole worker; drain reads until `EAGAIN`; drain writes symmetrically on `EPOLLOUT` edges;
loop `accept4` until `EAGAIN` on the listening socket; and inspect the full event mask
(`EPOLLIN|EPOLLRDHUP|EPOLLHUP|EPOLLERR|EPOLLOUT`) since one edge can coalesce several
conditions. Every ET bug is a violation of one of these. *Follow-up:* Why must `accept`
also loop under ET? A burst of connections produces one edge; if you accept once, the
backlog drains only at the rate of new arrivals, so connections sit unaccepted and clients
see latency or timeouts. *Red flag:* Using ET on a blocking socket, or reading/accepting
exactly once per wakeup.

**Q: You suspect an ET under-drain in production. What do you check?** A: Combine
symptoms: `ss -tan state close-wait` showing sockets stuck while the peer clearly
finished, on an epoll server, points at an ET drain bug. Then look for buffered-but-unread
bytes with `ss -tmi` - a persistent non-zero `Recv-Q` your app isn't reading is a smoking
gun (`ss -tan 'state established'| awk'$2 > 0'`). Confirm with
`strace -e trace=epoll_wait,read,accept,close -p <pid>` to see a full-buffer `read` with
no follow-up read until `EAGAIN`", or eBPF comparing `epoll_wait` edges to `read` counts
per fd. *Follow-up:* Why is the "partial progress with intermittent traffic" variant so
nasty? On a keep-alive connection, each new request creates a fresh edge that happens to
flush previously stranded bytes, so the bug hides in testing and only shows as sporadic
stalls under specific timing. *Red flag:* Debugging by staring at code without using
`ss -tmi`/Recv-Q or syscall tracing to localize the strand.

**Q: When would you deliberately choose level-triggered over edge-triggered?** A: When the
team can't guarantee ET drain discipline across a large codebase. LT is far more forgiving
- an under-drain just means the fd is reported again next cycle, so you trade a small,
bounded efficiency cost for immunity to the entire lost-event bug class. Many
high-performance servers (nginx by default) use LT successfully; the ET efficiency gain is
often marginal next to the correctness risk, so choosing LT is a legitimate senior
decision. *Follow-up:* If you do use ET, how do you keep it safe across the codebase?
Encapsulate ET I/O in one well-tested primitive every connection uses, so no hand-written
loop can violate the drain-to-`EAGAIN` contract. *Red flag:* Treating ET as mandatory for
performance and dismissing LT as amateurish.

**Q: Why is `EPOLLONESHOT` relevant in a multithreaded epoll design, and what's the
defense-in-depth regardless of mode?** A: `EPOLLONESHOT` ensures an fd is handed to
exactly one thread per arming and must be explicitly re-armed after a full drain,
preventing two workers from racing on the same connection. Regardless of ET/LT, run an
idle/lifetime reaper that force-closes connections with no progress past a deadline, so
even a stranded fois reclaimed -turning a permanent leak into a bounded one - and add
per-connection read/write deadlines so a stalled transfer surfaces as a loggable timeout
instead of an invisible hang, *Follow-up:* Does `EPOLLONESHOT` remove the drain
obligation? No - you st111 must fully drain before re-arming; it only serializes which
thread handles the fd, not whether readiness is consumed, *Red flag:* Assuming
`EPOLLONESHOT` or a reaper substitutes for honoring the ET drain contract.
