
# Feature: Worker Thread Pool (blocking-work offload)

## Purpose

Run blocking or CPU-bound work off the event loop so a slow operation cannot
stall the loop and every connection sharing it.

## Background

In a reactor, all callbacks for a loop run on one thread. A blocking call (DB,
disk, DNS, crypto, heavy CPU) in a callback freezes that entire loop. The fix is
to offload such work to a separate worker pool and post the result back to the
connection's loop. Full problem/solution-space analysis:
features/reactor-blocking-work.md.

## Design & technology choices (the journey)

### why a thread pool (vs. alternatives)

Among the solution space (async-native APIS, chunking, coroutines, thread pool,
multi-process, runtime MiN), only the thread pool solves genuinely cu-bound and
blocking-only-API work in a C++ library with no MiN runtime. Coroutines were
discarded (they solve async-I/O ergonomics, a different problem). So the pool is
the necessary escape hatch.

### Bounded queue = backpressure + load signal

The task queue is **bounded**. when full:

- `run()` blocks the producer (backpressure),
- `tryRun()` returns false immediately (load-shedding signal).

This makes the pool's "full" condition a direct input to overload control:
reject early rather than grow an unbounded backlog toward OOM.

### Drain-on-stop

`stop()` sets running-false, wakes all workers, and workers keep taking tasks
until the queue is empty - so queued work is not silently dropped on shutdown.

### Result marshaling

The pool does not know about loops. A task posts its result back with
`loop->runInLoop(...)`,preserving TopConnection thread affinity. This
separation keeps the pool a general-purpose primitive.

### Zero-threads inline mode

`start(0)` runs tasks inline in the caller - handy for tests and degenerate
configs, same API.

## API surface

```cpp
ThreadPool pool("name"); 
pool.setMaxQueueSize(1000);   // 0 = unbounded
pool.start (numThreads);      // 0 = inline
pool.run(task);               // blocks if full
bool ok = pool.tryRun(task);  // false if full (shed)
pool.stop();                  // drains then joins
```

## Usage (offload from a message callback)

```cpp
server.setmessageCallback([&pool](const TcpConnectionPtr& conn, Buffer* buf) {
    std::string req = buf-›retrieveallAsString();
    pool.run([conn, req] {
        auto resp = doBlockingWork(req);                 // off the loop
        conn->getLoop()->runInLoop([conn, resp] {
            conn->send(resp);                            // back on the loop
        });
    });
});
```

# Caveats

- Tasks are responsible for their own exception handling.
- Choose queue size deliberately: it is your backpressure boundary.
- Always marshal results back with `runInLoop` - never touch a connectiop from
  a worker thread directly.

## References

- Source: `concurrent/thread_pool.{h, cc}`.
- features/reactor-blocking-work.md (full solution-space rationale).
