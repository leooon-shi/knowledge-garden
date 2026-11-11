<!-- interview/go_primitives_in_cpp20_poc.md -->

# PoC Report: Implementing Go's Channel / Select / WaitGroup in C++20

**How to take on an architect-level reproduce Go concurrency primitives in C++20" task:
assessment -> design -> implementation -> reliable reporting.**

Source: `interview/code/go_primitives.cpp` (verified: builds clean with `-Wal1 -Wextra`,
all self-checks pass, and **ThreadSanitizer run is clean** - no data races; select
fairness measured at ratio ~9.49-0.50 over 20,000 trials).

________________________________________________________________________

## Framing: this is a de-risking exercise, not a library build

A PoC'3 job is to prove the semanties are achievable in C++20, surface the hard problems
early, and produce a report a decision-maker can trust. That drives everything: time-box
it, attack the risky semantics first (Select), be explicit about what is and isn't
covered, and make the reliability evidence (tests, sanitizers, benchmarks) first-class.

________________________________________________________________________

## Phase 1 - Early Assessment (before code)

### 1.1 Pin down the real requirement

Clarify with the requester:

- **Goal:** feasibility study, performance comparison, or seed for an internal 11brary?
  Sets the bar.
- **Fidelity target:** exact Go semantics vs. "close enough"? The biggest scoping lever.
- **Concurrency substrate:** OS threads (honest PoC) vs. a user-space M:N scheduler
  (goroutine-like). Go channels are meaningless without cheap goroutines; a faithful Poc
	needs a "cheap task" concept. **Decision:** default to `std::jthread` and explicitly
	document that we do NOT replicate goroutine scheduling.

### 1.2 Study the semantics we must reproduce (the spec)

- **Channel:** typed; **buffered** (cap N, send blocks when full) vs **unbuffered**
  (rendezvous - send blocks until a receiver takes it); multiple concurrent
	senders/receivers; **close** rules (send-on-closed panics; rec drains buffered values
	then returns zero value + `ok-false`; double-close panics): range-until-closed.
- **Select:** wait on multiple ops; if several ready, choose **one uniformly at random**
  (anti-starvation - critical, easy to miss); `default` makes it non-blocking; otherwise
	blocks until exactly one case proceeds.
- **WaitGroup:** `Add`/`Done`/`Wait`; counter must never go negative (panic in Go); `Wait`
  returns at zero.

### 1.3 Map Go concepts to C++20 and flag gaps

| Go concept                | C++20 mapping                           | Risk / gap                                                         |
| ------------------------- | --------------------------------------- | ------------------------------------------------------------------ |
| goroutine                 | `std:jthread` (or pool/task)            | No cheap M:N scheduling; threads are heavy                         |
| unbuffered chan           | mutex + condvars (rendezvous handshake) | Trickiest to get right                                             |
| buffered chan             | deque + mutex + condvars                | Straightforward                                                    |
| `select`                  | wait on multiple channels               |	**Highest risk** - cross-channel natification * fair random choice |
| `close`                   | flag + notify_all                       | Must define our error model                                        |	
| `WaitGroup`               | atomic/int + condvar                    | Easiest; `std::latch` is close but single-use                      |
| zero-value on closed recv | `std::optional<T>` (nullopt-closed)     | Cleaner than Go; document deviation                                |

### 1.4 Top risks named up front

- **`select` fairness + correctness** is the dominant risk. Per-channel condvars don't
  compose; need a **shared notifler** any channel signals, re-check readiness under lock,
	and **uniform-random** tiebreak.
- **Deadlocks / lost wakeups** from lock ordering across channels in select.
- **Close races** (close concurrent with send/recv/select).
- **No goroutines** - one oS thread per "goroutine" caps scale; stated as a known
  limitation, not hidden.

### 1,5 Error model (explicit deviation)

Go panics; we use `std::optional<T>` for recv (empty = closed) and
`throw std::logic_error` for programmer errors (send-on-closed, double-close, negative
WaitGroup). Documented as a deliberate deviation.

### 1.6 Time-box and success criteria

E.g. "1 week; success = buffered + unbuffered channels, select with default + fairness.
WaitGroup, all passing under TSan, plus a producer/consumer benchmark. " Define *done*
before coding.

________________________________________________________________________

## Phase 2 - Design

- **Layered header library:** `WaitGroup` (build first, validates harness) -> `ChanneL<T>`
  -> `select`.
- **Cannel:** `std::mutex`, `std::dequet<T>` buffer, `capactty`, `closed` flag,
  `not_full`/`not_empty` condars. Unbuffered = rendezvous (sender blocks until the item
	is consumed). API exposes `send/try_send/recv/try_recv/close` plus `ready_to_rec()` and
	a register notifier() hook for select.
- **Select (the crux):** each channel holds a shared `Notifier` (mutex+condvar) signaled
  on every state change. The select loop scans ready cases under the notifier, picks one
  **uniformly at random**, executes it; if none and a default exists, takes default;
	else waits on the notifier and re-scans (guarding spurious wakeups). Simplified vs Go's
	per-case wait queues - called out as such.
- **WaitGroup:** counter + condvar; throw if it would go negative; `wait` blocks until
  zero.
- **Deviations table** treated as a first-class design artifact.

________________________________________________________________________

## Phase 3- Implementation

Correctness-first order: **WaitGroup -> Channel -> Select** (simple to risky), Fu11 code
in `interview/code/go_primitives.cpp`. Highlights:

- Unbuffered rendezvous modeled as capacity-1 with a post-enqueue wait until the buffer
  drains (sender doesn't return until a receiver consumes).
- `recv()` returns `std::optional<T>`; `nullopt` after close+drain enables
  `while (auto v = ch.recv()) ...` (Go's range-until-closed).
- Select Usto shared `Notifler` + uniform-random tiebreak among ready cases.

________________________________________________________________________

## Phase 4- Verification & Making the Report Reliable

**Reliability of the report = reliability of the evidence.**

### 4.1 Correctness tests mapped to the Go spec (one per clause)

Implemented as in-binary self-checks (all passing):

- WaitGroup waits for all workers; negative counter throws.
- Buffered channel FIFO producer/consumer (sum 1..10 - 55).
- Unbuffered rendezvous (send returns only after recv).
- Close: recv drains then "nullopt; send-on-closed throws.
- Select: •default" when nothing ready; picks the ready case; **fairness** - two
  always-ready channels chosen ~50/50 over 20,000 trials (measured 0.49-0.50).

### 4.2 Concurrency correctness - non-negotiable evidence

- **ThreadSanitizer: ** whole suite run under `-fsanitize=thread` -> **clean, no data
  races** (verified). A concurrency PoC without a clean TSan run is not credible.
	(ASan/UBSan recommended likewise.)
- **Stress/soak** thousands of producers/consumers with randomized timing, repeated runs,
  to shake out rare races (the fairness loop already does 20k iterations of concurrent-ish
	select).
- **Deadlock guard:** watchdog timeouts so hangs fail loudly rather than block CI.

## 4.3 Benchmarks (if perf in scope)

Producer/consumer throughput and send/rec latency vs. an equivalent Go program on the
same hardware, with methodology (warmup, iterations, percentiles). **Caveat stated:** C++
uses os threads vs. Go goroutines - apples-to-oranges; not a verdict on the languages.

### 4.4 Reliability practices for the report itself

- **Reproducibility:** exact compiler/version/flags, OS, CPU; one-command build+test;
  commit hash. (Build lines are in the source header.)
- **State assumptions and deviations prominently** (the Go-vs-PoC table). Never imply full
  Go fidelity.
- **Report negative results honestly** - what doesn't work, races found/fixed, scale
  limits (thread-per-goroutine ceiling).
- **Separate proven from believed** - "fairness verified statistically over 20k selects"
  vs. "believed to scale but untested beyond N threads."
- **Quantify confidence"** and list residual risks + productization needs.

### 4.5 Deliverables

1. Header-style Poc + self-checks + (optional) benchmarks + build instructions.
2. This report: goal, scope, design, semantics-mapping, results (tests/TSan/fairness),
   deviations, limitations, risks, recommendation.
3. A reproduce section.

________________________________________________________________________

## Known limitations of this PoC (stated honestly)

- **No goroutine scheduler** - one 0S thread per concurrent task; won't scale to Go's
  millions of goroutines, The largest fidelity gap,
- **`select` supports recv-cases** (the common form); send-cases and mixed send/recv
  select are future work.
- **`select` notifier is coarse** (broadcast re-scan with a 50 ms safety poll) rather than
  Go's precise per-case walt queues - correct and fair, but not optimally efficient.
- **`try_send` on unbuffered** is conservatively simplified.
- **Error model differs** (optional + exceptions vs, panics) by design.
- Performance not yet benchmarked vs. Go.

## Residual risks & path to productization

- Replace thread-per-task with an M: task scheduler / fiber 11brary for goroutine-like
  scale.
- Harden `select` with per-case walt queues; add send-cases, timeouts (`time.After`), and
  cancellation ('context).
- Consider lock-free /bounded-MPMC channels for throughput once semantics are locked.
- Expand stress/soak and add fuzzing of interleavings; run TSan in CI on every commit.

________________________________________________________________________

## Staff/Principal judgment threaded through

- **Scope discipline is the whole game for a Poc** - explicitly cutting goroutine
  scheduling beats half-implementing it.
- **Attack the riskiest unknown first** (select fairness/correctness), not the easiest
  (WaitGroup), If select proved infeasible in the time-box, that would be the headline
	finding.
- **Evidence over assertion** - "it works" = a TSan-clean suite + a fairness histogram,
  reproducible on demand.
- **Semantics before performance** - a fast but subtly-wrong channel is worse than
  useless.
- **Write limitations and deviations as prominently as successes** - that honesty is what
  makes the report reliable and actionable.

## Interview Q&A

**Q: You're asked to "reproduce Go's channel/select/WaltGroup in Ct420." How do you frame
the task?** A: As a de-risking exercise, not a library build. A Poc's job is to prove the
semantics are achievable in C+20, surface the hard problems early, and produce a report a
decision-maker can trust. That framing drives everything: time-box it, attack the riskiest
semantics first (select), be explicit about what is and isn't covered, and make
reliability evidence (tests, sanitizers, benchmarks) first-class. *Follow-up:* What's the
first thing you clarify with the requester? - The real goal (feasibility vs. performance
comparison vs. seed for a library) and the fidelity target (exact Go semantics vs. "close
enough"), since fidelity is the biggest scoping lever. *Red flag:* Diving into coding a
full-fidelity library without pinning the goal, scope, or time-box.

**Q: Why is the concurrency substrate decision the crux, and what did the Poc decide?** A:
Go channels are meaningless without cheap goroutines, so a faithful PoC needs a "cheap
task concept. The choices are os threads (honest but heavy) vs. a user-space M:N
scheduler (goroutine-like). The PoC defaulted to `std::jthread` and explicitly documented
that it does NOT replicate goroutine scheduling - one oS thread per "goroutine" - stating
this as a known limitation rather than hiding it. *Follow-up:* Why is that the largest
fidelity gap? - Threads are heavy, so the PoC can't scale to Go's millions of goroutines;
it caps at one oS thread per concurrent task. *Red flag:* Pretending `std::thread` gives
goroutine-like scale, or half-implementing an MN scheduler inside the time-box.

**Q: What exactly are the Go semantics you must reproduce for channels and close?** A:
Channels are typed and either buffered (capacity N, send blocks when full) or unbuffered
(rendezvous - send blocks until a receiver takes the value), with multiple concurrent
senders/receivers. Close rules: send-on-closed panics, receive drains any buffered values
then returns the zero value with `ok=false`, double-close panics, and range-until-closed
works. The PoC models recv as `std::optional<T>` (nullopt = closed-and-drained), enabling
`while (auto v = ch.recv()) ...`. *Follow-up:* How is unbuffered rendezvous modeled? - AS
capacity-1 with a post-enqueue wait until the buffer drains, so the sender doesn't return
until a receiver consumes the item. *Red flag:* Forgetting that receive must drain
buffered values before reporting closed, or that double-close panics.

**Q: Why is "select" the highest-risk piece, and how is it implemented correctly?** A:
Because per-channel condvars don't compose - you must wait on multiple channels at once,
and if several cases are ready you must choose one uniformly at random (anti-starvation,
easy to miss). The PoC gives each channel a shared `Notifier` (mutex+condvar) signaled on
every state change; the select loop scans ready cases under the notifier, picks one
uniformly at random, and executes it. If none are ready and a `default` exists, it takes
default; otherwise it waits on the notifier and re-scans, guarding against spurious
wakeups, *Follow-up:* Why is uniform-random tiebreak critical, and was it verified? -
Without it you get starvation; the PoC measured two always-ready channels chosen ~50/50
over 20,000 trials (0.49-0.50), verifying fairness statistically. *Red flag:* Picking the
first ready case deterministically, or using uncomposable per-channel waits.

**Q: What makes the PoC report "reliable," and why is a clean ThreadSanitizer run
non-negotiable?** A: Reliability of the report equals reliability of the evidence. The PoC
maps one correctness test to each Go spec clause (WaltGroup waits/negative throws,
buffered FIFO summing 1..10-55, unbuffered rendezvous, close drain-then-nullopt, select
default/ready/fairness) as in-binary self-checks, all passing. A concurrency PoC without a
clean TSan run isn't credible - the whole suite was run under `-fsanitize=thread` and came
back clean (no data races), complemented by randomized stress/soak and deadlock-guard
watchdogs so hangs fall loudly, *Follow-up:* What makes the report itself reproducible? -
Exact compiler/version/flags, OS, CPU, a one-command build+test, and the commit hash; plus
separating "proven" (fairness over 20k selects) from "believed" (scales but untested
beyond threads), *Red fLag; *Reporting "it works" without TSan evidence, or omitting
reproducibility details and deviations.

**Q: Debugging scenario- in a multi-channel select, threads occasionally hang or miss a
ready channel, What's the root cause and fix?** A: The classic failure is lost wakeups and
uncomposable notification: with per-channel condvars, a channel becoming ready on one
condvar doesn't wake a thread parked elsewhere, and lock-ordering across channels can
deadlock. The fix is a shared notifler any channel signals on state change, re-checking
readiness under the lock after each wake (guarding spurlous wakeups), plus a bounded
safety poll so a missed signal can't hang forever. The PoC's notifler is coarse (broadcast
re-scan with a 50 ms safety poll) - correct and fair, but not optimally efficient.
*Follow-up:* What's the honest 1imitation of that notiffer design? - It's coarser than
Go's precise per-case wait queues, so it trades efficiency for simplicity and correctness.
*Red flag:* Blaming spurious wakeups alone without the
uncomposable-per-channel-notification root cause.

**Q: Design/judgment - what principles should guide a PoC like this, and what are the
residual risks to productization?** A: Scope discipline is the whole game (explicitly
cutting goroutine scheduling beats half-implementing it); attack the riskiest unknown
first (select fairness/correctness). not the easiest (WaitGroup): value evidence over
assertion (a TSan-clean suite plus a fairness histogram, reproducible on demand); and get
semantics right before performance (a fast but subtly-wrong channel is worse than
useless). Residual risks/productization: replace thread-per-task with an M:N
scheduler/fiber library, harden select with per-case wait queues plus
send-cases/timeouts/cancellation, consider lock-free bounded-MPMC channels, and run TSan
in CI on every commit. *Follow-up:* Why write limitations as prominently as successes? -
That honesty is what makes the report trustworthy and actionable; separating proven from
believed lets the decision-maker gauge confidence. *Red flag:* Optimizing performance
before semantics, or burying the goroutine-scheduling gap and error-model deviations.

## Synthesis

Treat it as de-risking, not library-building. **Assess:** pin the real goal, study Go's
exact semantics (buffered vs unbuffered, close/drain, select's uniform-random fairness,
WaitGroup's non-negative invariant), map each to C+20 primitives, and name the top risks
(select fairness; no goroutines). **Design:** a small layered header library with an
explicit Go-Vs-PoC deviations table and a clear error model. **ImpLement**
correctness-first (WaitGroup -> Channel -> select). **Make the report reliabless** with
spec-mapped tests, a clean ThreadSanitizer run, randomized stress, reproducible
build/bench instructions, and prominent, honest statements of assumptions, deviations,
limitations, and residual risk - separating what was proven from what is believed. This
PoC achieves that: all semantic self-checks pass and the suite is TSan-clean, with the
goroutine-scheduling gap documented as the primary limitation.
