/ coroutine_multicore_transfer.cpp
// Illustrates C++20 coroutines on a multi-core host and cross-thread ownership
// transfer (companion to interview/coroutine_multicore_ownership_transfer.md).
// Demonstrates:
/！
1. A coroutine is control-flow only; a ThreadPool scheduler supplies cores.
2.
co_await resume_on(pool) transfers EXECUTION ownership to another thread
(enqueue = release; dequeue = acquire) -> the coroutine MIGRATES threads.
3.
The single-resumer invariant: each handle is resumed by exactly one worker.
4. Many coroutines run in PARALLEL across workers.
5.
task<T> uses symmetric transfer on final_suspend (same-thread tail resume) and move-only RAII lifetime ownership of the frame.
// Build: g++ -Wall -wextra -pthread coroutine_multicore_transfer.cpp -o cmt
clang++ -std=c++20 -02 -Wall -Wextra -pthread coroutine_multicore_transfer.cpp -0 cmt
#include < atomic>
#include ‹condition_variable›
#include ‹coroutine›
#include ‹cstdio>
#include
‹deque>
#include
<mutex›
#include
(sstream›
#include
‹thread>
#include
‹vector›
/1 A minimal MPMC-queue thread pool. enqueue)/dequeue) are mutex-synchronized, /which provides the release/acquire happens-before edge for the frame hand-off. 1/ Each ready handle is handed to EXACTLY ONE worker (single-resumer invariant).
class ThreadPool (
std::vector<std: sithread workers:
std:: dequestd: coroutine handler» ready: std::mutex m;
std:: condition_variable cv:
bool stop_ - false;
std::atomickint> inflight_(®}; // coroutines not yet finished
public:
explicit ThreadPool (unsigned n) {
for (unsigned i = 0; i ‹ n; +i)
workers_• emplace_back([this, i] worker_1oop(1); 3);
｝
~ThreadPool) {
€ std: :lock_guard 1k(m_); stop - true; } cv_-notify_all();
}
// Hand "who resumes next" to the pool. The push is the RELEASE.
void enqueue(std:: coroutine handles> h) {
{ std::lock_guard 1k(m_); ready_-push_back(h); 7 cv_-notify_one();
｝
void add_inflight()
{inflight_.fetch_add(1, std: :memory_order_relaxed); 3
void
done inflight) inflight_.fetch_sub(1, std::memory_order_relaxed): }
void wait_idle() {
while (inflight_.load(std: :memory_order_acquire) !-0)
std:: this_thread: #sleep_for(std: chrono: milliseconds(1)):
}
private: void
worker_100p(unsigned /*id*/) {
for (33) 1
std: coroutine handles> h;
std;: unique lock 1k(m_); cV_.wait(1k, [8] { return stop-
1l Iceedy_. empty(); 3):
if (stop_
&& ready_
empty)) return;
h - ready_.front();
// dequeue = ACQUIRE
ready_-pop_front();
// handed to exactly ONE worker
h.resume();
1l resume on THIS worker thread
// Helper: short, stable id for the current thread for readable output.
statics:tring tid() {
std:: ostringstream os; os « :get_1d();
std: :string s = os.str();
return s.substr(s.size) > 4? s.size() - 4; 0); |/ last few chars
/1 resume_on{poo1): the ownership-transfer primitive.
/ await_suspend runs on the SOURCE thread, enqueues the handle to the pool, / then returns -> the source thread must NOT touch the handle again.
// A pool worker later resumes it -> the coroutine continues on ANOTHER thread.


struct resume_on {
ThreadPool& pool;
bool await_ready) const noexcept ( return false; }
// always suspend
void
await_suspend(std: coroutine handles› h) const noexcept pool. enqueue(h):
// transfer execution ownershi;
void await_resume() const noexcept 0)
// task<void>: lazy, move-only RAII owner of the frame (lifetime ownership),
1/ with symmetric transfer on final_suspend (same-thread tail resume of the
I/ awaiting coroutine, no stack growth). class task ( public:
struct promise_type {
stdticoroutine handles> continuation(:
task get return_object) (
return task(std: coroutine handle promise type›: from promise(*this)):
}
std:: suspend always initial_suspend() noexcept { return (); ) 1/ lazy struct final awaiter (
bool await_ready) noexcept { return false; }
// Symmetric transfer: tail-resume the awaiter on the SAME thread.
std::coroutine handles› await_suspend(
std:: coroutine_handle<promise_type> h) noexcept {
auto c = h.promise().continuation;
return c?c: std::noop_coroutine():
void await_resume() noexcept (
final_awaiter final_suspend() noexcept {return f)
void return void() (
void unhandled_exception) { std: :terminate(); }
task(task&& o) noexcept : h_(std:: exchange(oh O)) C
task& operator-(task&&) = delete;
task(const task&) - delete;
-task() { if (h) h_.destroy(); }
// RAII lifetime ownership
// Make task awaitable: wire continuation, start it (symmetric transfer in).
auto operator co await && noexcept {
struct awaiter {
std: :coroutine_handle<promise_type>h;
bool await_ready) noexcept { return false; }
std:: coroutine_handles> await_suspend(std: coroutine_handles> c) noexcept {
h-promise)-continuation = c;
return h;
// symmetric transfer into the task
}
void await_resume noexcept t
};
return awaiter(h_};
std::coroutine handlespromise_type› handle() const (return hi)
private:
explicit task(std:: coroutine_handle<promise_type> h): h_(h) ()
std::coroutine_handlespromise_type>h_t:
};
// Demo 1: a single coroutine MIGRATES across threads at
each co_await resume_on.
task migrating(ThreadPool& pool, int id) {
std: printf("
coro %d: start
on thread ..%snid,
tid().c_str()) ;
co_await resume_on[pool);
std: printf("
coro %d: step 2 on thread ...%s\n", 1d, tid().c_str()):
co await resume on pool);
std:: printf("
coro %d: step 3 on thread ...%s\n", 1d, tid() .c_str()) :
// Demo 2: many
coroutines run in PARALLEL across workers. Each bumps a shared
// atomic
(safe) and does a little work; we also track max concurrency observed.
static std: :atomic<int> g_active(®);
static
std:: atomic<int> g_max_active(0);
static
std:: atomic<long> g_sum(®};
task
parallel_work(ThreadPool& pool, int id) {
co await resume_on[pool);
// move onto a pool thread
int
cur
- g_active.fetch_add(1) + 1;
1l enter critical region
int prev
-g_max_active.load();
while (cur>prev && Ig_max_active. compare_exchange_weak(prev, cur)) ()
// simulate work
long local - 0;
for (int i - e; 1 < 200000; ++i) local +- (id * 31 + 1) & 7;

g_sum.fetch_add (local, std: memory_order_relaxed): g_active.fetch_sub(1);
co. return:
// A tiny driver that owns a detached-style task and tracks completion via the pool.
// CAVEAT (demo scaffolding, not production): " spawn" uses an eager "Runner" outer
// coroutine to drive each lazy "task" and decrement the pool's in-flight counter on
// completion. This is scaffolding for lifetime tracking only, NOT a production launch
// API. A real system would use a proper structured-concurrency scope (parent awaits
Il its children so lifetimes nest and ownership is unambiguous rather than detached /l fire-and-forget coroutines. The parts that actually illustrate the write-up's I/ statements are the core mechanics! resume_on" (cross-thread execution-ownership I/ transfer), the pool enqueue/dequeue hand-off (release/acquire + single-resumer),
// symmetric transfer in task, and RAII frame lifetime ownership in ~task().
static void spawn(ThreadPool& pool, task t) {
pool. add_inflight();
// Wrap: when the task completes, decrement inflight. We co_avait it from a
1l small outer coroutine whose continuation is noop, so the task self-drives. struct Runner (
struct promise_type {
ThreadPool* pool;
Runner get_return_object) { return (:)
std::suspend never initial_suspend) noexcept {return (;) std::suspend never final_suspend) noexcept {return 0: ) void return_void() (
｝；
void unhandled_exception) { std: :terminate():}
// Launch via an immediately-started outer coroutine.
[](ThreadPool& p, task inner) -> Runner {
co await std: nove(inner); p.done_inflight():
}(pool, std: :move(t)):
int main() {
const unsigned ncores - std::maxu, std::thread:: hardware_concurrency());
std::printf("main thread ...%s, pool workers = %u\n\n", tid().c_str(), ncores);
ThreadPool pool (ncores);
std::printf("Demo 1 - single coroutines migrating across threads: \n"); for (int i = e; i ‹ 3; ++i) spawn(pool, migrating(pool, i)); pool.wait_idle();
std: printf(" InDemo 2 - many coroutines running in parallel across workers: \n");
constexpr int N = 64;
for (int i = 0; i < N; ++i) spawn(pool, parallel_work(pool, i));
pool.wait_idle;
std::printf(" completed %d coroutines; max concurrent observed = %d; sum = %ld\n*,
N, g_max_active.load(), g_sum.load());
std::printf("\nNote: 'step 2/3' of a coro often print a DIFFERENT thread id than™
* its start, In proving the frame migrated; max-concurrent › 1"
" proves real parallelism. In"):
return e;

