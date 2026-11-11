exercise coro_2_compose. Cpp
// COROUTINE EXERCISE 2 of 3 - Compose generators: take / filter / map / zip.
//
// Build: g++ -std=c++20 -Wall -Wextra -o c2 exercise_coro_2_compose.cpp
// Run:
./c2
//
// GOAL: a WORKING Generator<T> is provided (from exercise 1). Your job is to
// implement generator ADAPTORS - coroutines that consume one generator and
// produce another - to build lazy pipelines like:
//
/
for (int x: take(5, map([](int n){ return n*n; }, naturals())))
1/
...
1/
// Because generators are lazy pull chains, composing them is just writing more
// coroutines that co yield from inside a range-based for over their input.
// Nothing is computed until the outermost consumer pulls.
//
when correct, the program prints "All compose checks passed!".
// YOUR TASKS: implement the 4 adaptor coroutines (take, filter, map, zip).
// Each is only a few lines. The pattern is always:
//
for (auto x : input) {...decide... coyield something; }
	#include <cassert>
	#include <coroutine>
	#include <cstdint>
	#include <exception>
	#include <functional>
	#include <iostream>
#include <tuple>	
	#include <utility>
	#include <vector>
I
============= PROVIDED: Generator<T> (from exercise 1) ===
==
template <typename T>
class Generator {
public:
struct promise_type (
T current;
std:: exception_ptr exception;
Generator get_return_object() {
return Generator(std:: coroutine_handlespromise_type>:: from_promise(*this)}:

~Generator) {if (h)h_.destroy(); }
struct iterator {
handle_type h;
iterator& operator++() {
h. resume();
if (h.done() & h.promise().exception) std: :rethrow_exception(h.promise().exception);
return *this;
}
const T& operator* () const { return h.promise).current; }
I coned
}3
bool operatorl=(std:: default.sentinelot s) const {return |(*this = s);
iterator begin() {
if (h_) {
h_. resume();
if (h_.done() && h_.promise().exception) std: :rethrow_exception(h_-promise().exception);
return iterator (h_};
std:: default_sentinel_t end() {return (;}
private:
handle_type h_ = f:
};
== SOURCE GENERATORS (provided) =--
|/ Infinite: 0, 1, 2, 3, ...
Generator<int> naturals()
{
for (int i = 0;; +i) co yield 1;
(safe because consumers
are lazy and finite
I
// A finite source from a vector.
Generator<int> from(std: : vector<int> v) {
for (int x: v) co yield x;
//
== YOUR TASKS ===
// TAKE: yield at most the first in values of input", then stop.

Generator<int> take(int n, Generator<int> input) {
(void)n; (void)input;
// ToDO: iterate "input; co yield each value; stop after n values.
// Remember to "break" once you've yielded n.
coreturn:
// FILTER: yield only values of input for which pred(value) is true.
Generator<int> filter(std: function<bool(int)> pred, Generator<int> input) {
(void)pred; (void)input;
// TODO: for each xin input, if pred(x) coyield x.
coreturn;
}
// MAP: apply fn to every value of input and yield the result.
Generator<int> map(std:: function<int(int)> fn, Generator<int> input) {
(void)fn; (void) input;
// TODO: for each x in input, co yield fn(x).
hereturn
// ZIP: yield sums of paired elements until EITHER input is exhausted.
I/ (Zipping to a pair<int, int> is more idiomatic, but summing keeps the element
// type int so it composes with the other adaptors here. Advance both inputs
I/ in lock-step using explicit iterators.)
Generator<int> zip_sum(Generator<int> a, Generator<int> b) {
(void)a; (void)b;
// TODO:
//
auto ia = a.begin(); auto ib = b.begin();
while (ia l= a.end() && ib I= b.end()) {
//
co yield *ia + *ib;
//
++ia; ++ib;
11
cooreturn;
=====
===== helper: collect a generator into a vector ====
static std:: vector<int> collect(Generator<int> g) {
std:: vector<int> out;
for (int x: g) out.push_back(x); return out;
}
1ニーーー
int
main(
===== SELF-CHECK HARNESS ====
===

int main() {
// take from an infinite source.
assert((collect(take(5, naturals())) = std: :vector<int>(0, 1, 2, 3, 4)));
// take more than available -> just get what's there.
assert((collect(take(10, from({1, 2, 3}))) == std: :vector<int> {1, 2, 3)));
// filter evens.
assert (collectfilter([](int x) { return x % 2 = 0; J, from({1, 2, 3, 4, 5, 6})))
= std:: vector<int>(2, 4, 6}));
// map square.
assert((collect(map([] (int x) {return x * x; }, from({1, 2, 3, 4})))
= std:: vector<int> {1, 4, 9, 16}));
// zip_sum, stops at shorter input.
assert((collect(zip_sum(from({1, 2, 3}), from({10, 20, 30, 40})))
= std:: vector<int>{11, 22, 33}));
// A full lazy pipeline over an INFINITE source:
1/
//
naturals -> map(square) -> filter(odd) -> take(4)
squares: 0 1 4 9 16 25 36 49...
odd ones: 19 25 49...
auto pipeline =
take (4,
filter([](int x) {return x % 2 == 1; ),
map([](int x) { return x * x; }, naturals())));
assert((collect(std:: move(pipeline)) == std: :vector<int>(1, 9, 25, 49})) ;
std:: cout << "All compose checks passed! \n"; return e;

}