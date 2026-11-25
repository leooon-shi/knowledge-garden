K!-- cpp/ranges. md --
# C++20 Ranges
A range is anything you can iterate: a pair of "[begin, end) bundled into one object. Th Ranges library (C++20, extended in C++23) builds two things on top of that idea:
1.
**Range algorithms**- std::ranges: :sort(v) instead of std::sort(v.begin(), v.end())
2. **Views & adaptors** - lazy, composable pipelines: "v | filter(...) | transform(...)*• If you have worked through the coroutine **generator** exercises in exercises/, views will feel familiar: both are *Lazy pulL* sequences. A view is the STL's built-in, allocation-free way to express the same idea a generator expresses with co yield.
*   Toolchain note: examples verified on **g++ 13**, '-std=c++20°. Core C++20 views all
*   work. C++23 "zip / enumerate work; std::ranges::to does **not** (a manual "for" loop
*   into a container is used instead where needed).
## Table of Contents
- [C++20 Ranges](#c20-ranges)
  - [Table of Contents](#table-of-contents)
  - [1. Range concepts and the vocabulary](#1-range-concepts-and-the-vocabulary)
  - [1. Range concepts and the vocabulary](#1-range-concepts-and-the-vocabulary-1)
  - [2. Range algorithms](#2-range-algorithms)
  - [2. Range algorithms](#2-range-algorithms-1)
  - [3. Views and the pipe operator](#3-views-and-the-pipe-operator)
  - [3. Views and the pipe operator](#3-views-and-the-pipe-operator-1)
  - [4. The adaptor catalogue](#4-the-adaptor-catalogue)
  - ["enumerate	(index, element) pairs	C++23](#enumerateindex-element-pairsc23)
  - [5. Laziness: what actually runs, and when](#5-laziness-what-actually-runs-and-when)
  - [Dangline and Iifetime ditfalls](#dangline-and-iifetime-ditfalls)
  - [8. Bounded iterator operations](#8-bounded-iterator-operations)
  - [8. Bounded iterator operations #include ](#8-bounded-iterator-operations-include-)
  - [9. Views vs. generators](#9-views-vs-generators)
  - [10. Practice](#10-practice)
    - [Key takeaways](#key-takeaways)
## 1. Range concepts and the vocabulary
A **range** is any type with begin() and 'end(). 'end() may return a **sentinel** of a different type than the iterator (this is what lets infinite and lazily-terminated ranges exist).

Concept	Guarantees	Examples
input_range		
forward _range	single-pass, read once multi-pass, re-iterable	input streams, generators
" bidirectional range	--it works	•forward list
* random_access_range	it + n°
, it[n]	list
"map, "set"
" contiguous_range	elements in contiguous memory I vector, "array, string,	•deque
## 1. Range concepts and the vocabulary
The concept hierarchy (each refines the previous):
"span"
**view** is a range that is cheap (o(1)) to copy and does not own its elements (std::ranges: view). Views are the composable building blocks; containers are the data.
#include <ranges›
static_assert(std::ranges: random_access_rangestd:: vector‹int»; static_assert(std::ranges: :contiguous_range‹std: astring›);
static_assert(std::granges: :viewstd: ranges: iota_viewsint, int>>):
## 2. Range algorithms
*std::ranges:: versions of ‹algorithm› take a range directly, return richer results, and support **projections** - a per-element transform applied before the predicate, so you can sort/search "by a field" without a custom comparator.
•**cpp
#include <algorithm›
#include <ranges›
#include ‹vector›
#include ‹string›
struct Person { std::string name; int age; };
std:: vector‹Person› people = ({"carol",40},("alice", 30), ("bob", 25));
// sort by age using a PROJECTIONL (&Person: :age) - no lambda comparator needed.
std:: ranges::sort(people, (), &person: :age);

## 2. Range algorithms
// people is now bob (25), alice(30), carol(40)
// Find by projected field.
auto it = std::ranges: :find(people, 30, &Person: sage);
// -› alice
// Algorithms return useful info: ranges:: for_each returns (in, fun}.
std::vector<int> v = {1,2,3,4,5);
auto n = std::ranges::count_if(v, [](int x){ return x > 2; });
//3
The projection argument (the "&Person: age) is the everyday win of range algorithms:
•sortpeople, (), &Person: sage) reads far better than a [](auto&a, auto&b){ return a.age ‹ b.age; } comparator.
## 3. Views and the pipe operator
A **view adaptor** wraps a range and lazily transforms it. operator| chains them left-to-right, so data reads like a pipeline:
...
cpp
#include <ranges> #include ‹vector›
#include <iostream>
namespace views = std::views;
// alias; std::ranges::views == std::views
int main() {
std:: vector<int> v = (1,2,3,4,5,6,7,8, 9,10);
auto result = v
views: filter([](int x){return x% 2 = 0; })
views: transform([](int x){ return x * x; }) views:: :take(3);
//246810
/ 416
36 64 100
// 4 16 36
for (int x: result) std::cout « x«•';
// 4 16 36
*v | filter(p)* is exactly views:: filter(v, p) - the pipe is sugar. Composition is 0(1):

## 3. Views and the pipe operator
building result allocates nothing and computes nothing; work happens only when you iterate (see §5).
## 4. The adaptor catalogue
The adaptors you will reach for most (all in std::views"), with the C++ version that introduced them:
Adaptor	Effect	Since
•filter(pred)	--:	
	keep elements where pred is true	
"transform(fn)	apply fn to each element	C++20
•take (n)	first n" elements	C++20
C++20
*take_while(pred)	prefix while pred holds	C++20
• drop(n)	skip first n	
drop while(pred)	skip prefix while pred holds	C++20
reverse*	iterate backwards	C++20
C++20
"keys" / values"	first / second of each pair (e.g. a
"map")	C++20
• elements<N>	the Nth element of each tuple	C++20
•split(delim)	split a range into subranges	C++20
"join	flatten a range-of-ranges	C++20
* iota(start[, bound])	generate start, start+1,.. (factory)	C++20
• single (x)
empty<T>	one-eLement / zero-element view (factory)	C++20
zip(r...)	tuples of corresponding elements	C++23
"enumerate	(index, element) pairs	C++23
-
Factories (iota, "single" , "empty") start a pipeline instead of adapting one:
cpp
namespace views = std::views;
// 3,4,5,6,7 (iota is lazy; the bound makes it finite)
for (int x: views: views::iota(3, 8)) std:: cout « x«':
// map keys / values
std:: map<std:: string,int> m = {{"a"
‚1), ("b"
,2},["c"
, 3)};
for (auto& k: m | views:: keys)
std:: cout «< k;
// abc
for (auto& val: m | views::values) std::cout «< val: // 123



// split a string on commas
std::string s = "a,bb, ccc";
for (auto part: s | views::split(',')) {
std:: string p(part.begin(), part.end());
std::cout « p«
}
// materialise one field
// a.bb.ccc.
// flatten with join
std:: vector‹std: :vector<int» nested = {{1,2}, {3),{4,5});
for (int x: nested | views: join) std::cout «< x; // 12345
C++23 "zip" and "enumerate" (available on g++ 13):
•**cpp
std:: vector‹std: string> names = {"a"
', "b", "c"}3
std:: vector‹int> scores = (10,20,30};
for (auto [name, scorel: std::views: :zip(names, scores))
std:: cout « name « = «< score « ';
for (auto [i, name]: std::views: :enumerate(names))
std:: cout «< i « ':' « name «< "';
// a=10 b=20 c=30
// 0:a 1:b 2:0
## 5. Laziness: what actually runs, and when
This is the property that makes views both fast and occasionally surprising. Adaptors do
**no work** when composed - they only remember what to do. Elements are computed **one at a time, on demand, during iteration**, and a downstream "take can stop the whole pipeline early.
Proof - a transform with a visible side effect, behind a "take(2)' over a million-element source:
***cpp
#include <ranges›
#include <iostream›
namespace views = std:: views;


int transforms = 0;
int main() {
auto pipe = views: Hiota(1, 1'000"000)
views: transformI](int x){++transforms; return x; })
views: take(2);
int sum = 0;
for (int x: pipe) sum += x;
std:: cout « "sum=" « sum « " transforms-" « transforms « "In';
// sum=3 transforms=2
<-- only TwO elements were ever transformed
Consequences to internalise:
-   Composing a pipeline is o(1) and allocation-free.
-   An infinite source ( iota(1)" with no bound) is fine as long as something downstream bounds it take, "take_while").Re-iterating a pipeline re-runs the work each time - a view is a recipe, not a cached result. Materialise into a container if you need to iterate repeatedly.
#include	<ranges›
#include	‹string>
#include	‹string_view>
#include	<vector>
#include	< charconv›
#include	‹numeric>
	#include <iostream>
#* 6. A worked pipeline
The Week 4 practice from the study plan: parse a CV, filter, transform, aggregate - with ranges instead of hand-written loops.
•**cpp
namespace views = std::views:

int main() i
std::string csv = "3,17,4,9,22,8,15,42, 1, 30";
// Parse "a,b,c" -> ints, keep those › 10, double them, sum the result. std:: vector<int> nums;
for (auto field : csv | views::split(',')) {
std::string_view sv(field.begin(), field.end());
int value = 0;
std:: from_chars(sv.data(), sv.data() + sv. size(), value);
nums• push_back(value);
auto pipeline = nums
views: filter([](int x){ return x> 10; }) views: transform([](int x)f return x * 2; });
int total = 0;
for (int x: pipeline) total += x; std:: cout « "total = " « total «< "\n';
// 17 22 15
42
30
// 34 44 30
84
• 60
// 252
Note the "split" step is materialised through a small loop because parsing needs a contiguous "string_view, and because g++ 13 lacks std::ranges: :to with ranges:: to, the parse becomes a single expression:
• On a C++23 Library
*** cpp
// C++23 with std::ranges::to (NOT available on g++ 13):
auto nums = csv | views: :split(',')
views: transform([](auto f) {
int vf: std::string_view sv(f.begin(), f.end()):
std:: from_chars(sv.data(), sv.data()+sv.size(), v);
return vi
})
std:: ranges: to‹std: :vector>);
## Dangline and Iifetime ditfalls

A view does **not** own its data. If the underlying range dies, the view dangles - the same hazard as an iterator outliving its container.
•cpp
// BUG: the temporary vector is destroyed at the semicolon; "bad" dangles.
auto bad = std:: vector<int>{1,2,3} | std:: views: transform([](int x)(return x*x;});
for (int x: bad) /* undefined behaviour */;
// FIX: keep the owner alive.
std::vector‹int> owner = {1,2,3);
auto ok = owner | std::views:: transform([](int x){ return x*x; });
The library helps in two ways:
-   Range algorithms return std::ranges: :dangling" instead of an iterator when called on an Irvalue range, turning a runtime bug into a compile error.
-   views:: owning_view (and passing an rvalue range into a pipeline) can take ownership so the pipeline is self-contained - but be deliberate about it.
Rule of thumb: a view should not outlive the range it adapts. If you need to store or return the result, materialise it into a container.
## 8. Bounded iterator operations
Separate from views, "std::ranges" also provides **bounds-aware** versions of the classic iterator utilities. classic std::prev / std::next/std:: advance have no bounds awareness - stepping past begin() / end() is undefined behaviour:
***cpp
std:: vector<int> v = {3, 1, 4};
auto it = v.begin() + 1;
auto bad = std:: prev(it, 42);
...
// UB: only 1 step available
The std:: ranges" overloads take a
**bound** and stop there:
cpp

## 8. Bounded iterator operations #include <ranges>
std::vector<int>v = {3, 1, 4};
auto pv = std::ranges: :prev(v.begin() + 1, 42, v.begin()); // stops at begin() -> 3
auto n = std::ranges: :nextv.begin(), 100, v.end());
// stops at end()
auto it = v.begin() + 1;
int remaining = std::ranges: :advanceit, 42, v.end());
| Feature	std::prev / advance	• std:: ranges: prev
Bounds checking
•advance return
Cost
Use when	none (UB if out of range)
"void same
distance known valid	stops at the bound
Leftover step count
same (early exit at bound) distance may exceed the range
// it -> end(); remaining == 40
ranges: :advance returns the *unused* step count, so you can tell whether the bound was hit early.
/ advance
Handy for sliding windows and pagination:
**cpp
// Safe sliding lookback of up to 3 elements.
std::vector<int> data = {10,20,30,40, 50);
for (auto it = data.begin(); it I= data.end(); Hit) {
auto start = std:: ranges:: prev(it, 3, data.begin());
for (auto w = start; w l= it; ++w)
std:: cout ‹< *w «<
std:: cout « "| " « *it « 'In';
}
## 9. Views vs. generators
Both give lazy, single-pass, pull-based sequences. Which to reach for:
I std:: views" pipeline
coroutine "Generator<T›(see exercises/)/

Cost to compose
Best at
Control flow
Recursion (e.g. tree walk)
Standard
I std:: views" pipeline
000！
o(1), no allocation mapping/filtering existing ranges limited to the adaptor set awkward
C++20 library
coroutine "Generator<T›" (see "exercises/"
one coroutine frame (usually heap)
producing a sequence from arbitrary logic full: loops, recursion, branching, state natural (for (x: child) co_yield x;)
I you write the type (C++23 std:: generator")
Rule of thumb: use a **views pipeline** to transform data you already have in a range; use a **generator** when the sequence comes from procedural logic that doesn't map onto the adaptor set (recursive traversals, stateful producers, parsers). They compose: a generator
*is* an "inputrange, so you can pipe a generator through views: :filter/ transform.
## 10. Practice
1.    Rewrite a hand-written loop that filters and maps a vector as a single views" pipeline. Confirm identical output.
2.    Use "views: :iota + "filter" + take to produce the first 10 primes without precomputing an upper bound (relying on laziness).
3.    Prove laziness yourself: put a counter in a transform behind a take(k) and check it runs exactly k' times.
4.    Take one of your "Generator<T›" tree traversals from 'exercises/ and pipe it through
•views: filter to yield only even keys - demonstrating that a generator is a range.
5. Trigger the dangling-view bug on purpose (build with "-fsanitize=address ), then fix it by keeping the owner alive.
### Key takeaways
-   Views are lazy, non-owning, o(1)-to-compose range recipes; work happens on iteration and stops early when a downstream adaptor is satisfied.
-   Range algorithms + projections replace iterator-pair calls and custom comparators with clearer intent.
-   A view must not outlive its underlying range - materialise into a container when you need ownership or repeated iteration.
-   Views and generators solve the same "lazy sequence" problem from two angles; pick views for adapting ranges, generators for procedural/recursive producers.


