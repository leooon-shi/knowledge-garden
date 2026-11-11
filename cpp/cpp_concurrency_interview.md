K!-- docs/cpp/cpp_concurrency_interview.md -->
## C++ concurrency interview questions
### Fundamental concepts
1.    What is the difference between concurrency and parallelism?
2.    What is the difference between a process, a thread, and a task?
3.    What are the advantages and disadvantages of using multiple threads?
4.    What is a data race?
5.    What is the difference between a data race and a race condition?
6.    What is undefined behavior in a multithreaded C++ program?
7.    What does thread-safe mean?
8.    What is the difference between thread safety and reentrancy?
9.    What is shared mutable state?
10.     Why are global variables particularly risky in concurrent programs?
11.     What is thread affinity?
12.     What is the difference between CPU-bound and I/O-bound workloads?
13.     When can adding more threads reduce performance?
14.     What is oversubscription?
15.     What is thread starvation?
16.     What is livelock?
17.     What is deadlock?
18.     What is priority inversion?

## C++ threading
1.     How do you create a thread using std: thread?
2.     What happens if a std:: thread" object is destroyed while it is still joinable?
3.     What is the difference between join() and detach() ?
4.     When is detaching a thread dangerous?
5.     How do you determine whether a thread is joinable?
6.     What is the purpose of std:: jthread?
7.     How does std::jthread" differ from std: thread?
8.     How does cooperative cancellation work with "std: jthread" and std::stop_token ?
9.     What happens if an exception escapes from a thread function?
10.     How can exceptions be transferred from a worker thread to the calling thread?
11.     What is the lifetime risk when a thread captures a reference to a local variable?
12.     What is wrong with this code?
•**cpp
void start() {
int value = 42;
std:: thread worker [&] {
use(value);
｝）；
worker detach();
### Fundam
I
1.     How should thread arguments be passed safely?
2.     What is the purpose of std:: thread: hardware_concurrency) ?

33. Why should its return value not always be treated as the exact number of usable CPU cores?
## Mutexes and locks
1.     What is a mutex?
2.     What is the difference between std: mutex" and std: :recursive_mutex?
3.     Why is recursive locking often a design warning?
4.     What is " std:: lock_guard"?
5.     What is std; unique_lock?
6.     What is the difference between std:: lock_guard" and std: unique_lock?
7.     When would you use std: :scoped_lock'?
8.     How does " std: :scoped_lock" help avoid deadlock when locking multiple mutexes?
9.     What does " std: :defer_lock' mean?
10.     What is the purpose of std: adopt_lock ?
11.     What is a timed mutex?
12.     What is the difference between try_lock(), try_lock_for(), and try_lock until) ?
13.     How do you protect multiple related variables with a mutex?
14.     Why should the critical section usually be as small as practical?
15.     Why should potentially blocking operations generally be avoided while holding a mutex?
16.     What is lock contention?
17.     How can lock contention be reduced?
18. 
19. minal Help
20. • study [SSH: vm]
21. _concurrency_interview.mdMX
22. nowledge-garden > cpp ## Mutexes and locks
23. 51. What is lock convoying?
24. cpp_concurrency_interview.md
25. ## Mutexes and locks
26.     What is lock granularity?
27.     Compare coarse-grained locking and fine-grained locking.
28.     What are the risks of using too many fine-grained locks?
29.     How can a mutex be accidentally copied or moved?
30.     What is lock ordering, and how does it prevent deadlocks?
31.     What is the difference between a deadlock and a livelock?
32.     How would you debug a deadlock?
## Condition variables
1.     What problem does " std:: condition_variable" solve?
2.     How does a condition variable work with a mutex?
3.     Why must a condition variable always be used with a predicate?
•cpp
condition wait(lock, [&] {
return queue. empty
｝）；
••、
stopped;
1.     What is a spurious wakeup?
2.     Why must the condition be checked in a loop?
3.     What is the difference between notify_one() and notify_all)?
4.     Can a notification be lost?
5.     What is the difference between a lost wakeup and a spurious wakeup?
6. 
7.     What is the purpose of wait for) and wait until()"?
8.     How would you implement a producer-consumer queue using a condition variable?
9.     What happens if the producer adds data before the consumer starts waiting?
10.     How should shutdown be implemented for a condition-variable-based worker queue
## Atomics and the C++ memory model
1.     What is std:: atomic ?
2.     Why is incrementing a normal integer from multiple threads unsafe?
3.     Is every operation on an atomic object automatically lock-free?
4.     What does "is_lock_free() indicate?
5.     What is the difference between atomicity, visibility, and ordering?
6.     What is the C++ memory model?
7.     What is a happens-before relationship?
8.     What is a synchronizes-with relationship?
9.     What is sequential consistency?
10.     What are the C++ memory-order options?
-   "memory_order_relaxed"
-   "memory_order_acquire
-   "memory_order_release
-   "memory_order_aca_rel
-   "memory_order_seq_cst
-   "memory_order_consume
1. 81. When is "memory_order_relaxed appropriate?
2. 
3.     What guarantee does a release operation provide?
4.     What guarantee does an acquire operation provide?
5.     Why are acquire and release commonly used for producer-consumer communication?
6.     What does memory_order_seq_cst provide beyond acquire and release?
7.     Why is memory_order_seq_st" often easier to reason about?
8.     What are the performance and correctness risks of using relaxed memory ordering?
9.     What is a release sequence?
10.     Why does a data race remain undefined behavior even on hardware that appears to execute safely?
11.     Does "volatile make operations thread-safe?
12.     What is the difference between "volatile" and "std::atomic?
13.     Can an atomic operation prevent all compiler and CPU reordering?
14.     What is an atomic flag?
15.     When would you use std: atomic_flag instead of std: atomic<bool>?
16.     What is std:: atomic_ref?
17.     What restrictions apply when using std:: atomic_ref?
18.     Can an atomic object be copied?
19.     How do you atomically update a compound value?
** Compare-and-exchange
99. What is compare-and-exchange?
I

100. What is the difference between compare_exchange_weak() and
- compare_exchangestrong()?
1.      Why can "compare_exchange weak() fail even when the comparison appears to succeed?
2.      Why is the weak form usually used inside a loop?cpp
auto expected = old_value;
while (Ivalue. compare_exchange_weak
expected, new_value, std:: memory_order_aca_rel,
std: memory_order_acquire)) {
I/ expected is updated after failure
1.      What happens to the expected value after a failed compare-and-exchange?
2.      What is the ABA problem?
3.      How can tagged pointers or version counters help solve the ABA problem?
4.      What is the difference between a fetch-add operation and a compare-and-exchange loop?
5.      How would you implement an atomic maximum operation?
Locks versus atomics
1.      When should a mutex be preferred over an atomic?
2.      When is an atomic counter appropriate?
3.      Why is replacing every mutex with atomics usually a bad idea?
4.      Can multiple atomic variables be updated as one transaction?
I

1.      How would you protect an invariant involving multiple variables?
2.      Why can an atomic operation still be expensive?
3.      What is the difference between lock-based and lock-free algorithms?
4.      What do wait-free, lock-free, and obstruction-free mean?
5.      Does lock-free mean faster?
6.      Does lock-free mean free from starvation?
## False sharing and cache behavior
1.      What is false sharing?
2.      How can two logically independent variables cause cache-line contention?
3.      How can "alignas" help reduce false sharing?
•** cpp
struct alignas(64) Counter {
std: atomic<std: uint64_t> valuef®};
};
1.      Why is the cache-line size platform-dependent?
2.      Is aligning a variable to 64 bytes always sufficient?
3.      What are the costs of padding objects to separate cache lines?
4.      Why can per-thread counters outperform one shared atomic counter?
5.      How would you design cache-conscious producer and consumer indexes?
6.      What is the difference between sharing data and sharing cache lines?

1.      How can object layout affect a multithreaded container?
2.      Why might reducing the size of a structure make a multithreaded program slower
*# Futures, promises, and asynchronous tasks
1.      What is std:: future?
2.      What is "std: :promise?
3.      How do promise and "future" communicate a result?
4.      How are exceptions transferred through a future?
5.      What happens when future::get) is called more than once?
6.      What is std: :async?
7.      What is the difference between std: launch: async and "std::launch:: deferred?
8.      why can relying on the default launch policy be risky?
9.      What is "std: :packaged_task?
10.      Compare std: :async, std:: packaged_task, and a custom thread pool.
11.      What is the purpose of std:: shared future?
12.      What happens if a future referring to an asynchronous task is destroyed?
## Thread pools and task scheduling
1.      Why use a thread pool instead of creating a new thread for every task?
2.      What components are needed in a basic thread pool?
I
1. 
2.      How should a thread pool be shut down?
3.      How do worker threads wait for new tasks?
4.      What happens to queued tasks during shutdown?
5.      Should a thread pool have one queue or one queue per worker?
6.      What is work stealing?
7.      What are the benefits and costs of work-stealing queues?
8.      How can task granularity affect thread-pool performance?
9.      What is queue backpressure?
10.      How should a bounded task queue behave when it is full?
11.      How can priority scheduling introduce starvation?
12.      How do you prevent a task from waiting forever?
13.      What is nested parallelism, and why can it exhaust a thread pool?
## Thread-safe containers and queues
1.      Are C++ standard containers thread-safe?
2.      Can multiple threads read the same container concurrently?
3.      Can one thread read a container while another thread modifies it?
4.      Can different threads modify different elements of the same container?
5.      What additional rules apply to std: vector<bool›?
6.      How would you make a std::vector safe for concurrent access?
7.      How would vou implement a bounded blocking queue?
I

1.      What is the ditterence between a blocking queue and a lock-tree queue?
2.      What is a single-producer/single-consumer queue?
3.      How does an SPSC queue differ from an MPSC or MPMC queue?
4.      Why is an SPSC queue generally easier to implement?
5.      What synchronization is needed for a multi-producer queue?
6.      How can a ring buffer avoid dynamic allocation during normal operation?
7.      What does the head index represent in a ring buffer?
8.      What does the tail index represent?
9.      How do you distinguish a full ring buffer from an empty ring buffer?
10.      How can wraparound of indexes be handled safely?
11.      Why should a ring buffer use unsigned indexes carefully?
12.      How can false sharing affect ring-buffer head and tail indexes?
13.      What is backpressure in a high-throughput queue?
14.      Should a full queue block, drop, overwrite, or reject new data?
15.      How would you select a queue policy for a packet-forwarding system?
## Lock-free programming
1.      What does Lock-free programming mean?
2.      What are the main difficulties of writing lock-free code?
3.      What is memory reclamation?
4.      Why is deleting a node immediately unsafe in a lock-free data strudture?


1.      What is hazard-pointer reclamation?
2.      What is epoch-based reclamation?
3.      What is read-copy-update, or RCU?
4.      What is deferred reclamation?
5.      How does the ABA problem affect a lock-free stack?
6.      How can tagged pointers help with lock-free structures?
7.      What is pointer alignment, and why can it matter for tagged pointers?
8.      Why can memory reclamation dominate lock-free data-structure performance?
9.      How would you test a lock-free queue?
10.      How would you prove that a lock-free algorithm is correct?
11.      What is linearizability?
12.      What is the linearization point of an operation?
13.      What is progress compared with correctness?
14.      Why should lock-free code usually be isolated behind a small, well-tested interface?
##C+-+20 and C++23 synchronization
1.      What is std: :counting_semaphore?
2.      What is the difference between a binary semaphore and a mutex?
3.      What is "std: binary_semaphore?
4.      When would a semaphore be preferable to a condition variable?
5.      What is " std: :latch'?
I


1.      How is a latch different from a barrier?
2.      What is "std::barrier"?
3.      Can a barrier be reused?
4.      What is a barrier completion function?
5.      What is std: atomic_wait() ?
6.      How does atomic wait() differ from repeatedly polling an atomic?
7.      What are atomic_notify_one) and atomic_notify_all) ?
8.      When can atomic waiting be more efficient than a condition variable?
9.      What are the synchronization risks when mixing atomic waits and ordinary mutexes?
10.      How can " std: :stop_token" be used to cancel blocking work?
## Coroutines and concurrency
1.      Are C+ coroutines automatically concurrent?
2.      What is the difference between a coroutine and a thread?
3.      How can a coroutine be suspended and resumed?
4.      What is an executor or scheduler?
5.      How can coroutines be integrated with asynchronous I/O?
6.      What lifetime problems can occur when a coroutine captures references?
216. Why does a coroutine frame require careful ownership management?
1.      How can cancellation be propagated through an asynchronous coroutine call chain?
2.      How do coroutines compare with a thread pool for hieh-concurrenc workloads?

## Code-based questions
### Question 1: data race
What is wrong with this code?
cpp
int counter = 0;
void worker() {
++counter;
Several threads call worker) concurrently. How would you fix it?
### Question 2: missing synchronization
•**cpp
bool ready = false;
int value = 0;
void producer() {
value = 42;
ready = true;
void consumer() {
while (!ready) {
}
use (value);
Discuss:
-   Data races
-   Visibility

### Question 2: missing synchronization
-   Compiler and CPU reordering
-   Atomic acquire and release operations
-   Condition variables
### Question 3: deadlock
std: mutex first; std:: mutex. second;
void task_a() {
std: :lock_guard lock1(first); std: :lock_guard lock2( second);
}
void task_b() {
std: :lock_guard lock1(second); std: :lock_guard lock2(first);
How can this deadlock be prevented?
### Question 4: safe locking of multiple mutexes
Rewrite the previous example using:
std:: scoped_lock lock(first, second);
Why is this safer?
I

### Question 5: condition-variable bug
•cpp
std: :mutex mutex;
std:: condition_variable cv;
bool ready = false;
void consumer () {
std: :unique_lock lock(mutex);
cv wait(lock);
if (ready) {
consume();
What is wrong with this code?
Expected discussion:
-   Spurious wakeups
-   Missing predicate
-   Correct use of wait(lock, predicate)
### Question 6: lifetime bug
std:: thread create_worker() (
int value = 10;
return std:: thread ([&] {
use(value);
}) ;
I
What happens when the returned thread starts after value has gone out of scope?

### Question 7: relaxed atomic
cpp
std: atomickint> counter{®);
void worker) {
counter. fetch_add(1, std:: memory_order_relaxed);
Is this sufficient for counting? Would it be sufficient for publishing related non-atomic data?
### Question 8: vector and concurrent access
•*cpp
std:: vector<int› values;
void producer() {
values.push_back(1);
void consumer() {
if (Ivalues. empty()) {
process (values. front()) :
What synchronization is required?
### Question 9: condition-variable shutdown
Design a worker loop that exits when:
- the queue is empty and shutdown has been requested;


- otherwise it continues processing queued work.
### Question 10: atomic compare-and-exchange
Implement an atomic increment using a compare-and-exchange loop. Explain:
-   The expected value
-   The update value
-   Weak versus strong compare-exchange
-   Failure memory ordering
## Design and discussion questions
1.      Design a thread-safe producer-consumer queue.
2.      Design an SPSC ring buffer for fixed-size packets.
I
1.      Design an MPM queue for a worker pool.
2.      Design a shutdown mechanism for a service with multiple worker threads.
3.      Design a per-thread statistics system that periodically aggregates counters.
4.      Design a concurrent cache with bounded memory.
5.      How would you prevent a slow consumer from blocking all producers?
6.      How would you handle overload in a high-throughput forwarding service?
7.      How would you choose between blocking, dropping, batching, and backpressure?
8.      How would you avoid sharing mutable state between worker threads?
9.      How would you test a concurrent data structure under heavy contention?
10.      How would you detect rare timing-dependent bugs?


	- Throughput
	- Latency
	- Tail latency
	- CPU utilization
	- Context switches
	- Lock contention
	- Cache misses
	- Memory allocation
	- Queue depth
	- Dropped or rejected work
231. Which tools can help detect data races and deadlocks?
Possible tools include:
-   ThreadSanitizer
-   AddressSanitizer
-   Helgrind
-   Static analysis
-   Core dumps
-  Thread dumps
-   Performance counters
-   stress and randomized testing
1.      How would you benchmark a concurrent queue fairly?
2.      Which metrics would you measure?
I
1.      Why can a concurrent benchmark produce misleading results?
2.      How would you distinguish lock contention from cache contention?
## Strong final interview questions
1.      Explain the difference between a mutex, semaphore, condition variable, latch, and barrier.
2.      Explain acquire-release synchronization with a producer-consumer example.
3.      Explain why "volatile is not a replacement for std: atomic.

1.      Describe a deadlock and at least three ways to prevent it.
2.      Compare mutex-based, lock-free, and wait-free designs.
3.      Explain false sharing and how it affects atomic counters.
4.      Design a cache-conscious multi-producer, multi-consumer queue.
5.      Explain how you would safely reclaim memory in a lock-free linked structure.
6.      Describe a situation where a lock-free implementation perfors worse than a mutex-based implementation.
7.      Design concurrency for a high-throughput packet-forwarding service, including:
-   Worker ownership
-   Queue structure
-   Backpressure
-   Shutdown
-  Per-thread statistics
-   Memory ordering
-   Cache-line layout
-   Error handling
-   Benchmarking and race detection
- 
