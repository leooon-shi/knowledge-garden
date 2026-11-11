<!-- Interview/close_wait_surge.md -->

# The Kernel Truth Behind a CLOSE_WAIT Surge in a C++ Access Gateway

See also: `tcp_connection_lifecycle_scaling-md` (accept queues, TIME_WAIT, port
exhaustion, SO_REUSEPORT, backpressure).

## What CLOSE_NAIT actually means at the kernel level

The single most important fact - the one that immediately localizes the bug - is this:

>  **CLOSE_WAIT is a state that only exists on the side that received a FIN but has not yet
>   called `close()`. A surge of CLOSE_WAIT sockets is almost never a network problem or a
>   peer problem. It is your own application failing to close file descriptors.**

To see why, walk the TCP state machine. Normal shutdown is a four-way handshake:

```
Peer (active close)              Your gateway (passive close)
     |------ FIN --------------------->|   ESTABLISHED -> CLOSE_WAIT
     |<----- ACK ----------------------|   (kernel auto-ACKs the FIN)
     |                                 |   <-- app must now call close()
     |<----- FIN ----------------------|   CLOSE_WAIT -> LAST_ACK
     |------ ACK --------------------->|   -> CLOSED
```

When the peer closes first, its FIN arrives. **The kernel automatically ACKs that FIN and
moves the socket to CLOSE_WAIT.** Then it waits. The only thing that advances the socket
out of CLOSE_MAIT is *your application calling `close()` (or `shutdown()`) on that fle
descriptor*, which triggers the kernel to send your FIN and move to LAST_ACK.

SO CLOSE_WAIT literally means: *the kernel is waiting for the application to close this
socket."* A pile of them is the kernel telling you, precisely, that your gateway has
leaked or stalled file descriptors. The peer has hung up; you haven't.

Contrast this with **TIME_WAIT**, which people often confuse it with. TIME_WAIT is on the
*active* closer, is entirely kernel-managed, is supposed* to exist (2xMSL, to absorb
stray retransmits and prevent old-connection confusion), and resolves on its own.
TIME_WAIT is usually benign. CLOSE_WAIT is almost always a bug in *your* code, and it
never resolves on its own.

## Why a surge is dangerous

Each CLOSE_WAIT socket holds:

- A **file descriptor»* – bounded by the process `RLIMIT_NOFILE` and system-wide
  `fs.file-max`. When you exhaust the fd limit, `accept()` and `open()` start returning
  `EMFILE`/`ENFILE`. The gateway can no longer accept new connections *or open backend
  sockets, log files, or config files*. This is the classic failure cascade: a slow leak
  that looks fine for hours, then a cliff.

- **Kernel socket memory** - receive/send buffers (`sk_buff`, `tcp_mem`). Thousands of
  stuck sockets pin non-trivial kernel memory.
- A **conntrack entry** if netfilter is in the path - `nf_conntrack` table fills, and once
  full the kernel drops packets with `nf_conntrack: table full, dropping packet` in dmesg,
  harming *unrelated* traffic.

Because the socket sits in CLOSE_WAIT indefinitely (there is no kernel timeout for it -
unlike TIME_WAIT), the leak is monotonic. You get a slow accumulation that ends in fd
exhaustion and a hard outage.

## The root causes in a C++ gateway

Every root cause reduces to *an fd received a FIN and the code never reached `close()`."*
In a gateway specifically, watch for these patterns:

**1. Missed `close()` on an error path.** The most common. The happy path closes the
socket; an exception, early `return`, or `goto fail` skips it.

```cpp
int fd = accept(listen_fd, ...);
if (process (fd) < 0)
    return -l;          // <-- LEAK: fd never closed on error
    close (fd);
```

The fix is RAII - make it structurally impossible to skip:

```cpp
struct FdGuard {
    int fd;
    explicit FdGuard (int f) : fd(f) {}
    ~FdGuard() { if (fd>0) ::close(fd); }
    FdGuard(FdGuard && o) noexcept : fd(o.fd) { o.fd = -l; }
    FdGuard &operator=(FdGuard&&) = delete;
    FdGuard(const FdGuard&) = delete;
};
// Now every exit path closes the fd, including exceptions.
```

**2. The event loop never learns the peer closed.** In an `epoll`-based gateway, when the
peer sends FIN, `epoll` reports `EPOLLIN` (and often `EPOLLRDHUP`), If you only handle
`EPOLLIN` by reading, and a `read()` returning `0` (EOF) is not explicitly treated as
"peer closed → I must close too," the fd stays registered and stuck in CLOSE_NAIT forever.

```cpp
ssize_t n = read(fd, buf, sizeof(buf));
if (n == 0) {                    // EOF = peer sent FIN
epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
close (fd):                   // <-- the close that exits CLOSE_WAIT
return;
}
```

Explicitly register `EPOLLRUP` so you get a clean half-close signal and can react
inmediately:

```cpp
ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
```

**3. Half-close asymmetry in a proxy.** A gateway bridges ellentbackend. When the client
sends FIN, a naive proxy stops reading from the client but keeps the *backend* connection
open (and vice versa), because it's waiting for the other direction to finish. If the
shutdown-propagation logic is wrong, one leg sits in CLOSE_WAIT. Correct proxies propagate
the half-close: on client EOF, `shutdown(backend_fd, SHUT_WR)` to forward the FIN, and
only fully close when both* directions are done. Getting this state machine right is the
crux of proxy correctness.

**4. Blocked or starved worker,** The connection-handling thread is blocked - on a slow
backend `read()` with no timeout, on a lock, on a full queue - so it never reaches the
`close()`. The FIN arrived, the kernel parked the socket in CLOSE_WAIT, and your worker is
stuck upstream of the close call. Here the CLOSE_WAIT count is a symptom* of a stall, not
a leak per se.

**5. Ownership ambiguity / double-tracking.** The fd is handed to a connection object, a
timer, and the epoll set. Nobody is clearly the owner, so on teardown each assumes another
will close it, and none does. A single owner (`std::sunique_ptr` with a custom deleter, or
a connection object whose destructor closes) removes the ambiguity.

**6. FD leak via `fork`/`exec` without `CLOEXEC`.** If the gateway spawns children,
sockets without `O_CLOEXEC` (or `accept4(..., SOCK_CLOEXEC)`) are inherited. The child
holds a duplicate reference; even after the parent closes, the *kernel refcount* on the fd
stays > 0, so the FIN is never sent and the socket lingers. Always
`accept4(fd, ..., SOCK_CLOEXEC)`.

## How to diagnose it fast

The goal is to go from "CLOSE_WAIT is high" to "this line of code / this fd" quickly.

**Confirm and count the state:**

```bash
ss -tan state close-wait | we -l          # fast, modern
ss -tan state close-wait | head           # see the peers
#Which local port (which listener/service) is accumulating them?
ss -tanp state close-wait | awk '{print $4)' | sort | uniq -c | sort -rn
```

**Attribute to the process and see the leaked fds:**

```bash
ss -tanp state close-wait                  # shows users: (("gateway", pid-1234, fd=57))
ls -l /proc/1234/fd | wc -l                # current fd count
cat /proc/1234/1imits | grep 'open files'  # the ceiling you're racing toward
lsof -p 1234 | grep CLOSE_WAIT
```

**Watch the trend** (a leak is monotonic; a stall plateaus and drains):

```bash
watch -n1 'ss -tan state close-wait | wc -l'
```

**Trace the missing close in production** with eBPF, without a debugger:

```bash
# Who is (not) calling close? Trace close) and correlate with accept()
sudo bpftrace -e 'tracepoint:syscalls:sys_enter_close { @[comm] = count(); }'
# Or watch tep state transitions into CLOSE_WAIT
sudo bpftrace -e 'kprobe:tcp_set_state { /* inspect newstate == TCP_CLOSE_MAIT */ }'
```

`strace -f -e trace=accept4,close,read,shutdown -p <pid>` on a canary process will show
you accepts without matching closes directly.

## What to do - mitigation vs, cure

There's a hard distinction to make, and it's the mark of a senior response: **only fixing
the code cures a CLOSE_WAIT Leak, Everything else buys time.**

### The cure (application)

- **RAII ownership of every fd.** One owner, destructor closes, move-only. This
  structurally eliminates causes 1 and 5.
- **Treat `read() == 0` and `EPOLLRDHUP` as first-class events** that drive the socket to
  close. Fixes cause 2.
- **Correct half-close propagation** in the proxy path with `shutdown(SHUT_WR)` and a
  two-sided done-state. Fixes cause 3.
- **Timeouts on every blocking operation** - connect, read, write, and an idle-connection
  reaper. A gateway must never wait forever on a backend. Fixes cause 4 and bounds the
  blast radius of any future bug.
- **`SOCK_CLOEXEC` on every accepted/opened fd.** Fixes cause 6.
- **An idle/lifetime reaper** as defense-in-depth: a timer wheel that force-closes
  connections exceeding a max idle or max lifetime. Even 1f a close is missed, the reaper
  eventually reclaims the fd. This converts a fatal leak into a bounded, self-healing
  condition.

### Mitigation (buys time; does not fix the bug)

- **Raise `RLIMIT_NOFILE`** (`ulimit -n`, systemd `LimitNOFILE=`) - pushes the cliff
  further out, doesn't remove it.
- **Raise `nf_conntrack_max`*** and tune `nf_conntrack_tep_timeout_close_wait` (default
  60s) so netfilter reaps its contrack entry sooner - but note this only cleans
  *conntrack state*, not your process fd. The socket in your process is still stuck.
- **TCP keepalive** (`SO_KEEPALIVE` + `tcp_keepalive_time/intv1/probes`) helps detect
  *dead* peers that vanished without a FIN, but it does **not** clear CLOSE_WAIT - by
  definition the peer already sent a FIN, so the connection isn't dead-silent; your app
  just hasn't closed. Keepalive is the wrong tool for this specific state, a common
  misconception worth calling out.
- ***TCP_USER_TIMEOUT**• bounds how long unacked data lingers - useful for stuck
  LAST_ACK/sending, again not the CLOSE_WAIT root.

The important nuance: because there is **no kernel timeout for CLOSE_WAIT**, kernel
tunables cannot save you the way they can with TIME WAIT. The kernel is deliberately
deferring to your application. That is why the reaper (application-level timeout) is the
only reliable safety net short of fixing the specific missed-close.

## The architect's framing

At the system level, a CLOSE_WAIT surge is a resource-lifecycle failure, and I'd treat it
as three concentric problems:

1. **Correctness:** enforce single-owner RAII fd lifecycle and correct half-close
   semantics so closes cannot be skipped. This is the real fix.
2. **Resilience:** every connection gets a timeout and every gateway runs an idle/lifetime
   reaper, so any *future* missed-close degrades gracefully into bounded resource use
   instead of an outage. Design for the leak you haven't found yet.
3. **Observability:** export CLOSE_WAIT count and process fd-utilization
   (`open_fds / max_fds`) as first-class metrics with alerts at, say, 70% fd utilization -
   *before* `EMFILE`. Because the leak is monotonic and slow, a trend alert catches it
   hours before the cliff, turning a 3 a.m. outage into a ticket.

And the one-line truth to anchor the whole discussion: **CLOSE_WAIT is the kernel waiting
on your `close()`. If it's piling up, your code owns an fd it forgot to release - find the
missing close, and add a reaper so the next missing close can't page you.**

_______________________________________________________________________

## Interview Q&A

**Q: What does a socket in CLOSE_WAIT actually mean at the kernel level?** A: It means the
peer sent a FIN, the kernel auto-ACKed it and moved the socket to CLOSE_WAIT, and it's now
waiting for *your application* to call `close()` (or `shutdown()`), which triggers your
FIN and the move to LAST_ACK. So a pile of CLOSE_WAIT sockets is almost never a network or
peer problem - it's your code failing to close file descriptors. The peer hung up; you
haven't. *Follow-up:* How is that different from TIME WAIT? TIME WAIT is on the *active*
closer, is entirely kernel-managed, is supposed to exist (2xMSL) to absorb stray
retransmits, and resolves on its own; CLOSE_WAIT has no kernel timeout and never resolves
until your app closes. *Red flag:* Blaming the peer or the network for a CLOSE_WAIT surge.

**Q: Why is a CLOSE_WAIT surge dangerous, and why does the failure look like a sudden
cliff?** A: Each stuck socket pins a file descriptor (bounded by
`RLIMIT_NOFILE`/`fs.file-max`), kernel socket-buffer memory, and a contrack entry if
netfilter is in the path. Because there's no kernel timeout for CLOSE_WAIT, the leak is
monotonic - it looks fine for hours, then you hit the fd ceiling and `accept()`/`open()`
start returning `EMFILE`/`ENFILE`, so the gateway can't accept connections *or* open
backends, logs, or configs. That's the cliff: a slow leak that ends in a hard outage.
*Follow-up:* What unrelated damage can the conntrack table cause? Once `nf_conntrack`
fills, the kernel drops packets (`nf_contrack: table full, dropping packet`), harming
traffic that has nothing to do with the leak. *Red flag;* Assuming the leak will
self-corrector that kernel tunables will reclaim the process fd.

**Q: Give the most common root cause in a C++ gateway and the structural fix. ** A: A
missed `close()` on an error path - the happy path closes the socket, but an exception,
early `return`, or `goto fail` skips it, The structural fix is RAII: wrap the fd in a
move-only guard whose destructor calls `::close()`, so every exit path, including
exceptions, releases it, Combine single-owner RAII with treating
`read() == 0`/`EPOLLRDHUP` as a first-class "peer closed → I must close" event.
*Follow-up:* In an epoll loop, what specific event must drive the close? `read()`
returning 0 (EOF) and/or `EPOLLDHUP`; 1f you only handle `EPOLLIN` by reading and never
treat EOF as "close," the fd stays stuck in CLOSE_WAIT forever, *Red flag:* Relying on a
single happy-path `close()` with no RAII and no EOF handling.

**Q: Walk me through diagnosing a live CLOSE_WAIT surge from "It's high" to "this
process/fd.** A: Count and attribute with `ss`: `ss -tan state close-wait | wc -l` to
confirm, then `ss -tanp state close-wait` to get the owning process
(`users:(("gateway",pid=1234,fd=57))`). Inspect the process: `ls /proc/1234/fd | wc -l`
for current fd count against `cat /proc/1234/11m1ts | grep 'open files'`, Watch the trend
with `watch -n1 'ss -tan state close-wait | wc-l'` - a leak climbs monotonically, a stall
plateaus. For the missing close itself, `bpftrace` on
`tracepoint:syscalls:sys_enter_close` by comm, or
`strace -f -e trace-accept,close,read,shutdown -p <pid>` on a canary to see accepts
without matching closes. *Follow-up:* How do you distinguish a leak from a transient
stall? A leak grows without bound; a stall (blocked worker) plateaus and then drains once
the worker unblocks, so the trend shape tells you which root cause family you're in. *Red
flag:* Only looking at the aggregate count and never attributing it to a pid/fd or
checking the trend.

**Q: A proxy leg sits in CLOSE_WAIT even though the fd-close code looks correct. What's
special about the proxy case?** A: Half-close asymmetry. A gateway bridges client↔backend;
when the client sends FIN, a naive proxy stops reading the client but keeps the backend
open waiting for the other direction, so one leg lingers in CLOSE_WAIT. Correct proxies
propagate the half-close: on client EOF, " `shutdown(backend_fd, SHUT_WR)` to forward the
FIN, and only fully close when *both* directions are done. Getting that two-sided
done-state machine right is the crux of proxy correctness. *Follow-up:* Can a blocked
worker cause the same symptom without any close bug? Yes - if the handler is blocked on a
backend `read()` with no timeout, the FIN arrives, the kernel parks the socket in
CLOSE_WAIT, and the worker never reaches `close()`; here CLOSE_WAIT is a symptom of a
stall. *Red flag:* Forwarding FIN with a full `close()` instead of `shutdown(SHUT_WR)`,
tearing down the still-active reverse direction.

**Q: Why won't TCP keepalive or kernel tunables fix a CLOSE_WAIT leak, and what's the real
safety net?** A: Keepalive detects *dead, silent* peers that vanished without a FIN - but
in CLOSE MAIT the peer already sent a FIN, so it's not dead-silent; your app just hasn't
closed, And because there's no kernel timeout for CLOSE_WAIT, sysctls can't reap the
socket the way they do for TIME_WAIT; raising `RLIMIT_NOFILE` or `nf_conntrack_max` only
buys time (and contrack timeouts clean contrack state, not your process fd). The only
real fix is in the application: RAII fd ownership plus correct half-close; the reliable
safety net is an idle/lifetime reaper (a timer wheel) that force-closes stale connections,
converting any future missed-close into a bounded, self-healing condition, *Follow-up:*
What observability turns a 3 a,m, outage into a ticket? Export CLOSE_NAIT count and fd
utilization (`open_fds/max_fds`) as metrics with a trend alert at ~70% - the leak is slow
and monotonic, so it fires hours before `EMFILE`, *Red flag;* Proposing `SO_KEEPALIVE` or
raising ulimits as the *fix* rather than a mitigation.
