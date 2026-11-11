// exercise_coro_6_sync_wait. cpp
I/ COROUTINE EXERCISE 6 of ? - sync_wait: bridge async coroutines to sync code.
1/
// Run:
// Build: g++ -std=c++20 -Wall -wextra -o c6 exercise_coro_6_sync_wait.cpp
./сб
//
// GOAL: implement sync_wait(task) - a blocking call that drives a Task<T> to
// completion and returns its value. This is how you call async coroutine code
// from a synchronous context like main(): something has to sit at the top and
// pump the coroutine until it finishes.
//
// A fully-working, chaining Task<T> is provided (the solution to exercise 5).
// You implement ONLY sync wait.
//
//
==・
/ HOW sync wait WORKS
1 --
// The simplest correct sync wait for THIS single-threaded Task (no real async
// I/o, no other threads) just needs to:
//
/1
1. start the task by resuming its handle,
1/
2. because our tasks complete synchronously once started (each co_await
//
transfers straight into the callee and back), after the initial resume
1/
the whole chain has run and the top task is done,
3. return the task's result (rethrowing any captured exception).
11
// To make sync_wait work for ANY task without reaching into Task's private
// handle, Task exposes a minimal driver interface: start), is_done), take() . //. Use those.
1/
// (In a multithreaded runtime, syncwait would instead block on a
// std::binary_semaphore that the task's final_suspend releases. That's the
/ stretch goal at the bottom.)
11
//
I
YOUR TASK: implement sync_wait (1 TODO).
#include <cassert> #include <coroutine>
#include <exception>
#include <iostream>
#include <utility>
====== PROVIDED: chaining Task<T> ==
template
くtypename

leskWask ca
ass Task {
blic:
struct promise_type {
T valued};
std::coroutine_handle<> continuation};
std::exception_ptr exception;
Task get_return_object) {
return Task(std::coroutine_handle<promise_type>::from_promise(*this)};
std:: suspend_always initial_suspend) noexcept {return ( struct FinalAwaiter (
bool await_ready) nexcept {return false; } std:: coroutine_handle<> await_suspend(std:: coroutine_handle<promise_type> h) noexcept {
auto c = h.promise). continuation;
return c?c: std::noop_coroutine):
void await_resume noexcept ()
};
FinalAwaiter final_suspend() noexcept {return (:)
void return_value(Tv) noexcept {value = std:::move(v); }
void unhandled_exception() { exception = std::current_exception(): }
using handle_type = std:: coroutine handlespromise_type>;
Task() = default;
explicit Task(handle_type h): h_(h) {}
Task( const Task&) = delete;
Task& operator=(const Task&) = delete;
Task(Task&& o) noexcept : h_(std:::exchange(oh_, (7))
Task& operator=(Task&& o) noexcept {
if (this I= 80) ( if (h_) h_.destroy(); h = std::exchange(oh (3); )
return *this;
struct Awaiter {
handle_type callee;
bool awaitready) noexcept (return false;}
std:: coroutine_handle<> await_suspend(std:: coroutine_handles> caller) noexcept i
callee.promise().continuation = caller;
return callee;
T
await _resume () {
if (callee.promise).exception) std:: rethrow_exception(callee.promise().exception);
return std:: move(callee.promise). value);

};
Awaiter operator co await() {return Awaiter(h): }
11 ---- minimal driver interface for sync_wait ----void start() { if (h&& Ih_.done()) h_.resume(): }
bool is_done() const { return h& h_.done(); }
T take() {
if (h_.promise).exception) std:: rethrow_exception(h_-promise().exception);
return std::move(h_.promise().value);
private:
handle_type h = 03
	
	template <typename T>
	T sync_wait(Task<T> task) {
	(void) task;
// TODO:	
//	task. start():
//	
	return task. take();
	
1/.==
= YOUR TASK =:
// Drive "task to completion and return its value (rethrowing on error).
assert(task.is_done()); // our tasks complete synchronously once started
return TO; // placeholder (incorrect)
}
ニニニ
TASKS ===
Task<int> leaf (int x)
Task<int> compute() {
{ co return x
awa11
leaf (4);
ww
int b = co_await
leaf (10);
co return at b;
Task<int> nested (int depth) {
† depth == o co return
nt r = co await nested(depth - 1)
co return ri
Task<int> failing() {
throw std:: runtime
_error ("nope") ;
3; 1
// 12
//
30
//
42

co_return
//
= SELF-CHECK HARNESS =
int main() {
// sync wait returns the computed value.
assert (sync_wait(compute()) == 42);
// Works through a nested chain.
assert (sync_wait(nested(50)) = 7);
// Simple leaf.
assert (sync_wait(leaf(14)) = 42);
1/
{
Exceptions surface out of sync_wait.
bool threw = false;
try {
sync wait(failing());
} catch (const std::runtime_error& e) {
threw = (std:::string(e.what()) = "nope");
assert(threw && "sync_wait must rethrow the task's exception");
std::cout << "All sync wait checks passed! \n";
/ /
//
STRETCH GOAL (not tested): make sync_wait work for a task that only completes on ANOTHER thread. Approach: wrap task in a small coroutine
// that co_awaits it and then releases a std:: binary_semaphore in its
// continuation; sync_wait blocks on that semaphore instead of asserting
// is_done(). Try it once you've studied exercise 7 (schedulers).
// return e;