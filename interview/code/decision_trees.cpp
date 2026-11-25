decision_trees.cpp
decision trees.pp ...
// Three kinds of decision-making trees in C++20:
1. Classification Decision Tree (CART, Gini impurity)
I
2. Behavior Tree (Sequence/Selector/Action)
- statistical learning
1/
3. Minimax game tree with alpha-beta pruning
-  - reactive control (AI/robotics)
-  - adversarial search
// Build: g++ -std=c++20 -02 -Wall -Wextra decision_trees. cpp -o dtrees
/
clang++ -std=c++20 -02 -Wall -Wextra decision_trees. cpp -o dtrees
#include <algorithm>
#include <array> #include ‹cstdio>
#include ‹functional>
#include < limits› #include <memory>
#include
‹ranges>
#include
‹span>
#include
(vector›
1.
CLASSIFICATION DECISION TREE (CART with Gini impurity)
Learns axis-aligned splits from labeled numeric samples,
then predicts.
ニニニニニニニニニニニニニニニニニニニニニニニニニニニニニニニニニニニニニニ==============≤===
namespace mi {
struct Sample {
std: :vector<double› features;
int label;
｝；
class DecisionTree (
struct Node {
bool leaf - false;
int prediction = -1;
// valid if leaf
int feature = -1;
/l split feature index
double threshold = e.e;
// go left if feature <- threshold
std: unique_ptreNode› left, right;
｝；
std::unique_ptr<Node> root_;
int max_depth;
static double gini(std::span<const Sample* const> rows) {
if (rows-empty)) return 6.0; std::vector<int> counts;
for (auto*s : rows) {
if (s->label > (int)counts.size()) counts.resize(s->label + 1, e);
*counts [s->label];
}
double impurity - 1.0; const double n= (double)rows.size();
for (int c: counts) { double p = c / n; impurity -= p * p; }
return impurity;
｝
static int majority(std: :spankconst Sample* const> rows) f
std::vector<int> counts;
for (auto*s: rOWs) {
if (s->label >- (int)counts.size()) counts.resize(s->label + 1, 0); ++counts[s->1abe1];
｝
return (int)std: :distance(counts.begin(),
std: :ranges:: max_element(counts));
std::unique_ptr«Node> build(std::vector<const Sample*> rows, int depth) {
auto node = std:: make_unique<Node>();
double parent gini - gini(rows);
Il Stop: pure node,
depth limit, or too few samples.
if (parent gini = 0.0 || depth ›= max_depth || rows.size() < 2) {
node->leaf - true;
node-›prediction = majority(rows);
return node;
I/ Find the best (feature, threshold) minimizing weighted child Gini.
double best gain = 0,0;
int best feat = -1;
double best_thr = e.e;
const std::size_t nfeat - rows,front()->features,size();
for (std: :size_t f = f f‹ nfeat; ++f) {
auto sorted = rows;
std: ranges:: sort(sorted, (), [f](auto*s) (return s-›features(f]; )):
for (std: :size_t 1 - 1; 1 < sorted,size(); ++1) {
double a = sorted[1 - 1]->features[f];
double b - sorted[1]->features[f];
if (a == b) continue; double thr - (a + b)
/ 2,05
std::vector<const Sample*> L(sorted.begin(),
sorted.begin() + 1):
std::vector<const Sample*> (sorted.begin() + 1, sorted.end()) :
double w1
- (double)L.size() / rows.size();
double wr = (double)R.size() / rows.size();
double gain - parent_gini - (w1 * gin1(L) + wn * gin1(R)); if (gain > best gain) (

best_gain gain; best_feat - (int)f; best_thr thr:
if (best_feat < e) {// no useful split
node->leaf • true;
node-›prediction = majority(rows): return node;
std: :vector<const Sample"> L, R;
for (auto*s : rows)
(s->features [best_feat] - best_thr L: R). push_back(s):
node-feature - best_feat; node-›threshold = best_thr;
node->left = build(std: smove(L), depth + 1);
node-›right = build(std::move(R), depth + 1): return node;
int predict(const Node* n, const std:: vector<double>& x) const [
if (n->leaf) return n-›prediction;
return [n->feature] <- n-›threshold ? predict(n->left.get(), x)
: predict(n->right.get(), x);
public:
explicit DecisionTree(int max_depth = 5) : max_depth_(max_depth) (
void fit(const std:: vector‹Sample›& data) { std::vector<const Samples> rows; for (auto& s : data) rows.push_back(&s); root_ - build(std::move(rows), e);
｝
int predict(const std: vector<double>& x) const {
return predictroot_•get), x);
｝
void
demo () {
/ Toy 2D dataset: label 1 if x0 + x1 › 10, else e (tree learns the boundary). std: :vector‹Sample> data;
for (double a = 0; a ‹ 10; ++a)
for
(double b = e; b < 10; ++b)
data-push_back({{a, b}, (a + b > 10) ? 1: 0});
DecisionTree tree(6); tree.fit(data);
int correct = 0;
for (auto&s : data) correct +- (tree.predicts.features) = s.label);
std::printf("[1] CART DecisionTree: train accuracy = %d/%zu\n",
correct, data.size());
std: printf("
predict ({2,3})=%d
predict ({8,9})-%d
(expect e then 1) (n",
tree, predict({2, 3}), tree.predict({8, 9}));
} / namespace ml
2. BEHAVIOR TREE (reactive control: Sequence / Selector / Action)
Ticks return Success / Failure / Running.
Uses polymorphic nodes.
namespace bt {
enum class Status { Success, Failure, Running ):
struct Node {
virtual ~ode()
• default;
virtual Status tick() - 0;
using Nodeptr - std: :unique_ptr<Node>;
// Leaf: runs a user callable returning Status.
class Action : public Node ( std:: function<Status)> fn_:
public:
explicit Action(std::function<Status)> fn) : fn_(std::move(fn)) 0
Status tick() override (return fnt); }
｝；
// Composite: succeeds only if ALL children succeed, in order (logical AND).
class Sequence : public Node {
std:: vector<NodePtr> children;
public:

explicit Sequence(std: vector<NodePtr> c) : children_(std::move(c)) (
Status tick() override {
for (auto& c : children {
Status s - c->tick();
if (s l- Status::Success) return 5;
// fail/running short-circuits
return Status: :Success;
// Composite: succeeds if ANY child succeeds, in order (logical oR / fallback).
class Selector : public Node ( std:: vector<NodePtr> children:
public:
explicit Selector(std: vector‹NodePtr> c) : children_(std: :move(c)) (
Status tick() override (
for (auto& c: children_) (
if (sl= Status::Failure) return s:
// success/running short-circuits
return Status: :Failure;
const char* to_str (Status s) (
switch (s) {
case Status: :Success: return "Success"; case Status: :Failure: return "Failure"; default:
return "Running";
}
void
demo () (
// Robot: IF door is open -> walk through; ELSE open door then walk.
bool door_open - false;
bool walked = false;
auto action = [](std:: function<Status)> f) {
};
return std: make_unique<Action>(std: :move(f));
std:: vector<NodePtr> open_path;
open_path-push_backaction[&] {
return door_open? Status: :Success : Status::Failure;
));
open_path-push_back(action([&] { walked = true; return Status: :Success; })) :
std::vector<NodePtr> force_path;
force path.push_back(action([&] {door_open - true; return Status: :Success; })):
force_path-push_back(action[&] { walked = true; return Status::Success; }));
std::vector<NodePtr> alts;
alts-push_back(std: :make_unique‹Sequence>(std:: move(open_path))): alts.push_back(std::make_unique‹Sequence> make_unique‹Sequence> (std: : move(force_path))):
Selector root(std::move(alts));
Status s = root.tick();
to_str(s), door_open, walked);
} / namespace bt
11 3.
MINIMAX GAME DECISION TREE with alpha-beta pruning (Tic-Tac-Toe)
1/
Searches the game tree to choose the optimal move.
//
namespace game f
using Board = std: array<char, 9>;
// 'x', 'o', or • •
char winner(const Board& b) (
constexpr int línes[8][3] - {{0,1,2}, (3,4,5), {6,7,8),
(0,3,6), (2,4,7), (2,5,8),
(0,4,8), (2,4, 6));
for (auto& 1: lines)
if (b[1[011
return bialell;
return • ;
}
bool full(const Board& b) {
return std: sranges: none_of (b, ](char c) (return c"; 3);
}
// Score from X's perspective: +(10-depth) x wins,
-(10-depth) 0 wins, @ draw.
int minimaxBoard& b, bool x_to_move, int depth, int alpha, int beta) (
char w = winner(b);
if w
'X')
return 10- depth;
if (w
'0')
return depth - 10;
if (full(b))
return
0;

if (x_to_move)
int best std::numeric_limits<int>::min()
for (int i = e; 1 ‹ 9; +1) ( if (b[i] |=
•) continue;
best • std::maxbest, minimaxb, false, depth + 1, alpha, beta)): b[i] =;
alpha • std: :maxalpha, best); if (beta <= alpha) break;
// beta cutoff (prune)
return best;
} else {
best • std:: min(best, minimaxb, true, depth + 1, alpha, beta));
// alpha cutoff (prune)
return best;
for	int best • std::numeric_limits<int›::max);
(int i = 0; i < 9; ++i) {
if (b[i] |= • ') continue; b[i] - '0';
	beta = std: :min(beta, best);
if (beta <- alpha) break;
int best move(Board b, bool x_to_move) {
int best_score - x_to_move? std: :numeric_limits<int>::min()
: std::numeric_limits<int>::max):
int move = -1;
for (int i = 0; i ‹ 9; +i) { if (b[il != • ") continue; b[il = x_to_move ? 'X' : '0°;
int score = minimaxb, Ix_to move, 1,
std:: numeric_limits<int>::min(),
std:: numeric_limits<int>::max());
b[i] = • •;
if (x_to_move ? (score > best_score) : (score < best_score)) {
best_score = score; move = i;
return move:
}
void demo () {
11 x to move; X can win by taking cell 2 (top row XX_).
Board b = {%,'x',' "
int m = best_nove(b, true);
std: printf("[3] Minimaxalpha-beta) Tic-Tac-Toe: best move for X = cell %d *
"(expect 2 to win the top row) \n", m);
} II namespace game
int main() {
ml: :demo();
bt:: demo();
game: : demo ();
return e;