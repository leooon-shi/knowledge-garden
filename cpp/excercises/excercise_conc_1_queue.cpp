// exercise_conc_1_queue. cpp
// CONCURRENCY EXERCISE 1 of 4 - A bounded blocking queue.
//
// Build: g++-std=c++20 -Wall -Wextra-pthread - 91 exercise_conc_1_queue. cpp
// Run:
•191
1/
// GOAL: implement a thread-safe bounded queue using std:: mutex +
// std:: condition variable. Producers block when the queue is full; consumers
// block when it is empty. A close() call wakes everyone and lets consumers
// drain the remainder, so worker threads can shut down cleanly.
//
// This is the "producer/consumer" foundation from the concurrency doc, and the
// building block for the thread pool in exercise 2.
//
//
//
1/
//
KEY POINTS (from concurrency.md)
*   Guard ALL shared state (the deque, the closed flag) with the mutex.
*   Use the PREDICATE overload of cv. wait - it re-checks the condition and is correct in the face of spurious wakeups.
*
Two condition variables: one for "not full" (producers wait), one for
"not empty" (consumers wait). Notify the right one after each operation.
* On close(): set the flag, then notify_all on BOTH cvs so blocked threads wake and observe the shutdown.
#include <atomic>	
#include<cassert>	
	#include < condition variable›
	#include ‹cstdint>
#include <deque›	
	#include <iostream›
#include <mutex>	
	#include <optional>
#include ‹thread>	
#include <vector>	
// YOUR TASKS: fill in the 3 TODOS (push, pop, close).
//
template ‹typename
• T>
class BoundedQueue {
std:: dequesT> q_;
const std:: size_t cap;
std:: mutex m;

exercise_conc_1_queue.cpp ..
class BoundedQueue {
std:: condition_variable notFull_; std:: condition_variable notEmpty:
bool closed = false;
public:
explicit BoundedQueuestd: size_t capacity) : cap(capacity) (
// Block until there is room, then enqueue v. Returns false if the queue
// was closed (rejecting the item).
bool push (Tv) {
std: unique_lock 1k(m_):
// TODO 1:
//
wait on notfull_until (size() < cap I closed_)
I/ if (closed_) return false;
//
push v into q, unlock, then notify one waiter on notEmpty_
//
return true;
(void)v;
return false; // placeholder (incorrect)
}
// Block until an item is available or the queue is closed & drained.
// Returns std::nullopt only when closed AND empty (the shutdown signal).
std:: optional<T> pop() {
std: unique_lock 1k(m_);
// TODO 2:
//
wait on notEmpty_until (!q•empty() || closed_)
//
if (q. empty()) return std::nullopt;
// closed and drained
1/
pop the front value, unlock, notify one waiter on notFull_
// return the value
return std:: nullopt;
// placeholder (incorrect)
}
// Signal shutdown: no more pushes accepted; wake all blocked threads.
void close() {
// TODO 3:
//
lock, set closed = true, unlock
notify_all) on BOTH condition variables
std:: size_t size() {
std: lock_guard 1k (m_);
return q•size();

== SELF-CHECK HARNESS ==
int
main() {
{
// 1. Single-threaded basics.	
BoundedQueue<int> q(2);	
assert(q•push(1));	
assert(q• push(2));	
assert (*q•pop() = 1);	
assert (*q-pop() = 2);	
q. close() ;	
assert(q-pop) = std::nullopt);	
assert (q-push(3) == false);	
// closed + empty
// closed rejects
// 2. Many producers + many consumers: every produced value is consumed
/
exactly once (checked by summing) .
const int kProducers = 4, kconsumers = 4;
const int kperProducer = 50000;
const long long expected =
(long long)kProducers * kPerproducer * (kPerProducer + 1) / 2;
// Each producer pushes 1. kPerProducer; total sum is known.
BoundedQueue<int> q(128);
std:: atomicklong long> consumedSum{0};
std:: atomic<int> consumedcount{0}s
std: vector‹std:: jthread› producers, consumers;
for (int p = 0; p < kProducers; ++p)
producers. emplace_back([&] {
for (int i = 1; i <= kPerProducer; ++i) q•push(i);
}) ;
for
(int c= 0; c< kConsumers; ++c) consumers. emplace_back ([&] (
while (auto v = q. pop()) {
consumedsum. fetch_add(*v, std:: memory_order_relaxed); consumedcount. fetch_add (1, std: memory_order_relaxed);
}) ;
for (auto& t : producers) t.join();
9. cLose () ;
for (auto& t : consumers) t.join();
// all items enqueued
/l tell consumers to finish draining

assert(consumedCount. load() = kProducers * kPerproducer);
assert (consumedSum. load() = expected);
std:: cout «< "concurrent sum matched (" « consumedsum. load() « ") \n";
std:: cout « "All bounded-queue checks passed! \n";
return
0;
}