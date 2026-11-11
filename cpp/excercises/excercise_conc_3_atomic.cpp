	#include <atomic>
	#include ‹cassert›
	#include <cstdint›
	#include ‹iostream›
	#include ‹thread>
	#include <vector>
// exercise_conc_3_atomics.cpp
// CONCURRENCY EXERCISE 3 of 4 - Atomics: relaxed counters and CAS loops.
//
// Build: g++ -std=c++20 -Wall -wextra -pthread -093 exercise_conc_3_atomics. cpp
// Run:
93/.
//
/ GOAL: use std:: atomic correctly for lock-free shared state, exercising the two /l core skills from the concurrency doc:
/ a) a RELAXED counter - atomic increments where only the final total
//
matters, not ordering relative to other data;
1/
comprehan R L the general pattern for "atomically
1/
update X based on its current value" (here: an atomic maximum.
//
1/
// WHY
RELAXED IS ENOUGH FOR A COUNTER
//
/l fetch_add is an atomic read-modify-write: no increment is ever lost, even
// under contention. relaxed drops cross-thread ORDERING guarantees, but a pure
// hit-counter doesn't publish other data through the counter, so ordering is
// irrelevant - only the atomicity matters. That makes relaxed both correct and
/ the fastest choice here.
//
// WHY MAX NEEDS A CAS LOOP
//
// There is no atomic "store if greater". You read the current value, compute the
// candidate, and compare_exchange it in; if another thread changed it meanwhile,
// compare_exchange refreshes your local copy and you retry.
//
//
// YOUR TASKS: implement increment (TODO 1) and atomic_max (TODO 2).
// -
// (a) A relaxed hit counter.
struct Counter
std:: atomic<long long> n{0};
void increment) {
// TODO 1: atomically add 1 using memory_order_relaxed.

struct Counter (
void increment) E
TODO 1: atomically add 1 using memory_order relaxed.
n. fetch_add(1, std: memory_order_relaxed);
• incremento
long long get) const {return n.loadstd:: memory_order_relaxed);
｝；
// (b) An atomic maximum via compare_exchange.
_weak.
Atomically set target = ma(target, candidate). Return nothing; the final
value is observable via target. load().
void atomic_max(std: :atomickint›& target, int candidate) {
// TODO 2: CAS retry loop.
int cur = target. load (std: memory_order_relaxed);
/1
/
while (candidate > cur &&
!target. compare_exchange_weak(cur, candidate,
std: memory_order_relaxed)) {
//
// cur was refreshed with the latest value; loop re-checks candidate›cur
(void) target; (void) candidate;
//
int main()
｛
｛
-- SELF-CHECK HARNESS =
// 1. Relaxed counter: N threads each increment K times; total is exact.
const int kthreads = 8;
const int kper = 200000;
Counter c;
std:: vector‹std: ithread ts;
for (int i = 0; i ‹ kThreads; ++i)
ts. emplace_back([&] {
for (int j = 0; j < kPer; ++j) c.increment();
});
for (auto& t : ts) t.join();
const long long expected = (long long)kThreads * kper;
assert (c.get() == expected);
std:: cout « "relaxed counter total = " « c.get()
« " (expected " « expected « ") \n";
// 2. atomic_max: many threads push candidates; the result is the global max.

FH0. -U0uLuuC-Ug
...
std:: atomic<int› best{INT32_MIN};
const int kThreads = 8;
std: vector<std:: thread› ts;
// Thread i pushes values i, i+kThreads, i+2*kThreads, ... up to < 100000.
for (int i = 0; i < kThreads; ++i)
ts. emplace_back([&, i] {
for (int v = i; v ‹ 100000; v += kThreads)
atomic ma(best, v);
});
for (auto& t : ts) t.join(); assert (best.load() == 99999);
std: cout « "atomic_max result = " « best.load() « " (expected 99999) In";
// 3. atomic_max ignores smaller candidates.
{
std:: atomic<int> m{50};
atomic_max (m, 10);
assert (m.load() = 50);
atomic_max (m, 75);
assert (m.load() = 75);
std:: cout « "All atomics checks passed! \n";
return 0;