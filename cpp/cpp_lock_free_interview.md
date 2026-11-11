<!-- docs/cpp/cpp_Lock_free_interview.md -->
It is an important topic in Ct concurrency, but it should be treated separately from ordinary mutex-based synchronization.
## Lock-free concurrency interview questions
Answers: ["cpp_lock_free_interview_answers.md"](cpp_lock free interview answers.md) (same numbering; 042" is at #q42, code question 3 at #c3, final question 2 at #f2 .)
### Fundamental concepts
1. What does **1ock-free**
mean in C++?
1.    Does lock-free mean that the program never uses a mutex?
2.    What is the difference between:
-   obstruction-free;
-   lock-free;
-   wait-free?
1.    Does lock-free guarantee that every thread will eventually complete its operation?
2.    Does lock-free guarantee freedom from starvatich?
3.    Does lock-free always mean faster than a mutex-based implementation?
4.    What is the difference between:
-   atomic;
-   lock-free;
-   wait-free?
8. How can you check whether an atomic type is implemented without locks?
***cpp
std: : atomic<int> value;
if (value.is_lock_free()) {
/ The implementation provides a lock-free atomic representation.

lock_free() is implementation- and type-dependent. An atomic object can be correct
without necessarily being lock-free.
### Atomic operations
1.    What is the purpose of compare_exchange_weak() ?
2.     What is the difference between compare_exchange_weak() and compare_exchange_strong()?
3.     Why is weak compare-exchange normally used inside a loop?
4.     What happens to the expected" argument when compare-exchange fails?
5.     What is the difference between •fetch_add(" and a compare-exchange loop?
6.     How would you atomically update a value based on its current value?
•**cpp
std:: atomic<int> value{0};
void increment() {
int expected = value. load(std: memory_order_relaxed);
do {
int desired = expected + 1;
while (Ivalue. compare_exchange_weak
expected,
expected + 1,
std: memory_order_relaxed, std: memory_order_relaxed));
I
The candidate should understand that 'expected" is updated automatically after a failed comparison. In production code, the desired value should generally be calculated explicitly on every retry.

1.     What is the ABA problem?
2.     How can version counters or tagged pointers help with ABA?
3.     What problems can occur when a pointer is atomically changed but the pointed-to object is deleted?
### Memory ordering
1.     Why is atomicity alone insufficient for a lock-free data structure?
2.     What is the purpose of acquire and release ordering?
3.     When is "memory_order_relaxed" sufficient?
4.     Can relaxed atomics safely publish related non-atomic data?
5.     What is the difference between an atomic operation and a memory barrier?
6.     What is the usual memory-ordering strategy for publishing a newly initialized object?
data = 42;
ready store(true, std: memory_order_release);
The reader can use:
cpp
if (ready.load (std: memory_order_acquire)) {
use(data);
}
The acquire load that observes the release store establishes the required visibility relationship for "data.
Why can using
"memory_order_seq_st" simplify reasoning?

25. What are the performance costs of unnecessarily strong memory ordering?
### Lock-free data structures
1.     How would you implement a lock-free stack?
2.     What is the Treiber stack?
3.     Where is the linearization point in a lock-free stack push or pop?
4.     What is linearizability?
5.     How would you implement a lock-free queue?
6.     What is the difference between an SPSC, MPSC, SPMC, and MPMC queue?
7.     Why is an SPSC ring buffer easier to implement than an MPMC queue?
8.     How do you distinguish an empty ring buffer from a full ring buffer?
9.     What does the producer index represent?
I
1.     What does the consumer index represent?
2.     How do you handle index wraparound safely?
3.     How can a bounded ring buffer handle a full condition?
Possible policies include:
-   block the producer;
-   reject the item;
-   drop the newest item;
-   drop the oldest item;
-   overwrite existing data;
-   apply backpressure.
38. Why should a lock-free queue usually avoid dynamic allocation on its hot path?

1.     Can a ring buffer be lock-free while still having bounded capacity?
2.     What is the difference between a lock-free queue and a wait-free queue?
### Memory reclamation
1.     Why is memory reclamation one of the hardest parts of lock-free programming?
2.     Why is this unsafe?
...
cpp
Node* node = head. load();
if (node I= nullptr) {
head. compare_exchange_strong(node, node-›next);
delete node;
Another thread may still have a reference to node, even after it has been removed from the shared structure. Immediate deletion can result in use-after-free.
1.     What are hazard pointers?
2.     How do hazard pointers protect a node from deletion?
3.     What is epoch-based reclamation?
4.     What is read-copy-update, or RCU?
5.     What is deferred reclamation?
6.     How do hazard pointers compare with epoch-based reclamation?
7.     What happens if a thread pauses indefinitely while holding a hazard pointer or participating in an epoch?
8.     why can memory reclamation consume a significant portion of lock-free queue performance？

### Lock-free versus mutex-based design
1.     When is a mutex-based design preferable to a lock-free design?
2.     What are the advantages of a mutex?
-   Simpler reasoning;
-   easier invariant protection;
-   easier memory reclamation;
-   clearer exception handling;
-   often good performance under low contention.
53. What are the advantages of a lock-free design?
-   One stalled thread does not necessarily block all other threads;
-   potentially better progress under contention;
-   useful for carefully designed queues and counters;
-   can reduce scheduler involvement in some workloads.
54. What are the disadvantages of lock-free programming?
-   Complex correctness proofs;
-   difficult memory reclamation;
-   ABA problems;
-   subtle memory-ordering bugs;
-   possible starvation;
-   high retry and cache-coherence costs;
-   difficult debugging and testing.
55. Why can a lock-free algorithm be slower than a mutex?
Expected points:
-   repeated compare-exchange failures;
-   cache-line bouncing;
-   contention on shared atomics;
-   expensive memory ordering;
-   poor locality;
-   memory reclamation overhead;
-   starvation or unfairness;
-   a well-implemented uncontended mutex may be very cheap.

1.     Does removing a mutex necessarily remove synchronization overhead?
2.     How can lock-free code cause cache-line contention?
3.     What is the relationship between compare-exchange retries and contention?
### Code-based questions
#### Question 1: CAS retry loop
•**cpp
std: atomic<int> counter{0);
void increment) {
int expected = counter. load();
while (Icounter. compare_exchange weak
expected,
expected + 1)) {
Ask the candidate:
-   Is this logically correct?
-   What happens to expected" after failure?
-   What memory ordering is being used?
-   Would fetch_add() be simpler?
-   What happens under heavy contention?
A simpler counter increment is:
counter. fetch_add 1, std: memory_order_relaxed);
I
If the counter is only used for statistics, relaxed ordering mav be sufficient.


#### Question 2: unsafe reclamation
cpp
Node* old_head = head.load (std: memory_order_acquire);
if
Cold head I= nullptr && head. compare_exchange_strong(
old head,
old_head-›next,
std: memory_order_aca_rel)) {
delete old _head;
Ask why another thread might still access "old head, and ask the candidate to propose hazard pointers, epochs, or another reclamation mechanism.
#### Question 3: false sharing
•* cpp
struct Queue {
std: atomic<std: size_t> head(0}; std: atomickstd: size_t tail(0};
Ask:
-   Are "head" and tail accessed by different threads?
-   Could they share a cache line?
-   Would separating them improve performance?
-   What memory cost would padding introduce?
A possible design is:
*** cpp
struct alignas(64) Index (
std:: atomic<std:: size_t> value(®);
｝；
struct Queue

Help
个.
• study [SSH: vm]
_lock_free_interview.md M X
cpp_zero_copy_interview.md M
nowledge-garden> cpp>
cpp_lock_free_interview.md >
## Lock-free concurrency interview questions### Code-based questions##料# Question 3: false sharing
Index head;
Index tail;
Ilvm_component.md M
## Lock-free concurrency interview questions >
The exact cache-line size is platform-dependent, so this should be validated for the target architecture.
#### Question 4: lock-free does not mean wait-free
cpp
while (Istate. compare_exchange_weak(expected, desired)) {
// Retry indefinitely.
Ask whether one thread can theoretically retry forever. If yes, the algorithm may be lock-free but not wait-free.
## Strong final interview questions
1.    Explain why lock-free does not mean wait-free.
2.    Design an SPSC lock-free ring buffer for fixed-size packets.
3.    Explain why an MPM queue requires more coordination than an SPSC queue.
4.    Explain the ABA problem using a lock-free stack.
5.    Explain why removing a node from a lock-free structure is easier than safely deleting it.
6.    Compare hazard pointers, epoch-based reclamation, and RCU.
7.    Explain the role of acquire and release operations in a lock-free queue.
8.    Describe a case where a mutex is better than a lock-free algorithm.
9.    How would you benchmark a lock-free queue fairly?

10. Which metrics would you measure?
-   Throughput;
-   median and tail latency;
-   CAS failure rate;
-   CPU utilization;
-   cache misses;
-   cache-line bouncing;
-   memory-reclamation cost;
-   queue depth;
-   dropped items;
-   starvation or fairness.
The key interview point is:
*   Lock-free programming is primarily a progress guarantee and a memory-reclamation
*   challenge, not simply a technique for making code faster.
