// exercise_coro_4_task.cpp
// COROUTINE EXERCISE 4 of ? - Build a lazy Task<T> (the co_await gateway).
//
// Build: g++ -std=c++20 -Wall -Wextra -o c4 exercise_coro_4_task.cpp
// Run:
•/c4
//
// GOAL: implement a lazy Task<T> - a coroutine that computes a single value and
// hands it back. Unlike a Generator (which uses co yield to produce a stream),
/ aTask uses coreturn to produce ONE result, retrieved after the coroutine
// runs to completion. This exercise isolates the value-returning promise; the
// NEXT exercise adds co_await chaining between tasks.
1/
// "Lazy" means: creating the Task does not run the body. The body runs when the
// task is resumed (here, driven manually by the harness; a real program uses a
// scheduler or sync wait - see exercise 6).
//
//
//
PROMISE REQUIREMENTS FOR A VALUE-RETURNING COROUTINE
//
//
/
//
//
get_return_object ()
initial_suspend()
final_suspend()
-  > the Task wrapping this coroutine's handle
-  > suspend_always (LAZY: don't run until resumed)
-  > suspend_always (keep frame alive so we can read
return_value(v)
the value; the Task destroys the frame in its dtor)
→ store the final result (NOT return_void!)
unhandled_exception) - capture, to rethrow when the caller reads result)
1/
//
YOUR TASKS: fill in the 4 TODOS.
template ‹typename
T>
class Task {
public:
struct promise_type f
T valuel);
std:: exception_ptr exception;
/ TODO 1: return the Task wrapping this coroutine.
	#include ‹cassert>
	#include ‹coroutine›
	#include <exception›
	#include < iostream›
#include ‹string>	
	#include ‹utility>
I

Task get_return_object) {
return Taskf};
// placeholder (incorrect)
// TODO 2: make the task LAZY (do not run body on creation).
std:: suspend_always initial_suspend() noexcept (return (;}
// Keep the frame alive after completion so result() can read value.
std:: suspend_always final_suspend()noexcept {return (;]
// TODO 3: store the returned value.
void return_value(Tv) noexcept {
• (void)v;
｝
void unhandled_exception) {exception = std:: current_exception); }
using handle_type - std: :coroutine_handlespromise_type>;
-Task() = default;
explicit Task(handle_type h) : h_(h) {}
Task(const Task&) = delete;
Task& operator=(const Task&) = delete;
Task(Task&& 0) noexcept : h_(std:: exchange(oh, ())) ()
Task& operator=(Task&& o) noexcept {
if (this I= 80) { if (h_) h_•destroy(); h= std::exchange(o.h, (); }
return *this;
｝
~Task() { if (h_) h_•destroy(); }
// Drive the task to completion (single-step is enough here since there is no
Il suspension inside the body yet). Provided for the harness. void resume() {if (h_&& Ih_•done()) h_•resume(); }
bool done() const { return h&& h_•done(); }
// ToDO 4: return the computed result. If an exception was captured, rethrow
/ it; otherwise return the stored value.
T result) {
// if (h_•promise).exception) std:: rethrow_exception(h_•promise().exception);
// return std:: move(h_• promise(). value);
return TO;
// plaçeholder (incorrect)

private:
handle_type h = f
//
Task<int> answer() {
co return 42;
TASKS THAT USE YOUR TYPE =
Task<int> sum(int a, int b) {
co return a + b;
Task<std: string> greetstd: istring name) €
co return "hello.+ name;
Task<int> boom() {
throw std: :runtime_error("kaboom");
co_return
0;
// makes this a coroutine
//
int main() {
// Basic value return.
auto t = answer();
t. resume( assert(t.done()) ;
assert(t.result() == 42);
SELF-CHECK HARNESS =
// Arguments are captured into the frame.
auto t = sum (20, 22);
t. resume () ;
assert (t.result) == 42);
Non-trivial value type.
auto t = greet ("world");
t. resume()

}
assert(t.result() = "hello, world");
// LAZINESS: constructing the task must not run the body.
bool ran = false;
auto lazy = [&]() -> Task<int> {
ran = true;
coreturn 1;
｝；
auto t = lazy();
assert(ran = false && "task body ran before resume!");
t. resume();
assert(ran == true);
assert(t. result() = 1);
// Exceptions propagate through result().
｛
}
auto t = boom();
t. resume():
bool threw = false;
try {
t. result();
} catch (const std: runtime_error& e) {
threw = (std:: string(e.what()) = "kaboom");
assert (threw && "exception did not propagate through result() ");
std:: cout « "All Task checks passed! \n"; return ø;
}