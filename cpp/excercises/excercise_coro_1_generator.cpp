exercise_coro_1_generator.cpp>..
1/ exercise_corogenerator p
1/ COROUTINE EXERCISE 1 of 3 - Build a Generator<T> from scratch.
//
// Build:
// Run:
g++-std=c++20-wall -Wextra-oc1 exercisecoro_1_generator.cpp
•/c1
1/
// GOAL: implement a lazy pull-based Generator<T› using a hand-written promise
// type (std:: generator<T> is C++23 and not available on gt+ 13, and writing it
// yourself is the whole point). When correct, the program prints
// "All Generator checks passed!".
// WHAT A GENERATOR NEEDS
//
// A generator coroutine uses "co yield v to produce a value and suspend.
// Recall from your notes: co yield v is co_await promise.yield_value(v) • //
// For LAZY (pull) semantics:
//
//
* initial_suspend() returns suspend always > nothing runs until first pull
1/
* yield_value(v) stashes v in the promise and returns suspend_always
//
* final_suspend() returns suspend_always
//
consumer can observe "done" before destroy ()
- frame stays alive so the
// The Generator owns the coroutine_handle (move-only, destroys in ~Generator).
// It exposes an input iterator so range-based for works:
//
//
for (int x: fib(10)) use(x);
//
// The iterator's operator++ resumes the coroutine; operator* reads the stashed
//
value; comparison against end() checks handle. done().
//
//
YOUR TASKS: fill in the 6 TODOs below.
	#include ‹coroutine›
	#include <cstdint>
	#include <exception›
	#include <iostream›
	#include <vector›
#include ‹cassert>
template ‹typename
class Generator {
public:
I
T>

1 current;
std:: exception_ptr exception;
// most recently yielded value
// captured if the body throws
// TODO 1: return the Generator that wraps this coroutine.
Hint: Generator{ std: coroutine_handle<promise_type>:: from_promise(*this)
Generator get_return_object() {
return Generator;
// placeholder (incorrect)
// TODO 2: make the coroutine LAZY - do not run the body until first pull.
std: suspend_always initial_suspend)noexcept {return O;} l Keep the frame alive after the body finishes so the consumer can see done(). std: suspend_always final_suspend() noexcept freturn O;}
/ TODO 3: store the yielded value and suspend.
1/
Signature is provided; set current then return suspend_alwayst}.
std:: suspend_always yield_valueT value) noexcept {
(void) value; return f};
}
void return_void() noexcept {}
void unhandled_exception) {exception = std:: current_exception(); }
using handle_type = std:: coroutine handlespromise_type>;
Generator() = default;
explicit Generator (handle_type h): h_(h) (}
Generator(const Generator&) = delete;
Generator& operator=(const Generator&) = delete;
Generator(Generator&& o) noexcept : h_(oh_) { o.h = 0: }
Generator& operator=(Generator&& o) noexcept {
if (this I= 80) { if (h_) h_.destroy(); h_ = o.h; o.h= (:}
return *this;
}
~Generator () { if (h_) h_.destroy(); }
---- input iterator ----
struct iterator {
handle type h;

// TODO 4: advance the coroutine. Resume it; if it finished with a stored 1/
exception, rethrow it.
iterator& operator++() {
// resume h; then if (h.done() && h.promise().exception) rethrow
return *this;
// TODO 5: read the current value from the promise.
const T& operator* ()
const (
static T dumny C:
// placeholder - replace with the real value
return dummy;
// Two iterators are "equal" when both are at the end. We model end() as a
// null handle; a live iterator is at end when its coroutine is done.
bool operator== (std:: default sentinelat) const {
return Ih || h.done();
};
bool operator|-(std: :default sentinelot s) const {return |(*this = s); }
iterator begin() {
// TODO 6: kick off the first value: if we have a handle, resume once so
//
current" holds the first yielded value, then return iterator(h.
(If the coroutine finishes immediately, begin() = end() naturally.)
return iterator (h};
std: default sentinelt end() (return (;)
private:
handle_type h_ = {:
}
;
// ===
= GENERATORS THAT USE YOUR TYPE ==
// First 'n° Fibonacci numbers: 0 1 1 2 3 5 ...
Generator<std: :uint64_t> fib(int n) (
std:: uint64_t a = 0, b = 1;
for (int i = 0; i ‹ n; ++i) {
co yield a;
std: uint64 t next = a + b:

for (int i = 0; i ‹ n; +i) {
a = b;
b = next;
// Half-open integer range [lo, hi).
Generator<int> range(int lo, int hi) {
for (int i = 1o; i < hi; Hi) co yield i;
｝
// A generator that yields nothing (tests begin() == end()).
Generator<int> empty_gen() {
ww
｝
SELF-CHECK HARNESS =
int main() {
// 1. Fibonacci.
std:: vectorstd::uint64_t> got;
for (auto x : fib(10)) got.push_back(x);
std: vector<std: suint64_t> want = {0, 1, 1, 2, 3, 5, 8, 13, 21, 34};
assert (got == want);
」
｛
// 2. Range.
std:: vector<int> got;
for (int x: range(3, range(3, 8))got.push_back(x); assert ((got == std:: vector<int> (3, 4, 5, 6, 7}));
// 3. Empty generator: the loop body must never run.
int count = 0;
for (int x: empty_gen()) { (void)x; ++count; } assert (count == 0);
4. Laziness: creating a generator must NOT run the body yet.
bool started = false;
auto lazy = [&1) -›'Generator<int> {
I started = true;
co yield 42;
ww
// only runs after the first pull
auto g = lazy();
assert (started = false & "generator body ran before first pull!");
auto it = g.begin();
/ now it should start
assert (started = true);
assert(*it == 42);
// 5. Manual iteration matches range-based for.
1
auto g = range(0, 5);
int expect = 0;
for (auto it = g.begin(); it l= g.end(); ++it)
assert (*it == expect++);
assert(expect == 5);
std:: cout « "All Generator checks, passed! \n";
return e;