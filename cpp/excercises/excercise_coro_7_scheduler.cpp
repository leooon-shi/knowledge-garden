/ exercise
_coro_7_scheduler.cpp
// COROUTINE EXERCISE 7 of 8 - A custom awaiter: hop onto a thread pool.
1/
// Build: g++ -std=c++20 -Wall -Wextra -pthread -o c7 exercise_coro_7_scheduler.cpp
// Run:
•/c7
// GOAL: write a CUSTOM AWAITER. So far your awaiters have come from Task; now
// you build one from scratch that does something a plain function cannot:
// co_await pool. schedule() SUSPENDS the current coroutine and RESUMES it on a
// worker thread of a thread pool. This is the bridge between coroutines and the
// concurrency primitives from your Week 3 plan.
/
// The insight: an awaiter is any object with await_ready / await_suspend /
// await_resume. In await_suspend you receive the suspended coroutine's handle
1l and may do ANYTHING with it - including handing it to another thread to resume later. That is exactly how coroutine schedulers work.
//A working ThreadPool and a fire-and-forget DetachedTask are provided. You
// implement the ScheduleAwaiter's three methods.
// YOUR TASK: implement ScheduleAwaiter (3 TODOS) .
	
#include ‹cassert>	
	#include < condition_variable>
	#include ‹coroutine›
#include ‹cstdio>	
#include < deque›	
	#include <iostream›
#include <mutex>	
#include ‹thread>	
	#include <unordered_set>
	#include ‹vector>
11
#include <atomic>
ミニニニニニニニニニニニニニニニ=
=== PROVIDED: a thread pool =
class ThreadPool {
std:: vector<std:: thread> workers_;
std: :deque std:: coroutine
handle<» queue;
std:: mutex m;
std:: condition_variable cv:
bool stop_ = false;
void run() {
for (:;) {
std:: coroutine handle‹> h
I

std: unique_lock 1k(m );
cv_waitIk, [this] {return stop_ I queue. empty); 3);
if (stop_ &d queue • empty()) return; h= queue•front ();
queue_•pop_front();
h. resume();
public:
explicit ThreadPool(int n) {
for (int i = 0; i ‹ n; ++i) workers_emplace_back([this] € run(); );
}
~ThreadPool() {
{ std: :lock_guard 1k(m_); stop = true; }
cv_•notify_all();
for (auto& t : workers_) t.join();
// Enqueue a coroutine handle to be resumed by some worker thread.
void enqueue std:: coroutine handle‹> h) {
{std: :lock_guard Ik(m_); queue •push_back(h); } cv_notify_one() ;
// Return this thread's id if it is one of the pool's workers (for the test).
std:: vector<std: thread: id› worker_ids) const (
std:: vector<std: :thread:: id› ids;
for (const auto& t : workers_) ids.push_back(t.get_id()) ; return ids;
//---- the awaiter you will implement ----
struct ScheduleAwaiter {
ThreadPool* pool;
// TODO 1: an unscheduled coroutine is never "already done" - we always
//
want to suspend so we can move to a worker thread.
bool await_ready (), noexcept {
return true;
// placeholder (WRONG: this skips scheduling entirely)


/ TODO 2: hand the suspended coroutine to the pool so a worker resumes it.
void await_suspend std::coroutine handle>h) noexcept {
(void)h;
// pool-›enqueue(h);
｝；
// TODO 3: nothing to produce on resumption.
void await_resume() noexcept (
I/ co_await pool. schedule() moves execution onto a worker thread.
ScheduleAwaiter schedule() { return ScheduleAwaiter(this}; }
};
//
== PROVIDED: fire-and-forget task =
struct DetachedTask {
struct promise
_type {
DetachedTask get_return_object) { return C}
std: :suspend never initial_suspend() noexcept {return (:) std:: suspend never tinal suspend noexcept i return tri void return_void()
noexcept {)
void unhandled_exception) { std: terminate(); }
// start eagerly
1/
self-destroy at end
｝
}
TEST WORKLOAD =
static std:: atomickint> g_done{0}; static std: mutex g_setMutex;
static std: unordered_set<std: thread: sid> gthreadsseen;
// This coroutine starts on the calling thread, then hops onto the pool.
DetachedTask job(ThreadPool& pool) {
co await pool schedule();
‹-- suspend here, resume on a worker
// Everything below runs on a pool worker thread.
std:: lock_guard Ik(g_setMutex);
_threadsSeen. insert(std:: this_thread: get_id());
_done. fetch_add(1, std: memory_order_relaxed);
== SELF-CHECK HARNESS ==
int main() {

const int kJobs = 500;
std:: vector<std: :thread: :id› workerIds;
｛
ThreadPool pool(4);
workerIds = pool. worker_ids);
for (int i = 0; i < kJobs; ++i) job(pool);
｝
// wait for all jobs to finish (they run asynchronously on the pool).
while (g_done.load(std:: memory_order_relaxed) < kJobs)
std: this_thread: yield();
// pool destructor joins all workers
// 1. Every job completed exactly once.
assert(g_done. load() == kJobs);
I/ The work ran on POOL WORKER threads, not the main thread.
assert(g_threadsseen. count (std: this_thread: get_id()) = 08&
"work ran on the main thread - did await_ready return true?");
// 3. Every thread that ran work was a pool worker.
std: unordered_set<std: thread: : id› workersetworkerIds.begin(), workerIds.end()):
for (auto id: g_threadsseen)
assert (workerset. count (id) = 1 && "work ran on a non-worker thread");
std:: cout « "work ran on " « g_threadsSeen.size() « " distinct worker threads) \n"; std:: cout « "All scheduler checks passed! \n";
return ø;
