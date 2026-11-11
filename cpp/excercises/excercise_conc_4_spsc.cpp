1/
exercise_conc_4_spsc.cpp
/ CONCURRENCY EXERCISE 4 of 4 - A lock-free SPSCring buffer.
//
// Build: g++ -std=c++20 -Wall -wextra -pthread -0 q4 exercise_conc_4_spsc.cpp
1/ Run:
. 94
//
// GOAL: implement a bounded, LOCK-FREE queue for the single-producer /
// single-consumer case. With exactly one producer and one consumer, you don't
// need a mutex or a CAS loop - just two atomic indices and a correct
// release/acquire handshake. This is the one lock-free structure that is
// genuinely simple, and the capstone of the concurrency doc's Day 3.
// THE HANDSHAKE (this is the whole exercise)
//
//
* The producer owns tail
//
: the consumer owns head. Each only STORES to
its own index and LOADS the other's.
//
//
push(): write the element into buf_[taill, THEN publish it with a RELEASE store to tail. The release ensures the element write is visible before
//
the index advance.
1/
*
pop(): ACQUIRE-load tail_ to check for data; the acquire pairs with the producer's release so the element read sees the written value.
//
*
Capacity is a power of two so & (Cap-1)" replaces "% Cap. One slot is
//
kept empty to distinguish "full" (next==head) from "empty" (head=-tail).
//
// GET THE ORDERING WRONG and you have a race that a stress test / Threadsanitizer
// may catch only sometimes - which is exactly why lock-free code needs both.
//
#include <array>	
#include <atomic>	
	#include <cassert>
	#include <cstddef>
	#include <cstdint>
	#include <iostream>
	#include <optional>
#include <thread>	
// YOUR TASKS: implement push (TODO 1) and pop (TODO 2).
-⋯
template <typename T, std: :size_t Cap> class SpscRing (
static_assert( (Cap & (Cap - 1))
== , "Cap must be a power of two");

class SpsoRing {
std:: array<T, Cap> buf_0; std:: atomic<std: size_t> head_(0}; std:: atomic<std: size_t> tail_(0};
// consumer advances (next slot to read)
// producer advances (next slot to write)
public:
// Producer only. Returns false if the ring is full.
bool push(const T&v) {
// TODO 1:
1/
auto t= tail_.Load(std: memory_order_relaxed); // we own tail
//
auto next = (t + 1) & (Cap - 1);
//
if (next = head_.load(std: memory_order_acquire)) return false; // full
//
buf_[t] = v;
// write element
//
tail_. store(next, std: memory_order_release);
// publish it
return true;
(void)v;
return false;
// placeholder (incorrect)
// Consumer only. Returns nullopt if the ring is empty.
std:: optional<T> pop() {
// TODO 2:
//
auto h= head.Load(std::memory_order_relaxed);
// we own head
1/
1/
if (h= tail.load(std: memory_order_acquire)) return std: nullopt; // empty
TV = buf_[h];
// read element
//
head_.store((h + 1) & (Cap - 1), std:: memory_order _release);
//
return v;
return std::nullopt; // placeholder (incorrect)
=== SELF-CHECK HARNESS =：
int main() {
/ 1. Single-threaded FIFO ordering + full/empty behaviour.
SpscRing<int, 4> r;
// 4 slots -> holds 3 (one kept empty)
assert (r.pop() == std: :nullopt);
// empty
assert (r.push(1)); assert (r.push(2)) ;
assert (r.push(3));
assert (r.push(4) == false); assert (*r. pop() == 1); assert (r.push (4));
// full (only 3 usable)
// FIFO
// room again
assert (*r.pop() == 2);

assert (*r.pop() = 4);
assert(r-pop() = std: :nullopt);
// 2. Concurrent single-producer / single-consumer streaming of 1,000,000
1/
//
items. The consumer's running sum must equal the known total, proving no item was lost, duplicated, or torn.
SpscRing std:: uint64_t, 1024> ring;
const std: :uint64_t N = 1'000'000;
std: jthread producer([8] f
for (std:: uint64_t i = 1; i <= N;) {
if (ring.push(i)) Hi;
// spin if full
// else: buffer full, retry (busy-wait keeps the test simple)
3);
std: suint64_t got = 0, count = 0;
while (count < N) {
if (auto v = ring.pop()) {.got += *v; ++count; }
// else: buffer empty, retry
producer. join();
const std: uint64_t expected = N* (N + 1) / 2;
assert(got = expected);
std: cout « "streamed " « N « " items, sum = " « got « " (expected " «< expected « ")\n";
}
std:: cout « "All SPSC ring-buffer checks passed! \n";
std:: cout « "\nTip: rebuild with -fsanitize=thread and rerun to check the\n"
"release/acquire pairing is race-free. Then try making every \n"
"operation memory_order_relaxed and see what changes. \n";
return ø;
