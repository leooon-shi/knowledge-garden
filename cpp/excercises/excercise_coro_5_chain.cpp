.••
/
exercise_coro_5_chain.cpp
/ COROUTINE EXERCISE 5 of ? - Task<T> continuation chaining (symmetric transfer)
/
/ Build: gt+ -std=c++20 -Wall -Wextra -o c5 exercise_coro_5_chain.cpp
/ Run:
•/c5
GOAL: make one Task able to "co_await another. This is THE concept that turns / toy coroutines into real async ones. When a parent does "co_await child, the / parent suspends, the child runs, and when the child finishes it must RESUME
// THE PARENT. Doing that resumption via "symmetric transfer" (returning a
I/ coroutine_handle from await_suspend) avoids growing the C+ call stack, so you
// can chain arbitrarily deep without stack overflow.
// THE TWO PIECES YOU IMPLEMENT
//
----
11(A) The AWAITER returned by
1/
"operator co_await" on a Task. When the parent
awaits a child task:
//
await_ready ()
- false (child hasn't run yet)
await_suspend (parentHandle):
*   record parentHandle as the child's continuation
*   RETURN the child handle -> control transfers into the child await_resume() -> return the child's result (rethrow if it threw)
// (B) The FINAL AWAITER returned by the promise's final_suspend(). When a child
1/
finishes:
//
await_ready ()
-> false (we must hand control back)
await
_suspend (childHandle):
1/
RETURN the stored continuation (the parent), or noop_coroutine() if there is none
-control transfers back to the parent
await_resume()
-> nothing
The promise stores std:: coroutine_handle‹› continuation linking child-›parent.
YOUR TASKS: fill in TODO A1, A2 (awaiter) and F1 (final awaiter).
#include ‹cassert>
#include ‹coroutine› #include ‹exception›
#include <iostream›
#include <utility>
template <typename

class Task public:
struct promise_type {
T valued;
std:: coroutine_handle‹> continuations // who to resume when we finish std: :exception_ptr exception;
Task get _return_object() {
return Task(std::coroutine_handle<promise_type›:: from_promise(*this)}:
std: :suspend_always initial_suspend() noexcept { return O; }
struct FinalAwaiter {
bool await_ready() noexcept { return false; }
// TODO F1: hand control back to the awaiting parent.
//
Return h.promise). continuation if set, else std:: noop_coroutine(). std:: coroutine_handle&> await_suspend(std: :coroutine_handle<promise_type> h) noexcept
(void)h;
return std:: noop_coroutine();
// placeholder: correct for "no parent"
/l but you must return the continuation
// when one exists.
void await_resume() noexcept (}
};
FinalAwaiter final_suspend) noexcept { return ti}
void return_value(Tv) noexcept f value = std: :move(v); }
void unhandled_exception) {exception = std:: current_exception(): }
};
using handle_type = std:: coroutine handlespromise_type>;
Task() = default;
explicit Task(handle_type h) : h_(h) {)
Task( const Task&) = delete;
Task& operator=(const Task&) = delete;
Task (Task&d noexcept noexcept : h_(std:: exchange(oh, {)) (
Task& operator=(Task&& o) noexcept {
if (this |= 80) { if (h_) h_.destroy); h_ = std: :exchange(oJh, (3): )
return *this;
~Task() { if (h_) h_•destroy(); }
// Awaiter: what co_await someTask produces.
struct Awaiter {

handle_type callee;
bool await_ready) noexcept { return false; }
// TODO A1: suspend the caller, remember it as the callee's continuation,
// and transfer control INTO the callee by returning its handle.
std:: coroutine_handle‹> await_suspend(std:: coroutine_handle‹>caller) noexcept {
(void) caller;
// callee.promise(). continuation = caller;
I/ return callee; return callee;
//‹-- transfer is right, but you must set continuation first
// TODO A2: produce the child's result (rethrow if it captured an exception).
T await_resume() {
// if (callee. promise). exception) std:: rethrow_exception(callee.promise().exception);
// return std:: move(callee.promise) .value);
return TO; // placeholder (incorrect)
}
;
Awaiter operator co await) freturn Awaiter(h_); )
// For the harness: drive the top-level task and read its result.
void resume() {if (h_&& Ih_•done()) h_.resume(); }
bool done() const {return h_&& h_.done(); }
T result() (
if (h_.promise().exception) std: :rethrow_exception(h_•promise().exception);
return std: move(h_• promise) value);
}
private:
handle_type h_= (;
};
=I
Task<int> leaf (int x) {
co return
*
2；
Task<int> middle(int x) {
I
TASKS THAT CHAIN =
====

int a = conawait
Leat x)
co return a + 1;
Task<int> top() {
int a = coawait middle(10);
int b = co_await middle(20);
co return a + b;
I suspends middle, runs leaf, resumes middle
// 10*2 + 1 = 21
// 20*2 + 1 = 41
// 62
|/ Deep chain to prove symmetric transfer doesn't overflow the stack when 1/ RESUMING. (Note: this simple Task still destroys its frame chain recursively
// in ~Task, so extremely deep chains can overflow at destruction time - a
// production task type cleans up iteratively. 20000 stays well within a default
// 8 MB stack; symmetric transfer is what lets the *resume* phase go this deep at
// all.)
Task<int> deep(int n) {
if (n = 0) co return 0;
int rest = co await deep(n - 1);
co return rest + 1;
Exception propagation across a chain.
Task&int> throwing() (
throw std: runtime_error("deep failure");
co_return
Task<int> catchesNothing() {
int x = conawait throwing();
coreturn x
// exception should surface here
========
int main() {
/ Single-level await.
=== SELF-CHECK HARNESS ==
ニニニ
auto t = middle(5);
t. resume() ;
assert (t.done()) ;
assert(t.result() == 11);
// 5*2 + 1 = 11
/ Multi-level, multiple awaits.

auto t = top();
t. resume() ;
assert(t.result() = 62);
// Deep chain (would blow the stack at RESUME without symmetric transfer).
auto t = deep(20000);
t. resume()
assert(t.result() = 20000);
// Exception propagates up through the awaiting chain.
auto t = catchesNothing();
t. resume ();
bool threw = false;
try [ t.result();
} catch (const std: :runtime_error& e) {
threw = (std: :string(e.what()) = "deep failure");
assert (threw && "exception did not propagate across co_await");
std: cout « "All chaining checks passed! \n";
return 0;