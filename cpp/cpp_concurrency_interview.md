<!-- docs/cpp/cpp_concurrency_interview.md -->

## C++ concurrency interview questions

Answers: [`cpp_concurrency_interview_answers.md`](cpp_concurrency_interview_answers.md)
(same numbering; `Q42` is at `#q42`, code question 3 at `#c3`.

### Fundamental concepts

1. What is the difference between concurrency and parallelism?

2. What is the difference between a process, a thread, and a task?

3. What are the advantages and disadvantages of using multiple threads?

4. What is a data race?

5. What is the difference between a data race and a race condition?

6. What is undefined behavior in a multithreaded C++ program?

7. What does thread-safe mean?

8. What is the difference between thread safety and reentrancy?

9. What is shared mutable state?

10. Why are global variables particularly risky in concurrent programs?

11. What is thread affinity?

12. What is the difference between CPU-bound and I/O-bound workloads?

13. When can adding more threads reduce performance?

14. What is oversubscription?

15. What is thread starvation?

16. What is livelock?

17. What is deadlock?

18. What is priority inversion?

## C++ threading

19. How do you create a thread using `std::thread`?

20. What happens if a `std::thread` object is destroyed while it is still joinable?

21. What is the difference between `join()` and `detach()`?

22. When is detaching a thread dangerous?

23. How do you determine whether a thread is joinable?

24. What is the purpose of `std::jthread`?

25. How does `std::jthread` differ from `std::thread`?

26. How does cooperative cancellation work with `std::jthread` and `std::stop_token`?

27. What happens if an exception escapes from a thread function?

28. How can exceptions be transferred from a worker thread to the calling thread?

29. What is the lifetime risk when a thread captures a reference to a local variable?

30. What is wrong with this code?

```cpp
void start() {
    int value = 42;

    std::thread worker([&] {
        use(value);
    });

    worker.detach();
}
```

31. How should thread arguments be passed safely?

32. What is the purpose of `std::thread::hardware_concurrency()`)?

33. Why should its return value not always be treated as the exact number of usable CPU
    cores?

## Mutexes and locks

34. What is a mutex?

35. What is the difference between `std::mutex` and `std::recursive_mutex`?

36. Why is recursive locking often a design warning?

37. What is `std::lock_guard`?

38. What is `std::unique_lock`?

39. What is the difference between `std::lock_guard` and `std::unique_lock`?

40. When would you use `std::scoped_lock`?

41. How does `std::scoped_lock` help avoid deadlock when locking multiple mutexes?

42. What does `std::defer_lock` mean?

43. What is the purpose of `std::adopt_lock`?

44. What is a timed mutex?

45. What is the difference between `try_lock()`, `try_lock_for()`, and `try_lock_until()`?

46. How do you protect multiple related variables with a mutex?

47. Why should the critical section usually be as small as practical?

48. Why should potentially blocking operations generally be avoided while holding a mutex?

49. What is lock contention?

50. How can lock contention be reduced?

51. What is lock convoying?

52. What is lock granularity?

53. Compare coarse-grained locking and fine-grained locking.

54. What are the risks of using too many fine-grained locks?

55. How can a mutex be accidentally copied or moved?

56. What is lock ordering, and how does it prevent deadlocks?

57. What is the difference between a deadlock and a livelock?

58. How would you debug a deadlock?

## Condition variables

59. What problem does `std::condition_variable` solve?

60. How does a condition variable work with a mutex?

61. Why must a condition variable always be used with a predicate?

```cpp
condition.wait(lock, [&] {
    return !queue.empty() || stopped;
});
```

62. What is a spurious wakeup?

63. Why must the condition be checked in a loop?

64. What is the difference between `notify_one()` and `notify_all()`?

65. Can a notification be lost?

66. What is the difference between a lost wakeup and a spurious wakeup?

67. What is the purpose of `wait_for()` and `wait_until()`?

68. How would you implement a producer-consumer queue using a condition variable?

69. What happens if the producer adds data before the consumer starts waiting?

70. How should shutdown be implemented for a condition-variable-based worker queue

## Atomics and the C++ memory model

71. What is `std::atomic`?

72. Why is incrementing a normal integer from multiple threads unsafe?

73. Is every operation on an atomic object automatically lock-free?

74. What does `is_lock_free()` indicate?

75. What is the difference between atomicity, visibility, and ordering?

76. What is the C++ memory model?

77. What is a happens-before relationship?

78. What is a synchronizes-with relationship?

79. What is sequential consistency?

80. What are the C++ memory-order options?

 - `memory_order_relaxed`
 - `memory_order_acquire`
 - `memory_order_release`
 - `memory_order_aca_rel`
 - `memory_order_seq_cst`
 - `memory_order_consume`

81. When is `memory_order_relaxed` appropriate?

82. What guarantee does a release operation provide?

83. What guarantee does an acquire operation provide?

84. Why are acquire and release commonly used for producer-consumer communication?

85. What does `memory_order_seq_cst` provide beyond acquire and release?

86. Why is `memory_order_seq_cst` often easier to reason about?

87. What are the performance and correctness risks of using relaxed memory ordering?

88. What is a release sequence?

89. Why does a data race remain undefined behavior even on hardware that appears to
    execute safely?

90. Does `volatile` make operations thread-safe?

91. What is the difference between `volatile` and `std::atomic`?

92. Can an atomic operation prevent all compiler and CPU reordering?

93. What is an atomic flag?

94. When would you use `std::atomic_flag` instead of `std::atomic<bool>`?

95. What is `std::atomic_ref`?

96. What restrictions apply when using `std::atomic_ref`?

97. Can an atomic object be copied?

98. How do you atomically update a compound value?

## Compare-and-exchange

99. What is compare-and-exchange?

100. What is the difference between `compare_exchange_weak()` and
     `compare_exchange_strong()`?

101. Why can `compare_exchange_weak()` fail even when the comparison appears to succeed?

102. Why is the weak form usually used inside a loop?

```cpp
auto expected = old_value;

while (!value.compare_exchange_weak(
    expected,
    new_value,
    std::memory_order_acq_rel,
    std::memory_order_acquire)) {
    // expected is updated after failure
}
```

103. What happens to the expected value after a failed compare-and-exchange?

104. What is the ABA problem?

105. How can tagged pointers or version counters help solve the ABA problem?

106. What is the difference between a fetch-add operation and a compare-and-exchange loop?

107. How would you implement an atomic maximum operation?

## Locks versus atomics

108. When should a mutex be preferred over an atomic?

109. When is an atomic counter appropriate?

110. Why is replacing every mutex with atomics usually a bad idea?

111. Can multiple atomic variables be updated as one transaction?

112. How would you protect an invariant involving multiple variables?

113. Why can an atomic operation still be expensive?

114. What is the difference between lock-based and lock-free algorithms?

115. What do wait-free, lock-free, and obstruction-free mean?

116. Does lock-free mean faster?

117. Does lock-free mean free from starvation?

## False sharing and cache behavior

118. What is false sharing?

119. How can two logically independent variables cause cache-line contention?

120. How can `alignas` help reduce false sharing?

```cpp
struct alignas(64) Counter {
    std::atomic<std::uint64_t> value{0};
};
```

121. Why is the cache-line size platform-dependent?

122. Is aligning a variable to 64 bytes always sufficient?

123. What are the costs of padding objects to separate cache lines?

124. Why can per-thread counters outperform one shared atomic counter?

125. How would you design cache-conscious producer and consumer indexes?

126. What is the difference between sharing data and sharing cache lines?

127. How can object layout affect a multithreaded container?

128. Why might reducing the size of a structure make a multithreaded program slower?

## Futures, promises, and asynchronous tasks

129. What is `std::future`?

130. What is `std::promise`?

131. How do `promise` and `future` communicate a result?

132. How are exceptions transferred through a future?

133. What happens when `future::get()` is called more than once?

134. What is `std::async`?

135. What is the difference between `std::launch::async` and `std::launch::deferred`?

136. Why can relying on the default launch policy be risky?

137. What is `std::packaged_task`?

138. Compare `std::async`, `std::packaged_task`, and a custom thread pool.

139. What is the purpose of `std::shared_future`?

140. What happens if a future referring to an asynchronous task is destroyed?

## Thread pools and task scheduling

141. Why use a thread pool instead of creating a new thread for every task?

142. What components are needed in a basic thread pool?

143. How should a thread pool be shut down?

144. How do worker threads wait for new tasks?

145. What happens to queued tasks during shutdown?

146. Should a thread pool have one queue or one queue per worker?

147. What is work stealing?

148. What are the benefits and costs of work-stealing queues?

149. How can task granularity affect thread-pool performance?

150. What is queue backpressure?

151. How should a bounded task queue behave when it is full?

152. How can priority scheduling introduce starvation?

153. How do you prevent a task from waiting forever?

154. What is nested parallelism, and Why can it exhaust a thread pool?

## Thread-safe containers and queues

155. Are C++ standard containers thread-safe?

156. Can multiple threads read the same container concurrently?

157. Can one thread read a container while another thread modifies it?

158. Can different threads modify different elements of the same container?

159. What additional rules apply to `std::vector<bool>`?

160. How would you make a `std::vector` safe for concurrent access?

161. How would you implement a bounded blocking queue?

162. What is the difference between a blocking queue and a lock-free queue?

163. What is a single-producer/single-consumer queue?

164. How does an SPSC queue differ from an MPSC or MPMC queue?

165. Why is an SPSC queue generally easier to implement?

166. What synchronization is needed for a multi-producer queue?

167. How can a ring buffer avoid dynamic allocation during normal operation?

168. What does the head index represent in a ring buffer?

169. What does the tail index represent?

170. How do you distinguish a full ring buffer from an empty ring buffer?

171. How can wraparound of indexes be handled safely?

172. Why should a ring buffer use unsigned indexes carefully?

173. How can false sharing affect ring-buffer head and tail indexes?

174. What is backpressure in a high-throughput queue?

175. Should a full queue block, drop, overwrite, or reject new data?

176. How would you select a queue policy for a packet-forwarding system?

## Lock-free programming

177. What does lock-free programming mean?

178. What are the main difficulties of writing lock-free code?

179. What is memory reclamation?

180. Why is deleting a node immediately unsafe in a lock-free data strudture?

181. What is hazard-pointer reclamation?

182. What is epoch-based reclamation?

183. What is read-copy-update, or RCU?

184. What is deferred reclamation?

185. How does the ABA problem affect a lock-free stack?

186. How can tagged pointers help with lock-free structures?

187. What is pointer alignment, and Why can it matter for tagged pointers?

188. Why can memory reclamation dominate lock-free data-structure performance?

189. How would you test a lock-free queue?

190. How would you prove that a lock-free algorithm is correct?

191. What is linearizability?

192. What is the linearization point of an operation?

193. What is progress compared with correctness?

194. Why should lock-free code usually be isolated behind a small, well-tested interface?

## C++20 and C++23 synchronization

195. What is `std::counting_semaphore`?

196. What is the difference between a binary semaphore and a mutex?

197. What is `std::binary_semaphore`?

198. When would a semaphore be preferable to a condition variable?

199. What is `std::latch`?

200. How is a latch different from a barrier?

201. What is `std::barrier`?

202. Can a barrier be reused?

203. What is a barrier completion function?

204. What is `std::atomic_wait()`?

205. How does `atomic_wait()` differ from repeatedly polling an atomic?

206. What are `atomic_notify_one()` and `atomic_notify_all()`?

207. When can atomic waiting be more efficient than a condition variable?

208. What are the synchronization risks when mixing atomic waits and ordinary mutexes?

209. How can `std::stop_token` be used to cancel blocking work?

## Coroutines and concurrency

210. Are C++ coroutines automatically concurrent?

211. What is the difference between a coroutine and a thread?

212. How can a coroutine be suspended and resumed?

213. What is an executor or scheduler?

214. How can coroutines be integrated with asynchronous I/O?

215. What lifetime problems can occur when a coroutine captures references?

216. Why does a coroutine frame require careful ownership management?

217. How can cancellation be propagated through an asynchronous coroutine call chain?

218. How do coroutines compare with a thread pool for high-concurrency workloads?

## Code-based questions

### Question 1: data race

What is wrong with this code?

```cpp
int counter = 0;

void worker() {
    ++counter;
}
```

Several threads call `worker()` concurrently. How would you fix it?

### Question 2: missing synchronization

```cpp
bool ready = false;
int value = 0;

void producer() {
    value = 42;
    ready = true;
}

void consumer() {
    while (!ready) {
    }
    use(value);
}
```

Discuss:

- Data races
- Visibility
- Compiler and CPU reordering
- Atomic acquire and release operations
- Condition variables

### Question 3: deadlock

```cpp
std::mutex first;
std::mutex second;

void task_a() {
    std::lock_guard lock1(first);
    std::lock_guard lock2(second);
}
void task_b() {
    std::lock_guard lock1(second);
    std::lock_guard lock2(first);
}
```

How can this deadlock be prevented?

### Question 4: safe locking of multiple mutexes

Rewrite the previous example using:

```cpp
std::scoped_lock lock(first, second);
```

Why is this safer?

### Question 5: condition-variable bug

```cpp
std::mutex mutex;
std::condition_variable cv;
bool ready = false;

void consumer () {
    std::unique_lock lock(mutex);
    cv.wait(lock);

    if (ready) {
        consume();
    }
}
```

What is wrong with this code?

Expected discussion:

- Spurious wakeups
- Missing predicate
- Correct use of `wait(lock, predicate)`

### Question 6: lifetime bug

```cpp
std::thread create_worker() {
    int value = 10;

    return std::thread([&] {
        use(value);
    });
}
```

What happens when the returned thread starts after `value` has gone out of scope?

### Question 7: relaxed atomic

```cpp
std::atomic<int> counter{0};

void worker() {
    counter.fetch_add(1, std:: memory_order_relaxed);
}
```

Is this sufficient for counting? Would it be sufficient for publishing related non-atomic
data?

### Question 8: vector and concurrent access

```cpp
std::vector<int> values;

void producer() {
    values.push_back(1);
}

void consumer() {
    if (!values. empty()) {
        process(values. front());
    }
}
```

What synchronization is required?

### Question 9: condition-variable shutdown

Design a worker loop that exits when:

- the queue is empty and shutdown has been requested;
- otherwise it continues processing queued work.

### Question 10: atomic compare-and-exchange

Implement an atomic increment using a compare-and-exchange loop. Explain:

- The expected value
- The update value
- Weak versus strong compare-exchange
- Failure memory ordering

## Design and discussion questions

219. Design a thread-safe producer-consumer queue.

220. Design an SPSC ring buffer for fixed-size packets.

221. Design an MPMC queue for a worker pool.

222. Design a shutdown mechanism for a service with multiple worker threads.

223. Design a per-thread statistics system that periodically aggregates counters.

224. Design a concurrent cache with bounded memory.

225. How would you prevent a slow consumer from blocking all producers?

226. How would you handle overload in a high-throughput forwarding service?

227. How would you choose between blocking, dropping, batching, and backpressure?

228. How would you avoid sharing mutable state between worker threads?

229. How would you test a concurrent data structure under heavy contention?

230. How would you detect rare timing-dependent bugs?

231. Which tools can help detect data races and deadlocks?

Possible tools include:

- ThreadSanitizer
- AddressSanitizer
- Helgrind
- Static analysis
- Core dumps
- Thread dumps
- Performance counters
- Stress and randomized testing

232. How would you benchmark a concurrent queue fairly?

233. Which metrics would you measure?

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

234. Why can a concurrent benchmark produce misleading results?

235. How would you distinguish lock contention from cache contention?

## Strong final interview questions

236. Explain the difference between a mutex, semaphore, condition variable, latch, and
     barrier.

237. Explain acquire-release synchronization with a producer-consumer example.

238. Explain Why `volatile` is not a replacement for `std::atomic`.

239. Describe a deadlock and at least three ways to prevent it.

240. Compare mutex-based, lock-free, and wait-free designs.

241. Explain false sharing and how it affects atomic counters.

242. Design a cache-conscious multi-producer, multi-consumer queue.

243. Explain how you would safely reclaim memory in a lock-free linked structure.

244. Describe a situation where a lock-free implementation performs worse than a
     mutex-based implementation.

245. Design concurrency for a high-throughput packet-forwarding service, including:

- Worker ownership
- Queue structure
- Backpressure
- Shutdown
- Per-thread statistics
- Memory ordering
- Cache-line layout
- Error handling
- Benchmarking and race detection
