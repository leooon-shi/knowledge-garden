exercise_coro_8_when_all. cpp
// COROUTINE EXERCISE 8 of 8 - when all: await several tasks, resume when all don
//
// Build: g++ -std=ct+20 -Wall -Wextra -pthread -o c8 exercise_coro_8_when_all.c
// Run:
• /c8
1/
// GOAL: implement when_all(...) - take several tasks, run them, and complete
/only after EVERY task has finished, collecting their results into a tuple.
// This is the fan-out/fan-in primitive at the heart of structured concurrency.
//
// The coordination idea (works even if tasks finish on different threads):
// * keep an atomic counter initialised to the number of tasks
// * give each task a completion callback that decrements the counter
* whoever decrements it to zero resumes the awaiting coroutine (or, in this synchronous test harness, simply unblocks when_all)
1/
// To keep the exercise deterministic and testable, tasks here complete
// synchronously when started, and when_all is a plain function that starts all
//
tasks and gathers results. You implement the counting + gathering logic. The
//
atomic counter is still exercised so the pattern transfers directly to the
// multithreaded case (see the stretch note at the bottom).
//
//
#include <atomic>		
#include ‹cassert>		
	#include ‹coroutine›	
#include ‹cstdio>		
	#include ‹exception›	
	#include <iostream>	
#include <tuple〉		
	#include <utility>	
	#include ‹vector>	
YOUR TASKS: implement "all_done
(TODO 2) inside when_all.
accounting (TODO 1) and result gathering
11
I
/ ===================== PROVIDED: a lazy Task<T> == template
‹typename T>
Class Task public:
struct promise_type {
T value();
std:: coroutine_handle‹> continuation();
std:: exception_ptr exception;
Task get_return_object() (
return Task(std:: coroutine_handle<promise_type>: :from_promise(*this)};

std: : suspend_always initial_suspend() noexcept { return 0}; }
struct Final {
bool await_ready) noexcept { return false; }
std:: coroutine_handle‹> await_suspend(std: coroutine_handle<promise_type
auto c = h•promise). continuation; return c? c: std::noop_coroutine();
void awaitresume) noexcept (
Final final_suspend() noexcept { return 0; }
void return_value(Tv) noexcept { value = std: smove(v); }
void unhandled_exception) {exception = std:: current_exception); }
｝；
using handle_type = std: coroutine handlespromise_type>;
Task() = default;
explicit Task(handle_type h) : h_(h) {}
Task( const Task&) = delete;
Task& operator=(const Task&) = delete;
Task (Task&& 0) noexcept : h_(std: :exchange(oh, O)) O
Task& operator=(Task&& o) noexcept {
if (this I= 80) { if (h_) h_•destroy); h_ = std: :exchange(oh_, f}); }
return *this;
}
~Task() {if (h_) h_•destroy(): }
I
// Start the task; it runs to completion synchronously in this design.
void start() { if (h_ && Ih_•done()) h_.resume(): }
bool done() const {return h&& h_.done(); }
T value() const {
if (h_•promise().exception) std: :rethrow_exception(h_•promise().exception);
return h_•promise(). value;
private:
handle_type h_fl:
};
// =======
== YOUR TASK: when_all ==
// Run all tasks and return their results as a tuple, once ALL have completed.
template <typename... Ts>
std:: tuple<Ts...› when_all(Task<Ts>&... tasks) {
// An atomic counter models "how many tasks are still outstanding• In this
// synchronous harness we decrement it as each task finishes; in a real
// async runtime the task's completion callback would do the decrement from
// whatever thread it finished on, and the last one would resume the awaiter.
std: : atomic<int> remaining(sizeof...(Ts)};
// TODO 1: start every task, and after each one finishes, decrement
// remaining. Use a fold expression over the pack.
// Hint: ( (tasks. start), remaining.fetch_sub(1)), ...);
After this line, assert every task is done and remaining==.
(void) remaining;
((void) tasks, ...); // silences unused-parameter warning in the stub; remove
| Il this line once you use tasks in your implementation.
// (For the pattern's sake, verify the accounting worked.)
// assert(remaining.load() == 0);
// TODO 2: gather each task's value) into the result tuple, in order.
// Hint: return std: :tuple<Ts...>{ tasks. value).••}; return std: :tuple<Ts...>;
// placeholder (default-constructed - WRONG)
=== TASKS ======
Task<int> doubler(int x) { co return x
2；
」
Task<int> plus100(1nt x) t co
return x
100；
」
Task<std:: string> label(std: istring s)
{ co return mi" + s + "›"; }
== SELF-CHECK HARNESS =
int main()
｛
// Two int tasks.
I
auto a = doubler(5);
auto b = plus100(7);
auto [ra, rb] = when_alla, b);
assert(ra == 10);
assert(rb == 107);
Mixed types in the result tuple.
	auto a = doubler(50);
	auto b = label("hi");
	auto c = plus100(1);
	auto [ra, rb, rc] = when_all(a, b, c);
	assert(ra == 100);
	assert(rb == "<hi>");

assert(rb = "<hi>");
assert(rc == 101);
}
// Many tasks: results line up with inputs in order.
auto tø = doubler(0);
auto t1 = doubler(1);
auto t2 = doubler(2);
auto t3 = doubler (3);
auto t4 = doubler(4);
auto [ro, r1, r2, r3, r4] = when_all(te, ti, t2, t3, t4);
assert(r0 = 0 8 r1 == 2 8& r2 = 4 8& r3 = 6 88 4 = 8);
std: cout « "All when_all checks passed! \n";
//
// STRETCH GOAL (not tested): make when_all a true awaitable that runs its
I/ tasks on the thread pool from exercise 7 and only resumes the awaiting
// coroutine when the atomic counter hits zero - i.e. the LAST task to
// finish (on whatever worker thread) calls awaiting.resume(). That is the
// real, concurrent when_all.
// return 0;
}