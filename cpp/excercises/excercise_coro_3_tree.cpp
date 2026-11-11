lowledge-garden > cpp> exercises > exercise_coro_3_tree.cpp>...
// exercise_coro_3_tree.cpp
// COROUTINE EXERCISE 3 of 3 - Recursive tree traversal as a generator.
11
// Build: g++ -std=c++20 -Wall -Wextra -o c3 exercise_coro_3_tree.cpp
// Run:
•/c3
1/
// GOAL: express binary-tree traversals (in-order, pre-order, post-order) as
// LAZY generators using coroutines. This is where generators shine over
// hand-written iterators: a recursive tree walk becomes a recursive coroutine,
Il with no explicit stack and no state machine to maintain by hand.
11
// A WORKING Generator<T> is provided. The key new capability you 1l use is
// "yielding from a sub-generator": to splice a child traversal into the parent
// stream, iterate the child generator and co yield each element:
1/11
for (int x : inorder(node->left)) coyield x;
//
// (C++23's std: :generator has " coyield std: :ranges: :elements_of(child) to do
// this in one step, but it's unavailable on gt+ 13, so we splice by hand - which
I1 also makes the mechanism explicit.)
//
// When correct, the program prints "All tree-traversal checks passed!".
1/
1/11
YOUR TASKS: implement inorder, preorder, postorder as coroutines.
1/
#include <cassert>
I
//
PROVIDED: Generator<T> =
template ‹typename T>
class Generator {
public:
struct promise_type f
T current;
std:: exception_ptr exception;
Generator get return obiect. s
	#include <coroutine>
	#include <exception>
	#include <iostream›
#include <memory›	
#include <vector>	

return Generator(std: coroutine_handle<promise_type›::from_promise(*this)};
std: : suspend_always initial_suspend() noexcept { return 0); } std: suspend_always final_suspend() noexcept { return O; }
std:: suspend_always yield_valueT value) noexcept { current = std: move(value); return (;}
void return_void() noexcept {}
void unhandled_exception) {exception = std:: current_exception();
using handle_type = std:: coroutine handlespromise_type>;
Generator() = default;
explicit Generator (handle_type h) : h_(h) {}
Generator(const Generator&) = delete;
Generator& operator=(const Generator&) = delete;
Generator (Generator&& 0) noexcept : h_(o.h) { o.h= C; }
Generator& operator=(Generator&& o) noexcept {
if (this I= 80) { if (h) h_•destroy); h_= o.h: o.h_= 0: }
return *this;
~Generator() { if (h_) h_•destroy(): }
struct iterator {
handle_type h;
iterator& operator++() {
h. resume();
if (h.done() && h.promise().exception) std: :rethrow_exception(h.promise).exception);
return *this;
}
const T& operator* () const { return h.promise().current; } poo1 operator=(sta:: detault sentinel t) const {Treturn Ih || h.done();
bool operator!-(std: :defaultsentinelots) constreturn 1(*this = s); }
iterator begin() {
if (h_) (
h_. resume();
if (h_.done() && h_•promise().exception) std::rethrow_exception(h_•promise().exception);
return iterator (h_);
std:: default sentinel t end() (return (:)

private:
｝；
handle_type h_ = 0:
- PROVIDED: a small BST ==
struct Node {
int value;
std:: unique_ptr‹Node> left, right; explicit Node(int v) : value(v) {}
33
// Insert into a BST (unique keys).• void insert(std:: unique_ptr‹Node›& root, int v) {
if (Iroot) { root = std: make_unique‹Node> (v); return; } if (v.< root-›value) insert(root->left, v); else if (v > root-value) insert (root-›right, v);
/=
= YOUR TASKS ==
// Each traversal takes a raw observing pointer (the tree owns the nodes).
// Splice child traversals with: for (int x: childTraversal(•..)) co_yield x;
// IN-ORDER: left subtree, node, right subtree (yields BST keys in SORTED order)
Generator<int> inorder(const Node* n) {
(void)n;
// TODO:
if (In) co_return;
//
for (int x : inorder(n->left.get())) co_yield x;
//
co yield n-›value;
//
for (int x: inorder(n-›right.get())) co_yield x;
co return;
// PRE-ORDER: node, left subtree, right subtree
Generator<int› preorder(const Node* n) {
(void)n;
// TODO: node first, then left, then right.
co return;
I
}
// POST-ORDER: left subtree, right subtree, node
Generator<int› postorder(const Node* n) {
(void)n;
/ TODO: left, then right, then node.

o return
==== helper ===
static std: vector‹int> collect(Generator<int> g) (
std:: vector<int> out;
for (int x: g) out.push_back(x);
return out;
SELF-CHECK HARNESS
int main() {
//
1/
1/
1/
//
//
1/
8
/
3
10
1
14
std: unique_ptr‹Node> root;
for (int v: {8, 3, 10, 1, 6, 14, 4, 7}) insert(root, v);
// in-order of a BST == sorted.
assert((collect(inorder(root.get()))
== std: :vector<intx{1, 3, 4, 6, 7, 8, 10, 14}));
// pre-order.
assert((collect(preorder(root. get()))
= std:: :vector<int>(8, 3, 1, 6, 4, 7, 10, 14}));
// post-order.
assert((collect(postorder(root-get()))
= std:: vector<int>{1, 4, 7, 6, 3, 14, 10, 8}));
I
// Empty tree yields nothing.
assert(collect (inorder(nullptr)) .empty()) ;
// LAZINESS: we can stop early without walking the whole tree. Pull only the
// first 3 in-order values (the three smallest keys) and stop.
{
std:: vector‹int› firstThree;
auto g = inorder(root.get()):
int taken = 0;
for (auto it = g.begin(); it |= g.end() && taken < 3: Hit. ++taken)

firstThree•push_back(*it);
assert((firstThree == std: :vector<int>{1, 3, 4}));
std:: cout « "All tree-traversal checks passed! \n";
return 0;
