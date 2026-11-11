exercise_conc_2_threadpool. cpp
// CONCURRENCY EXERCISE 2 of 4 - A thread pool with future-returning submit().
//
// Build: g+
// Run:
-  std=c++20 -Wall -wextra-pthread - -o q2exercise_conc_2_threadpoo1.cpp
*  142
//
1/ GOAL: build a fixed-size thread pool. submit(f, args...) enqueues a task and
// returns a std:: future for its result; worker threads pull tasks from a queue
// guarded by a mutex + condition variable (the pattern from exercise 1). The
// destructor drains/stops cleanly.
//
// This combines the concurrency doc's Day 2 (thread pool) and Day 4 (futures):
// std:: packaged_task turns any callable into something that fulfils a future,
// and the pool just needs to run those tasks on worker threads.
//
1/
DESIGN
//
Tasks are type-erased as std:: function<void()>. submit() wraps the real callable in a std:: packaged_task, grabs its future, pushes a lambda that runs the packaged_task, and returns the future.
*   Workers loop: wait for a task or stop; run the task; repeat.
*   shutdown: set stop, notify_all, workers finish the queue then exit.(std:: jthread auto-joins in its destructor.)
// YOUR TASKS: fill in the worker loop (TODO 1), submit (TODO 2), and the
// stop signal in the destructor (TODO 3).
#include <atomic>
class
ThreadPool 1
#include <cassert>	
	#include <condition_variable
	#include<functional>
#include	‹future>
	#include <iostream>
#include <memory>	
#include <mutex>	
#include < queue>	
#include <string>	
#include <thread>	
	#include ‹ utility>
#include ‹vector›	

std:: vector std: ithread> workers_;
std:: queue std:: function<void() tasks:
std: mutex mi
std:: condition variable cv:
bool stop_ = false;
void workerLoop() {
for(ii) {
std:: function<void()> task;
std: unique_lock 1k(m_);
// TODO 1:
//
wait on cv_ until (stop I|Itasks.empty())
11
if (stop && tasks_. empty()) return; // done
//
move the front task out, pop it cv_•waitIk, [&] {return stop 11 Itasks.empty): F;
if (stop_ && tasks_• empty)) return;
// TODO 1 (cont.): task = std: move(tasks_•front()); tasks_•pop(); return;
// placeholder - replace with the two lines above + fallthrough
task() ;
// run OUTSIDE the lock
public:
explicit ThreadPool (unsigned n) {
for (unsigned i = 0; i ‹ n; +i)
workers_• emplace_back([this] { workerLoop(); });
~ThreadPool() {
// TODO 3:
{ lock; stop = true; }
//
1/
cv_. notify_all();
(jthread destructors then join the workers automatically)
ThreadPool(const ThreadPool&) = delete;
ThreadPoo1& operator=(const ThreadPool&) = delete;
// Enqueue *f(args...); return a future for its result.
template ‹typename F, typename... Args>
auto submit(F&& f, Args&&... args) - std:: future‹std:: invoke_result_t<F, Args...» (
using R
std: : invoke_result_trF, Args... زد

// Bind arguments now; the task becomes a nullary callable returning R.
auto bound = [f = std:: forward<F> (f),
••• a = std::forward<Args>(args)]() mutable -> R {
return f(a...);
j；
// TODO 2:
/
wrap "bound" in a shared _ptr‹std:: packaged_task<()>
//
grab its future BEFORE enqueuing
//
push a std:: function<void()> that invokes the packaged
_tack
//
notify_one; return the future
//
auto task = std: make_shared‹std: :packaged_task<R()>>(std: :move(bound))
//
/
//
std: : future<R› fut = task->get_future();
{std: :lock_guard Ik(m_); tasks_•emplace[task (*task)(); }); }
cv_. notify_one();
return fut;
(void) bound;
return std:: future<R>}:
// placeholder (incorrect)
｝；
//
int main() {
｛
== SELF-CHECK HARNESS =
// 1. A single task returns its value through the future.
ThreadPool pool (4);
auto f = pool. submit([](int x) { return x * x; }, 7);
assert (f.get() == 49);
｝
// Many Many tasks run and every result is correct; total workisparallelised.
｛
ThreadPool pool (8); const int N= 2000;
std:: vector<std: :future<long long>> futs; futs. reserve (N);
for (int i = 0; i < N; ++i)
futs.push_back(pool.submit([](int k) -> long long {
long long s = 0;
for (int j = 0; j <= k; ++j) s += j:
// sum 0..K
return s；

}, i));
long long total = 0;
for (int i = 0; i ‹ N; +i) {
long long expect = (long long)i * (i + 1) / 2;
long long got = futs[i]-get();
assert(got = expect);
total += got;
(void) total;
// 3. "Tasks can carry different return types.
ThreadPool pool(2);
auto fi = pool.submit([] { return 42; });
auto fs = pool.submit([] { return std: :string("hi"); });
assert (fi.get() = 42);
assert (fs-get() = "hi");
// 4. Exceptions thrown in a task surface through future: :get().
ThreadPool pool(2);
auto f = pool. submit([J) -> int { throw std::runtime_error"boom"); H);
bool threw = false;
try {f.get(); } catch (const std: gruntime_error& e) {
threw = (std:: :string(e.what()) == "boom");
assert (threw);
}
std:: cout « "All thread-pool checks passed! \n"; return ø;
