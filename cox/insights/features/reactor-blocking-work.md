
<!-- cox/insights/features/reactor-blocking-work.md -->

# Reactor + Blocking Work: The Problem and Solution Space

> An engineering guide for reasoning about blocking/CU-bound work in a
> single-threaded-per-loop reactor (like Cox), and how to choose among the
> available solutions.

## TL;DR

- A reactor runs all I/O and callbacks for a loop on **one thread**. Any
  callback that blocks freezes **every connection on that loop**.
- There is no single fix; there is a **solution space**. Pick per workload:
  - I/O-bound with async APIs available -> use them (make it an fd).
  - CPU-bound or blocking-only APIs -> **offload to a worker thread pool**.
  - Big in-loop algorithms you control -> **chunk/incremental** processing.
  - Ergonomics of async composition -> coroutines (sugar over async I/O; does
    NOT solve blocking/CPU work).
  - Isolation/scale -> multi-process (SO_REUSEPORT).
- For a C++ library with no M:N runtime scheduling, the **thread pool is
  the necessary escape hatch** for genuinely blocking/CPU work. Coroutines are
  optional and solve a different problem.

## The problem, concretely

When data arrives, the io loop thread runs the user callback synchronously:
```
io loop thread: poll() -> handleRead() -> message_callback_(conn, buf) -> poll() -> ...
```

The thread does nothing else until the callback returns. If the callback
blocks, the loop cannot:

- read/write any other connection on that loop,
- fire timers (idle timeouts, drain deadlines),
- process runInLoop() tasks,
- honor a graceful-shutdown force-close.

With N io loops (one-loop-per-thread pool), one blocking callback wedges all
connections pinned to that loop (~1/N of them under round-robin). Several
blocking callbacks can stall the whole server.

### What "blocking" means (the real culprits)

- Synchronous DB / RPC calls.
- Disk I/O (read/write/sync) -- can stall tens of ms.
- CPU-heavy work: compression, encryption, image processing, large JSON parse,
  password hashing (bcrypt/argon2 are intentionally slow).
- Synchronous DNS (getaddrinfo) -- can block for seconds.
- Third-party libraries offering only blocking APIs.

## The solution space

### 1. Make the operation non-blocking (avoid the problem)

Never issue a blocking call from the loop. Use async-native libraries that
register their own fd with the reactor and call back on readiness:

- libpq async mode, c-ares (async DNS vs blocking getaddrinfo), hiredis async.
- Philosophy: "make everything an fd" (timerfd, eventfd, signalfd). If the
  resource is an fd the poller waits on, it isn't blocking.
- Tradeoff: ideal when async APIs exist; impossible with blocking-only libs.
  (Node.js is single-threaded for I/O because its libs are async -- but still
  needs a threadpool for fs/DNS where no async fd exists.)

An "async-native library" is one that, for a specific blocking operation,
exposes a non-blocking API driven by an fd + "call merwhen ready" model instead
of a call that blocks. The common operation-by-operation mapping:

| Blocking operation | Blocking API (bad in a loop) | Async-native library |
|--------------------|------------------------------|----------------------|
| DNS resolution     | `getaddrinfo()`              | c-ares (gives you an fd) |
| PostgresQL         | libpq `PQexec()`             | libpq async: `PQsendQuery` + socket readiness |
| Redis client       | blocking hiredis             | hiredis async (`redisAsynccontext`) |
| HTTP client        | blocking curl                | libcurl multi interface (fds + poll) |
| MySQL              | blocking `mysql_query`       | libmariadb non-blocking API |

The integration pattern: the async-native library hands your reactor an fd; you
register it as a Channel in the EventLoop; when it becomes readable/writable
your callback drives the library forward. No thread, no blocking -- the
operation was turned into an fd the poller waits on. The reactor drives
readiness; the operation library exposes fd + callback.

### 2. Chunk the work （cooperative yielding）

Decompose a big computation into slices; yield to the loop between slices via
runInLoop/queueInLoop so other work interleaves.

- Redis does this: rehashing, key expiration, SCAN are incremental (a little
  per tick) to avoid blocking.
- Tradeoff: only for CPU work you control and can decompose; cannot chunk a
  blocking syscall (e.g. a slow disk read()).

### 3. Coroutines / async-await (ergonomics over async I/O)

Straight-line code that suspends the coroutine (not the thread) at await points.

- C++20 coroutines, Rust async/.await (tokio), Python asyncio, Kotlin, C#.
- Really a developer-ergonomics layer over #1: the await point registers
  interest with the reactor and resumes when ready.
- Tradeoff: great for I/O-bound composition; does NOT help CPU-bound work (a
  coroutine doing heavy CPU still hogs the thread) or blocking-only syscalls.
  Adds machinery (promise_type, awaiters) and lifetime hazards (coroutine
  frames outliving connections).

### 4. Offload to a worker thread pool (the escape hatch)

Post blocking/CPU work to a separate bounded pool; marshal the result back to
the connection's loop with runInLoop (preserving thread affinity):

```
io loop thread:                    worker pool thread:
  message_callback_
    pool.enqueue(work) ---------->   result = blockingwork(data)
    return immediately               loop->runInLoop([result]{
    (loop keeps serving)               conn->send(result);   <-- back on io loop
  ...poll() continues...             });
```

Variants:

- Shared pool: libuv uv_queue_work, Netty DefaultEventExecutorGroup.
- Dedicated background threads for specific tasks: Redis "bio" threads
  (close(), fsync(), lazy-free).
- Per-loop worker vs global pool (global simpler; per-loop avoids contention).

Bonus: a BOUNDED queue is also an overload signal -- when full, shed load
(reject) instead of growing an unbounded backlog toward ooM. This is why the
thread pool naturally precedes overload/admission control.

Tradeoff: introduces threads, synchronization, and the result-marshaling hop.

### 5.Process-level isolation

Multi-process, each with its own loop (nginx workers, HAProxy; Redis is
single-threaded-per-process). A blocking op in one worker doesn't stall others.

- SO_REUSEPORT lets N processes share a listen port with kernel load balancing.
- Tradeoff: strong isolation, no shared-memory races; higher memory/IPC cost;
  does not fix a worker blocking its OWN connections.

### 6. Dedicated thread with its own Loop

Give a blocking subsystem its own EventLoopthread; it blocks only itself.

- Degenerate case of #4 using an event-loop thread instead of a task pool.
- Tradeoff: fine for a few long-lived blocking sources; doesn't scale to many
  concurrent short blocking ops.

### 7. Runtime M:N scheduling

Go goroutines: a blocking syscall parks the goroutine's os thread; the scheduler
uses another thread for others ("blocking is fine").

- Tradeoff: best ergonomics, but a language/runtime feature -- NOT available to
  a C++ library.

### 8. Timeouts / deadlines (damage control)

Not a way to avoid blocking, but to bound its damage: deadline the operation so
a stuck call can't hang the loop forever. Pairs with any of the above.

## How real systems combine these

- Node.js: async libs (#1) + libuv threadpool (#4) for fs/DNS.
- Redis: single-threaded loop + incremental algorithms (#2) + bio threads
  (#4-variant) + multi-process for scale (#5).
- Nginx: async everywhere (#1) + `aio threads` pool (#4) for disk +
  multi-process workers (#5).
- Netty: async I/O + EventExecutorGroup (#4) for blocking handlers.
- tokio (Rust): coroutines (#3) + spawn_blocking onto a blocking pool (#4).

## Decision for Cox

Constraints: C++20 library, no runtime M:N scheduling (so #7 is out).

- #1 (async-native) + #3 (coroutines) is the strategic direction for I/O-boun
  ergonomics -- BUT does not solve cPu-bound or blocking-only-API work.
- #4 (thread pool) is the NECESSARY escape hatch for genuinely blocking/CPU
  work, and its bounded queue feeds overload control.
- #2 (chunking) is a per-feature technique (like Redis), not a library
  primitive.
- #5 (multi-process) is deployment-level; already enabled via SO_REUSEPORT.

These are complementary, not either/or.

### Coroutines: decision to DISCARD (for now)

Rationale: coroutines solve async-I/O ergonomics, a DIFFERENT problem from
blocking work. Dropping them costs no capability -- the API stays callback-based
(as in muduo/libevent/Netty core). Benefits: simpler codebase, no C++20
coroutine machinery or frame-lifetime footguns, faster path to features that
matter for production. The blocking-work thread pool remains required regardless.

what you give up: nicer straight-line composition for multi-step protocols
(write a small state machine instead).

### Chosen next step

Implement the **bounded worker thread pool** (#4):

- Fixed worker count + bounded task queue.
- enqueue() returns/handles rejection when the queue is full (backpressure hook
  for overload control).
- Result marshaled back to the owning EventLoop via runInLoop (preserves
  TcpConnection thread affinity).

## Rule of thumb

- Can the resource be an fd or use an async API? - #1 (and optionally #3 for
  ergonomics).
- Is it CPU-bound or blocking-only?               -> #4 (thread pool).
- Is it a big algorithm you control?              -> #2 (chunk it).
- Need isolation/horizontal scale?                -> #5 (multi-process).
- Always: bound it with a deadline                -> #8.

If you reach for a thread to run "I/O", first ask whether that I/O can be an fd
instead. If you reach for a thread to run CPU work, that's legitimate -- the
loop must never do heavy CPU inline.

## References

- Cox source: reactor/event_loop.cc (runInLoop), reactor/event_loop_thread*.
- Libuv threadpool (uv_queue_work), Redis bio.c, nginx aio threads,
  Netty EventExecutorGroup, tokio spawn_blocking.
- Companion doc: docs/insights/features/poll-timeout-strategy.md.
 