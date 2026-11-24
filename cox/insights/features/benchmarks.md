
<!-- cox/docs/insights/features/benchmarks.md -->

# Feature: Benchmark Suite

## Purpose

Measure Cox's actual throughput and latency, so performance claims rest on
numbers and optimizations can be judged rather than guessed at.

## Background

Cox exists because event-driven architecture is supposed to be fast. That is a
testable claim, and an untested one tends to rot: a change that quietly adds a
syscall per read, or an extra allocation per message, is invisible to a
correctness test suite. The suite answers three separate questions, which is why
there are three benchmarks rather than one:

1. Does the **reactor** schedule work cheaply?
2. Does the **buffer** move bytes cheaply?
3. Does the **whole TCP stack** deliver throughput and acceptable latency under
   concurrent load?

## Design & technology choices (the journey)

### Closed-loop,real server, real sockets

`echo_throughput_bench` starts an actual `Tepserver` echo endpoint and drives it
with client connections over real loopback sockets. Each client is
**closed-loop**: send a request, wait for the echo, then send the next. This
matters for what the numbers mean - an open-loop generator that fires regardless
of responses measures how fast you can queue work, and its latency figures
become meaningless once the server falls behind. Closed-loop measures the
server's actual service rate, and its latency percentiles are real.

The clients deliberately use plain blocking `poll()`/`read()`/`write()` rather
than Cox's own client stack, so the measurement does not depend on the thing
being measured.

### Percentiles, not averages

The benchmark reports **p50/p90/p99/max** alongside QPS. An average latency
hides the tail, and the tail is what users experience - a mean of 1ms with a p99
of 200ms is a bad server that looks fine. Percentiles also make regressions in
scheduling fairness visible, which a mean would smooth away.

### Parameterized, to expose scaling shape

```
echo_throughput_bench [connections] [duration_s] [msg_size] [server_threads]
```

The interesting output is not a single number but how it *moves*: varying
`server_threads` shows whether the one-loop-per-thread model
(features/threading-model.md) actually scales; varying `msg_size` separates
per-message overhead (syscalls, dispatch) from per-byte cost (copies,
throughput).

### Micro-benchmarks to localize a regression

- `buffer_bench` exercises `Buffer` append/retrieve cycles in isolation, since
  the buffer sits on every read and write path (features/buffer.md).
- `reactor_benchmark` measures `runInLoop` task-scheduling cost.
- `thread_pool_bench` measures `Threadpool` task throughput and the effect of
  the dequeue **batch size** (features/reactor-blocking-work.md).

When the end-to-end number moves, these say whether the cause is the buffer, the
loop, or the network path.

### ThreadPool dequeue batching

A worker pool's adversarial workload is a flood of *trivial* tasks: the
per-task work is cheaper than the synchronization around it, so lock cost
dominates. A naive pool locks the queue once per task - N tasks cost N
lock/unlock cycles shared across the workers. `ThreadPool` amortizes this by
draining a batch of tasks per lock acquisition (`setTakebatchSize`, default 32),
capped at a fair share of the backlog so slow tasks still spread across idle
workers rather than being hoarded by one.

`thread_pool_bench` sweeps the batch size so the trade-off is a measurement
rather than an assertion:

```
thread_pool_bench [worker_threads] [num_tasks] [producers]
```

The `producers` argument matters. With a single producer the *enqueue* side
(still one lock per task) is the bottleneck and the queue stays near-empty, so
workers rarely have a batch to drain and the batch size barely moves the number.
Several producers keep the queue backed up - the regime where dequeue batching 
pays off, and the regime a real "burst of millions of tasks" workload is in.

### Kept out of the default build

Benchmarks live in `examples/benchmark/` and compile only with
`--build-examples`. They are diagnostic tools, not part of the library or the
test suite, and a long-running load generator has no business in CI's default
path.

## API surface
```bash
./build.sh --release --build-examples

# End-to-end TCP: 100 connections, 5s, 128B messages, 1 server thread
./build/bin/echo_throughput_bench
# Scaling: 200 connections, 10s, 1KB messages, 4 io threads
/build/bin/echo_throughput_bench 200 10 1024 4

./build/bin/buffer_bench        # Buffer append/retrieve throughput
./build/bin/reactor_benchmark   # runInLoop scheduling cost

# ThreadPool throughput, sweeping dequeue batch size:
# 10 workers, 10M trivial tasks, 4 producer threads
./build/bin/thread_pool_bench 10 10000000 4
```

## Usage

Always benchmark a **release** build - a Debug build is `-O0` and additionally
uses `poll(-1)` rather than a bounded timeout
(features/poll-timeout-strategy.md), so its numbers describe a different
program. Compare like-for-like: same machine, same flags, one variable at a
time.

Indicative loopback numbers (Release, dev machine): ~84K QPS single-threaded and
~234K QPS with 4 io threads at 128B echo; ~77 MB/s at 1KB single-threaded.

Indicative `thread_pool_bench` numbers (10 workers, 10M trivial tasks, 4
producers), from this environment:

```
compiler: clangt+ 18.1.3
build: Release (NDEBUG)
C++ std:  202002 (C++20)
hw threads: 8
```

| batch | tasks/s | speedup |
|------:|--------:|--------:|
|     1 |   ~1.45M |  1.00x |
|     4 |   ~1.90M |  1.31x |
|     8 |   ~2.14M |  1.48x |
|    16 |   ~1.96M |  1.35x |
|    32 |   ~1.66M |  1.14x |

The benchmark prints this environment block at the top of every run, so
a pasted results table always carries the provenance needed to interpret it (a
number from 8 hardware threads means something different from one off 64).
Dequeue batching buys a real but modest ~1.5x on this adversarial all-trivial
workload, peaking around batch 8 and tapering as larger batches trade
lock-amortization for coarser load-balancing and worse cache locality. The
default of 32 is a conservative middle ground, not the throughput optimum -
the best value is workload-dependent (task cost, producer count, worker count),
which is exactly why it is a knob (`settakebatchSize`) rather than a constant.
Real tasks do more work per task than a single atomic increment, so their
relative gain from batching is smaller; the win is largest precisely when tasks
are cheap and plentiful.

## Caveats

- **These numbers are for relative comparison only.** They are machine-,
  kernel-, and build-dependent; treat them as a baseline to detect regressions,
  never as an absolute performance claim.
- Loopback is not a network: it has no real NIC, no driver, no RTT, and no packet
  loss. It measures the stack's CPU cost, not wire behavior.
- client and server share the same machine and compete for cores; at high thread
  counts the generator itself becomes a factor.
- Echo is the cheapest possible workload - no parsing, no application logic. Real
  services will be slower, and HTTP/TLS paths are not covered here at all.
- No automated regression gate: nothing fails a build when a number drops.
  Comparison is manual today.

## References
- Source: `examples/benchmark/echo_throughput_bench.cc`,
  `examples/benchmark/buffer_bench.cc`
  `examples/benchmark/reactor_benchmark.cc`
  `examples/benchmark/thread_pool_bench.cc`.
- features/buffer.md, features/threading-model.md,
  features/poll-timeout-strategy.md, features/reactor-blocking-work.md.
