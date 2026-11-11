# Flow Controller - Detailed Design

> Design of the `flowcontrol/` module in Cox: a token-bucket rate limiter and a
> per-key jittered backoff manager. Covers the design model, data structures,
> control flow, rate math, performance, abnormal cases, and test scenarios.
>
> Companion docs: `docs/insights/features/poll-timeout-strategy.md`,
> `docs/insights/features/reactor-blocking-work.md`

## 1. Purpose and scope

Flow control governs *how fast* work is admitted, as opposed to *how much*
concurrent work exists (that is overload/admission control, a separate layer
built on top of this one). Two complementary primitives:

- **`TokenBucket`** - an admission rate limiter. Answers "may I do this now?"
  at a bounded average rate with controlled bursting. Used for accept-rate
  limiting, per-connection read/write throttling, and request admission.
- **`Backoff`** a per-key retry pacer. Answers "how long should I wait before
  retrying this key after a failure?" with exponential growth + jitter. Used
  for reconnect (Connector), retryable RPC, and any failure-driven retry loop.

Design goals: correctness under concurrency, O(1) hot path, no background
threads/timers, and small dependency surface (standard library only).

Modeld after Kubernetes client-go's `flowcontrol` package: its `RateLimiter`
(a token bucket wrapping `golang.org/x/time/rate`) and its `Backoff` (per-key
exponential backoff with a reset window).

## 2. Design model

### 2.1 Why token bucket (vs. alternatives)

| Algorithm | Bursts? | Smoothing | Memory | Notes |
|-----------|---------|-----------|--------|------|
| Fixed window counter | Yes (2x at edges) | Poor | O(1) | Boundary burst problem |
| Sliding window log | No | Exact | O(events) | Costly memory |
| Sliding window counter | Approx | Good | O(1) | More math |
| **Token bucket** | **Yes, Bounded** | **Good** | **O(1)** | Chosen |
| Leaky bucket (queue) | No | Perfect | O(queue) | No bursting; needs a queue |

Token bucket is chosen because it gives a **bounded average rate** while still
permitting **short bursts** up to the bucket capacity - the behavior real
servers want (absord brief spikes, cap sustained load) - at O(1) memory and
time. Leaky-bucket-as-queue would smooth perfectly but forbids bursts and needs
a backing quue; window counters either burst at boundaries or cost memory.

### 2.2 Token bucket mental model

```
        refill at qps tokens/sec (capped at `burst`)
                       |
                       |
           +----------------------+
 event ->  |    tokens (0..burst) | -> admit if tokens >= 1, tokens -= 1
           +----------------------+ -> else reject
```

- Capacity = `burst`. The bucket starts **full** (allows an initial burst).
- Tokens accrue continuously at `qps`/sec, clamped to `burst`.
- Each admitted event cost 1 token (or N for `tryAcquireN`).

### 2.3 Backoff mental model

Per kev. a duration that **doubles on each failure**. is **capped** at `max`.
is **reset** on success, and is **jiterred** when read so many keys don't retry
in lockstep.

```
failures:    1       2      3       4       5 ...
base(ms):   100  → 200 → 400 →  800 → 1600 ... (capped at max)
returned:   base × random[1, 1+jitter)         (e.g. 100...150ms)
success   → reset → back to initial
```

## 3. Data structure

### 3.1 TokenBucket

```cpp
class TokenBucket {
    const double qps_;              // sustained refill rate (tokens/sec)
    const double burst_;            // capacity (max tokens)
    double tokens_;                 // current tokens in [0, burst_]
    Clock::time_point last_refill_; // steady_clock timestamp of last refill
    std::mutex mutex_;              // guards tokens_ / last_refill_
};
```

key choices:
- **`double` tokens** - allow fractional accrul (`elapsed x qps`) without
  integer rounding drift, and supports fractional `tryAcquireN`.
- **`steady_clock`** - monotonic; immune to wall-clock jumps (NTP, DST).
- **Lazy refill** - no timer; tokens are recomputed from elapsed time on each
  access (see §5.1). O(1) state regardless of rate.

### 3.2 Backoff

```cpp
struct Entry {
    Duration duration;                  // current (un-jittered) backoff
    Clock::time_point   last_update;    // for gc() aging
};
class Backoff {
    const Duration initial_, max_;      // base and cap
    const double jitter_;               // fractinal jitter in [0, 1)
    std::unordered_map<std::string, Entry> entries_;    // per-key state
    uint64_t rng_state_;                // xorshift64 PRNG (jitter)
    std::mutex mutex_;                  // guards entries_ / rng_state_
};
```

key choices:
- **`unordered_map<string, Entry>`** - O(1) average per-key lookup; unbounded in
  principle, bounded in practice by `gc()` (see §7.4).
- **`std::chrono::milliseconds`** - integer ms is sufficient granularity for
  retry pacing and avoids float drift in doubling.
- **xorshift64 PRNG** - tiny, fast, self-contained (no `<random>` global state
  or locking); seeded from the clock so independent instances jitter
  differently.

## 4. How flow is managed & controlled

### 4.1 TokenBucket control flow(`tryAcquireN`)

```
lock
    refillLocked(now):
        elapsed = now - last_refill         // seconds
        tokens = min(burst, tokens + elapsed x qps)
        last_refill = now
    if tokens >= n: tokens -= n; return true    // ADMIT
    else:           return false                // REJECT
unlock
```

Callers decide policy on the boolean:
- **Reject** (drop / 429 / close) - hard rate cap.
- **Defer** - schedule a retry via `Backoff` or a timer.
- **Throttle** - for per-connection I/O, pause reading (disable `EPOLLIN`) until
  a token is available.

### 4.2 Backoff control flow

- On **failure**: `next(key)` (or `nextDelay(key)`) doubles the base, capped.
- On **retry schedulling**: read `get(key)` / use `nextDelay(key)` → jittered 
  delay → arm a timer (`EventLoop::runAfter`).
- On **success**: `reset(key)` clears the entry.
- Periodically: `gc(max_age)` prunes stale keys.

### 4.3 where flow control attaches in Cox

| Attach point | Primitive |  Purpose |
|--------------|-----------|----------|
|`Acceptor` accept loop | TokenBucket | Cap new-connection rate (accept storm) |
|`TcpConnection` read | TokenBucket | Per-connection ingress throttle |
| Request handler | TokenBucket | Application request admission |
| `Connector` reconnect | Backoff | Jittered reconnect (avoid herd)|
| Retryable RPC/Client | Backoff | Per-endpoint retry pacing |

## Rate calculation

### 5.1 lazy (deferred) refill - the core formula

Intead of a timer adding tokens periodically, tokens are computed on demand:

```
elapsed_seconds = (now - last_refill)                        // steady_clock
tokens = min(burst, tokens + elapsed_seconds x qps)
last_refill = now
```

Properties:
- **Exact average rate.** Over any interval T, at most `burst + qpsxT` events
  are admitted. As T grows, the average → `qps`.
- **Bounded burst.** `min(burst, ...)` caps accrual; idle time cannot bank more
  than `burst` tokens.
- **No timer, O(1).** State is (tokens, last_refill); cost is one subtraction,
  one multiply, one min per call.

### 5.2 Worked example

`qps = 100`, `burst = 10`, bucket starts at 10 tokens.
- t=0ms: 10 back-to-back events → all admitted, tokens=0.
- next event immediately → rejected (0 tokens).
- wait 30ms → accrue `0.030 × 100 = 3` tokens → up to 3 more admitted.
- sustained: long-run admit rate converges to 100/s regardless of arrival
  pattern; transient spikes up to 10 are absorbed.

### 5.3 Choosing qps and burst

- **qps** = target sustained throughput (per second). For accept limiting, set
  to max sustainable new-conn rate; for request admission, to capacity.
- **burst** = tolerance for spikes. Larger burst = smoother user experience but
  larger transient overshoot. Rule of thumb: `burst = qps x expected_spike_s`,
  or a small multiple of concurrency. `burst = 1` degenerates to strict spacing
  (no bursting).

### 5.4 Backoff duration math

```
base(n) = min(max, initial x 2^(n-1))       // n = failure count
delay   = base x U[1, 1 + jitter)           // U = uniform random
```

With `initial=1s, max=30s, jitter=0.5`: bases 1,2,4,8,16,30,30,... seconds; each
returned delay is that base times a random factor in [1.0, 1.5). Jitter
**decorrelates** retries: 10 000 clients that all failed at once spread their
retries across a window instead of hammering the server at the same instant.

## 6. Performance impact

### 6.1 Hot-path cost
- **TokenBucket.tryAcquire**: one mutex lock/unlock + a few FP ops. No
  allocation, no syscall, no timer, Sub-microsecond uncontended.
- **Backoff.get/next**: one mutex + one hash lookup (+ possible insert). O(1)
  average; `next` may allocate a map node on first use of a key.

### 6.2 Contention
- Both use a single `std::mutex`. Under very high concurrency this can be a
  contention point. Mitigations (future, if profiling shows need):
  - **Sharded/per-thread-buckets** (e.g. one bucket per io loop) - the natural
    fit for Cox's one-loop-per-thread model; removes cross-thread contention
    for per-loop limits.
  - **Atomics/CAS** token bucket - lock-free variant; more complex, defer until
    justified.
- For per-connection throttles, the bucket is accessed only from that
  connection's loop thread -> effectively uncontended already.

### 6.3 Memory
- TokenBucket: fixed, tiny (two doubles + a timestamp + mutex).
- Backoff: O(number of live keys). Bounded via `gc()`. Without gc, a churny key
  space (e.g. per-client-IP) can grow unboundedly - see §7.4

### 6.4 Clock cost
- `steady_clock` is a VSDO call on Linux (no real syscall) - cheap, but
  called on every access. Acceptable; if it ever dominates, batch or coarsen.

## 7. Abnormal cases & edge handling

### 7.1 Invalid construction
- TokenBucket: `qps <= 0` clamped to 0 (bucket never refills - only the initial
  burst is ever admitted); `burst < 1` clamped to 1.
- Backoff: `initial <= 0` clamped to 1ms; `max < initial` clamped up to
  `initial`; `jitter < 0` clamped to 0.

### 7.2 Clock behavior
- `steady_clock` is monotonic, so wall-clock jumps do not affect rate.
- Defensive `if (now <= last_refill) return;` guards against a non-increasing
  timestamp (e.g. same-instant calls) - no negative accrual.

### 7.3 Fractional / zero / negative token requests
- `tryAcquireN(N<=0)` returns true(no-op admit)
- Fractional N supported via `double` tokens.
- Requesting `n > burst` can **never** succeed (bucket can't hold that many) -
  caller must ensure `n <= burst`.

### 7.4 Backoff map growth (key-space explosion)
- Risk: keying by per-client attribute (IP, connection id) in an adversarial or
  churny environment grows `entries_` without bound.
- Mitigation: call `gc(max_age)` periodically (e.g. from a repeating timer)
  to evict entries not updated within the window. Alternatively key by a bounded
  space (server endpoints), or cap map size with an eviction policy (future).

### 7.5 Thundering herd
- The reason jitter exists. With `jitter=0` (deterministic, for tests), mass
  failures retry in lockstep - acceptable only in tests. Production keeps
  jitter > 0.

### 7.6 Reset races
- `reset(key)` on success while another path calls `next(key)` on a late failure
  can reorder. Both are individually locked (consistent state), but the *logical*
  outcome depends on call order. For per-connection use this is single-threaded
  (the loop), so no race. For shared use, the caller must sequence success vs.
  failure for a key.

### 7.7 Overflow
- Backoff doubling could overflow for absurd failure counts; the `min(.., max_)`
  cap prevents runaway growth long before overflow. `initial x 2^n` reaches
  `max` within ~log2(max/initial) steps (e.g. 1s->30s in 5 doublings).

## 8. Test scenarios

### 8.1 TokenBucket (implemented in `tests/token_bucket_test.cc`)
- **StartsFullUpToBurst** - first `burst` events admitted, next rejected.
- **RefillsOverTime** - after emptying, waiting accrues tokens; next admitted.
- **DoesNotExceedBurst** - long idle does not bank more than `burst`.
- **AcquireN** - multi-token requests consume correctly; over-request rejected.
- **AverageRateIsBounded** - admitted count over a window stays within
  `[~burst, qpsxT + burst]` bounds.

Additional scenarios worth adding:
- **Concurrent acquire** - N threads hammering one bucket admit no more than
  `burst + qpsxT` total (rate integrity under contention).
- **qps=0** - only the initial burst is ever admitted.

### 8.2 Backoff (implemented in `tests/backoff_test.cc`)
- **ExponentialGrowth** - base doubles per failure.
- **CapsAtMax** - never exceeds `max`.
- **ResetCalearsState** - success returns to initial.
- **PerKeyIndependent** - keys to not interfere.
- **JitterWithinRange** - returned delay ∈ [base, basex(1 + jitter)).
- **NextDelayAdvancesAndReturns**   - combined advance+read.
- **GcRemovesStaleEntries** - aging eviction works; fresh entries retained.

Additional scenarios worth adding:
- **Concurrent multi-key** - many threads advancing distinct keys; no data race
  (run under TSan).
- **Jitter distribution** - statistical check that jitter spreads roughly
  uniformly (anti-herd), not just within range.

### 8.3 Integration scenarios (future, as flow control is wired in)
- **Accept-rate cap** - flood connections; verify accept rate ≤ configured qps.
- **Reconnect herd** - kill a server with many connected clients; verify
  reconnect attempts spread over time (jittered), not synchronized.
- **Per-connection throtlle** - a fast sender is paced to the configured
  ingress rate; loop stays responsive.

### 8.4 Test techniques
- Use generous timing slack for CI (scheduling jitter) - assert bounds,
  not exract counts.
- For deterministic backoff-growth assertions, set `jitter=0`.
- Run concurrency tests under ThreadSanitizer to catch data races.

## 9. Relationship to overload control (next layer)

Flow control (rate) is an input to **overload/admission control** (concurrency
+ load schedding):
- TokenBucket rejection is one admision signal.
- The bounded ThreadPool queue (`concurrent/`) full condition is another.
- Backoff paces the *client* side after a server sheds load.

Overload control composes these: e.g. "admit if tokena available ADN  in-flight <
limit AND work-queue not full; else shed (reject early) and let the client back
off (jittered)."

## 10. Summary of design decisions

- Token bucket for rate limiting: bounded average + controlled burst, O(1),
  lazy refill (no timer), `double` tokens, `steady_clock`.
- Per-key exponential backoff with jitter for retry pacing: `unordered_map`
  state, xorshift jitter to break herds, `gc()` to bound memory.
- Single-mutex simplicity now; sharded/per-loop or lock-free later only if
  profiling demands it (Cox's per-loop threading makes per-loop buckets a
  natural, content-free refinement).
- Standard-library-only, no background threads - predictable and portable.

