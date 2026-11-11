-- cpp/exercises/README.md -->
ciuies #t Coroutine exercises （exercise_coro_”）.in
# C++ Exercises
Hands-on practice for the concepts in the
../*.md study docs. Each file is a standalone
program with **TODO stubs** and a **self-checking 'main() **. Fill in the TODOS; when correct, the program prints "All ... checks passed!.
Every stub compiles warning-free with "-Wall -Wextra; the 'assert's fail (or a stub fails fast) until you implement the TODOs. Reference solutions were verified for each exercise.
Two topics so far:
- **Coroutines** (exercise_coro
**) - practice for •../cpp_coroutines.md".
- **Concurrency**
(exercise_conc**) - practice for
./concurrency.md" •
*   Toolchain note: these target **C++20** and are verified on gt+ 13. C++23's "<generator›"
*   and "std: :generator" are not available there, so the generator exercises have you
*  **write the generator type by hand**-which is the point.
## Build & run
••bash
# generators + tasks (no threads)
g++-std=c++20 -Wall -Wextra-o ex exercise_coro_1_generator. cpp && ./ex
# scheduler /when_all use threads
g++ -std=c++20 -Wall -wextra-pthread -o ex ex cpp & ./ex
## How to test your changes
Every exercise **is its own test**: main() is a harness of assert's (and, for concurrency, stress loops that cross-check results against known totals). You don't write tests - you fill in the TODOs, then compile and run.
### The 1oop
***bash
# 1. compile (add -pthread for exercise_conc_* and coro 7/8)
gt+ -std=c++20 -Wall -Wextra -pthread -o ex exercise_conc_3_atomics. cpp
# 2. run
•/ex

## How to test your changes
### The 100p
# 3. read the result (see table below), fix, repeat
### Reading the result
1 Outcome do
Meaning
What to
1
"All
•••
checks passedl, exit e
I every assert passed - your code is correct | done
I Assertion •
..•' failed* + file:line, exit "134" | that specific check failed
shows what was expected
I compile error
| syntax/type problem before tests run
recompile
I open that line; the expression
fix and
1 **hangs** (never returns)
1 a deadlock/livelock in threaded code
I the stubs are designed to *fail
fast, not hang*, so a hang points at your logic (eg. a cv. wait" whose predicate is never true, or a spin loop that never makes progress) |
Example of a failing (unsolved) run - the message names the exact check:
ex: exercise_conc_3_atomics.cpp:81: int main(): Assertion 'c.get() = expected' failed.
Aborted (exit code 134)
#** Extra rigor for the concurrency exercises
Concurrency bugs are **non-deterministic** - one clean run is not proof. Two techniques the harnesses are built to support:
1. **Run many times.** A race may surface only occasionally:
•*bash
for i in $(seq 1 50); do /ex >/dev/null || ( echo "FAILED on run $i"; break; }i done
##Sanitizers.** Build with runtime checkers and rerun:

•**bash
# data races
gt -std=c++20 -fsanitize-thread
# use-after-free / out-of-bounds / UB
-g-pthread -o ex exercise_conc_4_spsc.cpp &&•/ex
gt-std=c++20-fsanitize-address,undefined -g-pthread -o ex exercise_conc_1_queue.cpp &&./ex
Never combine thread" and "address" in one build. On some hardened kernels TSan aborts with "unexpected memory mapping"; if so, "sudo sysct]. vm.mmap_rnd_bits=28, or lean on the built-in stress checks (the harnesses already push 1Mt items and verify sums).
The harnesses are deliberately thorough so mistakes actually get caught - e.g.
•exercise_conc_1_queue" cross-checks a 4-producer/4-consumer run against a known sum, and
"exercise_conc_4_spsc" streams a million items and verifies the total, so a lost item or a wrong memory ordering shows up as a sum mismatch.
## Coroutine exercises ('exercise_coro_*), in order work through them top to bottom - each builds on concepts from the previous one.
##* Tier 1 - Generators ('coyield", lazy pul1)
一#
1 File
concept
一---
1-
1 You implement
| New
11
I exercise_coro_1 generator. cpp* | "Generator<T›' promise + iterator
I promise machinery, lazy "initial_suspend",
coroutine-iterator bridge | 12l exercise_coro_2_compose. cpp*
| take / Ifilter" / map* / zip_sum
infinite source
composing lazy pull pipelines, incl. over an
13 Iexercise_coro_3_tree.cpp
linorder / preorder® / "postorder"
I recursive coroutines, splicing sub-generators,
early termination
*** Tier 2 - Tasks (co_await, "co_return")
|#
| File
concept
I You implement
| New
14
• exercise_coro_4_task.cpp
exception capture |
15
Iexercise_coro_5_chain.cpp"
transfer**
16
"exercise_coro_6_sync_wait.cpp* | sync_wait
•get_return_object, "return_value, result| value-returning promise, lazy start,
Awaiter + FinalAwaiter'
I co_await between tasks via **symmetric | bridging async coroutines to synchronous

main

#** Tier 3- Schedulers & coordination (threads)
1#	1 File	1 You implement
| New
concept		
17	exercise_coro_7_scheduler cpp	I scheduleAwaiter"
poo1**		
18	exercise_coro	_when_all. cpp* | when_all accounting + gather | fan-out/fan-in with an atomic completion
counter		
I a custom awaiter that resumes a coroutine on a **thread
#* Concurrency exercises ('exercise_conc_*)
Practice for "../concurrency.md* (study plan Week 3, Days 1-4). All need "-pthread":
***bash
g++ -std=c++20 -Wall-wextra -pthread-og exercise_conc_1_queue.cp && ./q
work through them in order - each builds on the previous.
1# I File concept
1 You implement
| New
1c1 I lexercise_conc_1_queue.cpp*
I push / "pop* / close"
shutdown |
I mutex + condition_variable, predicate wait, clean
| c2 | exercise_conc_2_threadpool. cpp* | worker loop, "submit", dtor stop | jthread pool + "packaged_task / future task submission
I c3 | exercise_conc_3_atomics.cpp*
I relaxed "increment", "atomicmax" | relaxed" counters + a compare_exchange retry
loop
I cA I exercise_conc_4_spsc.cpp"
| lock-free SPSC ring via **release/acquire
handshake**
C1 → c2 is a natural progression (the pool reuses the queue pattern). C4 is the capstone: the one lock-free structure simple enough to get right by hand.
### Verifying concurrency work
Concurrency bugs are non-deterministic - run the solutions under sanitizers and stress:
**bash
# data-race detection
B+t -stdac++20 -fsanitizesthread - -pthread -o q exercise_conc_4_spsc. cpp
8& •/q

*   On some hardened kernels Threadsanitizer aborts with "unexpected memory mapping"; if so,
*   "sudo sysctl vm.mmap_rnd_bits=28" or rely on Addresssanitizer
>
*   - the built-in stress checks (the harnesses already push 1M+ items and cross-check
*   sums).
C4 ends with a suggested experiment: rebuild it with every operation set to
"memory_order relaxed and see whether the stress test / TSan catches the race you just introduced. That is the single best way to feel *why* the release/acquire pairing matters.
## Notes & caveats worth understanding
- **Exercise 3** is the payoff for anyone coming from data structures: a recursive tree walk becomes a three-line recursive coroutine with no explicit stack - compare it to a hand-written tree iterator.
*  *Exercise 5**: symmetric transfer (returning a "coroutine_handle" from "await_suspend" prevents stack growth when *resuming* a deep co_await" chain. The simple Task here still destroys its frame chain *recursively* in ~Task, so extremely deep chains can overflow at destruction time - a real limitation that production task types (cppcoro, stdexec) solve with iterative cleanup. The test uses depth 20000, which is safe on a default 8 MB stack.
*  *Exercises 6&g** keep tasks completing *synchronously* so the harness is deterministic, but each has a **stretch goal** (in-file comment) that upgrades it to the genuinely concurrent version using a semaphore / the exercise-thread pool.
## Where to go next
Once these feel comfortable, the natural extensions are:
-   a generator‹T› that supports co yield elements_of(child) in one step (mimic C++23),
-   a concurrent "when_all that resumes its awaiter from the last task to finish on a worker thread,
-   an async I/O awaiter (timer or socket readiness) - the shape real frameworks (asio, libunifex, stdexec) use.

