<!-- interview/coroutine_multicore_ownership_transfer.md -->

# C++20 Coroutines on Multi-Core Hosts & Cross-Thread Ownership Transfer

## Part 1 - How coroutines work on a multi-core host

**Crucial fact: C++20 coroutines are a *control-flow* mechanism, not a *concurrency*
mechanism.** By themselves they give suspension/resumption on a single thread - nothing is
inherently multi-threaded or parallel. Multi-core execution comes entirely from *who
resumes the coroutine and on which thread* - the **scheduler/executor**, not the coroutine
machinery.

**Runtime objects:**

- A **coroutine frame** - heap-allocated (unless elided) block: promise, suspended locals,
  resume/destroy pointers.
- A `std::coroutine_handles<>` - non-owning pointer to that frame.
- `handle.resume()` runs the coroutine **on whatever thread calls it**, from the last
  suspension until the next suspension/completion.

**How multi-core execution arises:**

- A coroutine suspends (`co_await`) and its handle goes to a **scheduler** pool workers
  pick up ready handles and `resume()`. The same* coroutine can thus run on **different
  threads across its lifetime** - it migrates each time a different worker resumes it.
- **Parallelism across coroutines:** N coroutines resumed by M workers run genuinely in
  parallel on multiple cores. Coroutines are the units of work; the pool provides the
  cores.
- **A single coroutine is still sequential** - never on two cores at once; suspension
  points are the only hand-off points. Between suspensions it can land on any core.

**Nulti-core hazards:**

- **No thread affinity.** After `co_await` you may resume on a different thread. Taking a
  lock before `co_await` and releasing after is dangerous (acquired on one thread,
  released on another - UB; also held across suspension indefinitely).
- **`thread_local` is a trap.** Not stable across a suspend (thread changed). Cache
  nothing thread-specific across `co_await`.
- **The frame is shared mutable state once its handle is visible to another thread.**
  Resuming *writes* the frame. Two threads resuming/touching the same handle concurrently
  → frame data race. **Only one thread may resume a given coroutine at a time** -
  enforced by the scheduler, not the language.
- **Memory ordering / publication.** Handing a handle from suspender to resumer needs
  release/acquire so the resumer sees the fully-written frame.
- **False sharing / allocator contention.** Per-core/thread-local frame pools avoid malloc
- contention and cache bouncing (see `coroutine_frame_malloc.md`).

So: coroutines on multi-core - **a scheduler resuming handles on pool threads**, with each
frame resumed by at most one thread at a time and every hand-off a properly-synchronized
ownership transfer.

## Part 2- Transferring coroutine ownership between threads

"Ownership" = two things: (a) **lifetime ownership** - who holds and eventually destroys
the frame; (b) **execution ownership** - who has the right to resume it next. Both must
transfer safely.

**What's transferred:** a `std::coroutine_handle<P>` - a 1-pointer, trivially copyable.
Moving it between threads is moving a pointer; the hard part is **synchronization + the
single-resumer invariant**, not the copy.

### Canonical mechanism - reschedule via "co_await" on a scheduler

``` cpp
struct resume_on {
    ThreadPool& pool;
    bool await_ready() const noexcept { return false; }       // always suspend
    void await_suspend(std::coroutine_handle<> h) noexcept {
        pool.enqueue(h);   // hand "who resumes next" to the pool
    }                       // this thread returns; a pool worker resumes h
    void await_resume() const noexcept {}
};

task example(ThreadPool& pool) {
    // on thread A ...
    co_await resume_on(pool);       // suspend; a pool thread resumes the rest
    // now on some pool thread B ...
}
```

Transfer sequence:

1. Coroutine suspends -> `await_suspend` runs **on the source thread** with the handle.
2. It **enqueues the handle** onto the target pool's ready-queue (the push is the
   **release**).
3. The source thread returns from `resume()` and **must not touch the handle again** - it
   gave up execution ownership.
4. A target worker **dequeues** (the **acquire**) and `resume()`s, continuing the
   coroutine there.

The concurrent queue's push/pop establishes the happens-before edge so the resumer sees
frame state correctly. The awaiter *is* the ownership-transfer primitive.

### Invariants to maintain

- **Single resumer at a time (mutual exclusion on resume).** Exactly one thread holds the
  right to resume; the hand-off must be atomic (source relinquishes via enqueue, target
  acquires via dequeue). Enqueuing to two queues or racing resumes - frame data race /
  double-resume (UB).
- **Resume-once per suspension.** A suspended coroutine must be resumed exactly once; a
  double-resume corrupts the frame.
- **Lifetime ownership & destruction.** Someone must eventually `handle.destroy()` (or let
  it complete). Be explicit whether you transfer *execution only* (frame still owned by a
  `task` elsewhere) or *full* ownership (target now destroys). Destroying a frame while
  another thread references its handle • use-after-free.
- **Memory ordering on the queue** - enqueue/dequeue must be acquire/release (a correct
  MPMC/SPSC queue provides it), else the resumer sees a half-written frame.

### Common cross-thread transfer bugs

- **Double resume** (two workers race the same handle) > frame corruption.
  Fix: ready-queue hands each handle to exactly one worker.
- **Resuming a destroyed frame** -> use-after-free. Fix: clear ownership policy; don't
  destroy while a resume may be pending.
- **Lock/RAII guard held across the transfer** -> acquired on A, released on B. Fix: never
  hold thread-affine resources across `co_await`.
- **`thread_local`/affine state assumed stable** across the hop. Fix: re-read after
  resume; don't cache.
- **Missing hand-off synchronization** (raw handle through a non-synchronized channel) ->
  frame data race. Fix: synchronized queue.
- **Symmetric-transfer vs. cross-thread confusion:** symmetric transfer (returning a
  handle from `await_suspend` to tail-resume) stays on the same* thread; cross-thread
  requires *enqueuing* to another thread, not returning the handle.

### Patterns safe by construction

- **Executors/schedulers own resumption** - app code never calls `resume()` directly; it
  `co_await`'s a scheduler that centralizes single-resumer + synchronization (libunifex,
  stdexec, Asio, copcoro, Folly coro).
- **Structured concurrency** - a parent `co_await`'s its children so lifetimes nest and
  ownership is unambiguous; avoids detached coroutines destroyed from the wrong thread.
- **Per-core frame arenas + remote-free path** - created on core A, destroyed on core B
  needs an MPSC return queue (the allocator concern from the coroutine-malloc answer).

## Staff / Principal-level framing

- **Coroutines are orthogonal to threading; the scheduler supplies concurrency.** A
  coroutine is a resumable function object; *where* it runs is entirely the executor's
  decision. Expecting affinity or parallelism from coroutines alone is the root
  misconception.
- **A handle is shared mutable state; transfer it like a lock.** Invariant: exactly one
  thread resumes a frame at a time, each suspension resumed once, every hand-off
  acquire/release-synchronized. Frame data races and double-resume are the dominant,
  *silent* (UB) bugs.
- **Separate execution ownership from lifetime ownership.** "Who resumes next" vs. "who
  destroys" are distinct; conflating them (destroy while resume in flight; two threads
  both owning resumption) causes the cross-thread bugs.
- **Don't hand-roll the scheduler for production.** Correct cross-thread
  resumption/cancellation/lifetime is hard; use a mature executor and let the
  `co_await scheduler` awaiter be your only ownership-transfer primitive.
- **Same discipline as all prior concurrency topics** - hand-off = publication
  (release/acquire), single-resumer = mutual exclusion, double-resume - data race,
  per-core pools with remote-free - the MVC/coroutine-malloc reclamation pattern.

## Interview Q&A

**Q: Are C++20 coroutines a concurrency mechanism? Where does multi-core execution come
from?** A: No - C++20 coroutines are a control-flow mechanism (suspension/resumption), not
a concurrency mechanism. By themselves they give single-threaded suspend/resume; nothing
is inherently multi-threaded or parallel. Multi-core execution comes entirely from *who
resumes the coroutine and on which thread* - the scheduler/executor - not the coroutine
machinery, `handle.resume()` runs the coroutine on whatever thread calls it. *Follow-up:*
Can a single coroutine run on two cores at once? - No; a single coroutine is always
sequential and resumed by at most one thread at a time, though it can migrate to a
different core after each suspension point. *Red flag:* Expecting parallelism or thread
affinity from coroutines alone.

**Q: How does parallelism arise across coroutines, and how does a single coroutine migrate
between threads?** A: A coroutine suspends at `co_await` and its handle goes to a
scheduler; pool workers pick up ready handles and `resume()`, So the *same* coroutine can
run on different threads across its lifetime - it migrates each time a different worker
resumes it, with suspension points the only hand-off points, Parallelism across coroutines
comes from coroutines being resumed by M workers, running genuinely in parallel;
coroutines are the units of work and the pool supplies the cores, *Follow-up:* Between
suspensions, which core does it run on? - Whichever worker resumed it; there's no
affinity, so you can't assume it stays put, *Red flag:* Thinking a coroutine pins to the
thread that created it.

**Q: Why is a `thread_local` a trap in a coroutine, and what else is unsafe across a
`co await` ?** A: After a `co await` the coroutine may resume on a different thread, so
`thread_local` state read before the suspend may not be the same after it - it's not
stable across a suspend. Similarly, taking a lock before `co_await` and releasing after is
dangerous: it may be acquired on one thread and released on another (UB) and is held
across the suspension for an unbounded time. Don't cache anything thread-specific across
`co_await`; re-read after resume, *Follow-up:* Why is a lock
acquired-before/released-after a suspend specifically UB? - A mutex must be unlocked by
the thread that locked it; after migration a different thread may attempt the unlock. *Red
flag:* Holding RAII locks or caching thread-affine state across suspension points.

**Q: What does "transferring coroutine ownership between threads" actually involve?** A:
Two distinct things: lifetime ownership (who holds and eventually destroys the frame) and
execution ownership (who has the right to resume it next) - both must transfer safely.
What's physically transferred is a `std::coroutine_handle<P>`, a one-pointer, trivially
copyable value, so moving it between threads is just moving a pointer. The hard part is
the synchronization and the single-resumer invariant, not the copy. *Follow-up:* Why
separate execution ownership from lifetime ownership? - Conflating them causes bugs like
destroying a frame while a resume is in flight, or two threads both believing they own
resumption. *Red flag:* Treating handle transfer as a trivial pointer move while ignoring
synchronization and the resume invariant.

**Q: Walk me through the canonical cross-thread transfer via `co_await` ing a scheduler,
and the happens-before edge.** A: In the awaiter's `await_suspend`, running on the source
thread with the handle, you enqueue the handle onto the target pool's ready-queue (the
push is the release); the source thread returns from `resume()` and must not touch the
handle again - it gave up execution ownership. A target worker dequeues (the acquire) and
calls resume()", continuing the coroutine there. The concurrent queue's push/pop
establishes the happens-before edge so the resumer sees the fully-written frame. The
`co_await scheduler` awaiter *is* the ownership-transfer primitive. *Follow-up:* What
memory ordering must the queue provide? - Acquire/release (a correct MPMC/SPSC queue
provides it); otherwise the resumer may see a half-written frame. *Red flag:* Passing a
raw handle through an unsynchronized channel, or the source touching the handle after
enqueuing.

**Q: Debugging scenario - a handle gets enqueued to two worker queues and the frame
corrupts. Name the invariant violated and the related silent bugs.** A: This violates the
single-resumer invariant: exactly one thread may resume a given coroutine at a time, and
each suspension must be resumed exactly once. Enqueuing to two queues leads to a
double-resume, which races on and corrupts the frame - a silent UB bug. Related silent
bugs: resuming a destroyed frame (use-after-free), holding a lock/RAII guard across the
transfer (acquired on A, released on B), and missing hand-off synchronization (frame data
race). *Follow-up:* How is symmetric transfer different from cross-thread transfer? -
Symmetric transfer returns a handle from `await_suspend` to tail-resume on the *same*
thread; cross-thread requires *enqueuing* to another thread, not returning the handle.
*Red flag:* Confusing symmetric transfer with cross-thread hand-off, or not naming the
single-resumer invariant.

**Q: Design trade-off - what patterns are safe by construction for multi-core
coroutines?** A: Let executors/schedulers own resumption so app code never calls
`resume()` directly - it `co_await`'s a scheduler that centralizes the single-resumer
guarantee and synchronization (libunifex, stdexec, Asio, cppcoro, Folly coro). Use
structured concurrency so a parent `co_await`'s its children and lifetimes nest
unambiguously, avoiding detached coroutines destroyed from the wrong thread. For
allocation, use per-core frame arenas with an MPSC remote-free path for
create-on-A/destroy-on-B. *Follow-up:* What's the one-line principal framing? - A handle
is shared mutable state; transfer it like a lock (single resumer, resume-once,
acquire/release on every hand-off), and don't hand-roll the scheduler for production. *Red
flag:* Hand-rolling cross-thread resumption, or using detached/fire-and-forget tasks with
unclear destruction ownership.

## Synthesis

C++20 coroutines are a **single-threaded control-flow mechanism** (suspension/resumption)
with **no inherent concurrency**; multi-core execution comes entirely from a **scheduler
resuming handles on pool workers**, so a single coroutine can migrate across threads
between suspension points (never on two cores at once) while many coroutines run in
parallel across cores, The runtime object is a heap **frame** addressed by a non-owning
`coroutine_handle`, and resuming it *writes* the frame - so the governing invariant is
**exactly one thread may resume a coroutine at a time, each suspension resumed once, every
cross-thread hand-off synchronized**, **Transferring ownership across threads** is done
idiomatically by `co_await` ing a scheduler: `await_suspend` **enqueues the handle** onto
the target executor (release), the source relinquishes it and must not touch it again, and
a target worker **dequeues** (acquire) and resumes - the concurrent queue providing
happens-before, Separate **execution ownership** (who resumes) from **lifetime ownership**
(who destroys), never hold locks/`thread_local`/RAII across `co_await` (thread may
change), and never double-resume or resume a destroyed frame, Principal view: coroutines
are orthogonal to threads, a handle is shared mutable state transferred like a lock, and
the safe path is to let a mature executor own resumption via the `co_await scheduler`
primitive - the hand-off being the same publication/mutual-exclusion discipline as every
earlier concurrency topic.
