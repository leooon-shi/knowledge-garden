<!-- interview/decision_making_trees_cpp20.md -->

# Three Kinds of Decision-Making Trees in C++20 - Design, Performance, and Trade-offs

"Decision-making tree" spans several distinct paradigms. This answer implements and
analyzes three genuinely different kinds, each solving a different class of problem:

1. **Classification Decision Tree (CART)** - statistical learning; learns splits from
   data.
2. **Behavior Tree** - reactive control (game AI / robotics); orchestrates actions.
3. **Minimax game tree with alpha-beta pruning** - adversarial search; picks optimal
   moves.

Full runnable source: `interview/code/decision_trees.cpp` (built with
`g++ -std=c++20 -O2 -Wall-Wextra`, verified: CART 100/100 on the toy set, behavior tree
opens door + walks, minimax picks the winning cell).

_____________________________________________________________________

## 1. Classification Decision Tree (CART, Gini impurity)

**What it is.** Supervised learning: recursively partition the feature space with
axis-aligned splits `feature <= threshold`, chosen greedily to maximize impurity reduction
(Gini). Leaves store the majority class. Prediction walks root-to-leaf.

**Key C++20 features used.** std::span for cheap non-owning views over row subsets,
`std::ranges::sort`/`max_element` with projections, `std::unique_ptr` for the owning node
tree.

**Complexity.**

- **Training:** the dominant cost is, at each node, sorting each feature and scanning
  candidate thresholds. Naive (as implemented) is roughly `O(n * m * n log n)` per level
  -> about `O(m * n^2 log n)` overall for `n` samples, `m` features, because it re-sorts
  and rebuilds subsets. Production implementations pre-sort each feature once and maintain
  running impurity counts, bringing training to `O(m * n log n)`.
- **Prediction:** `O(depth)`, typically `O(log n)` for a balanced tree - very fast,
  branch-heavy pointer chasing.
- **Memory:** `O(number_of_nodes)`, up to `O(n)` for a fully grown tree.

**Performance characteristics.** Prediction is extremely cheap and cache-unfriendly
(pointer-chasing a linked node structure; each step is a data-dependent branch the CPU
mis-predicts). For latency-critical inference, real engines **flatten** the tree into a
contiguous array and often compile it to branchless code or SIMD (e.g., decision-forest
inference libraries). Training is the expensive phase and is done offline.

**Use cases.** Tabular classification/regression, feature-importance analysis,
interpretable models, and as the base learner in **random forests** and **gradient-boosted
trees** (GBoost/LightGBM) - the workhorse of tabular ML.

**Advantages.**

- Interpretable ("white box"): the path *is* the explanation.
- No feature scaling needed; handles mixed feature types.
- Fast, simple prediction.

**Disadvantages.**

- **High variance / overfitting** - a single deep tree memorizes noise; needs pruning,
  depth limits, or ensembling.
- Greedy splitting is not globally optimal.
- Unstable: small data changes -> very different tree.
- Axis-aligned splits struggle with diagonal/correlated boundaries (our demo needs depth
  to staircase-approximate `x0+x1>10`).

_____________________________________________________________________

## 2,Behavior Tree (Sequence / Selector / Action)

**What it is,** A reactive control structure from game AI and robotics. Internal
**composite** nodes orchestrate children; leaves are **actions/conditions**, Every node,
when `tick()`ed, returns `Success`, `Failure`, on `Running`:

- **Sequence** - logical AND; run children in order; stop at the first non-Success.
- **Selector (Fallback)** - logical OR: run children in order; stop at the first
  non-Failure,
- **Action**• a leaf that does work and reports status.

**Key C++29 features used.** `enum class` for the status, runtime polymorphism
(`virtual tick()`), `std::unique_ptr` ownership, `std::function` for action bodles.

**Complexity.**

- **Per tick:** `O(number_of_nodes_visited)` - worst case the whole tree, but
  short-circulting (Sequence stops on fallure, Selector on success) usually visits far
  fewer.
- **Memory:** `O(nodes)`; the tree is built once and ticked repeatedly (typically every
  frame / control cycle).

**Performance characteristics.** The cost model is per-tick, at a fixed frequency (e.g.,
60 Hz game loop, or a robot control loop). The `virtual` dispatch + `std::function`
indirection per node is fine at these scales (hundreds of nodes) but is *not* free - for
very large trees or high tick rates, the virtual calls and heap-allocated `std::function`s
cause cache misses and indirect-branch mispredicts. High-performance engines replace this
with a **flattened, data-oriented** layout (nodes in a contiguous array, indices instead
of pointers, no virtuals) and cache the `Running` frontier so a tick resumes where it left
off instead of re-walking from the root.

**Use cases.** NPC/enemy AI in games, robot task orchestration, drone mission logic,
autonomous-agent decision logic - anywhere you need **modular, composable, inspectable
reactive behavior** that designers can edit.

**Advantages.**

- **Modular and composable** - subtrees are reusable; add behavior by adding nodes, no
  giant switch.
- **Readable/authorable** - maps to how designers think ("try this, else that").
- Far more maintainable than finite-state machines as complexity grows (no `O(states^2)`
  transition explosion).
- The `Running` state naturally models long-running/async actions.

**Disadvantages.**

- Re-walking from the root each tick is wasteful without a resumption optimization.
- Pointer/virtual layout is cache-hostile at scale.
- Sharing state between nodes (a "blackboard") is ad-hoc and a common source of bugs.
- Not a *learning* system - behavior is entirely hand-authored.

_____________________________________________________________________

## 3. Minimax Game Tree with Alpha-Beta Pruning

**what it is.** Adversarial search over a game tree. `minimax` explores all reachable
positions; the maximizing player (x) picks the move maximizing the score, the minimizing
player (0) minimizes it. **Alpha-beta pruning** skips branches that cannot affect the
result (once a branch is proven worse than an already-found alternative). The `depth` term
in the score makes it prefer faster wins / slower losses.

**Key C++20 features used.** `std::array` for the fixed board (value type,
stack-allocated, cache-friendly), `std::ranges::none_of`, `constexpr` win-line table.

**Complexity.**

- **plain minimax:** `O(b^d)` - branching factor `b`, depth `d`. For Tic-Tac-Toe
  `b<=9, d<=9` (at most `9! = 362880` leaf orderings), trivially exhaustible.
- **With alpha-beta:** best case `O(b^{d/2})` (with perfect move ordering) - effectively
  **doubles the searchable depth** for the same work. Worst case (bad ordering) degrades
  back to `O(b^d)`.
- **Memory:** `O(d)` - only the recursion stack; the board is mutated in place and undone
  (make/unmake), so no per-node board copies.

**Performance characteristics.** This is a **compute-bound tree search**, unlike the other
two. The in-place make/unmake on a stack `std::array` is deliberately cache-friendly and
allocation-free - the hot path is pure recursion with no heap traffic. For larger games
(chess, Go) the raw `b^d` is astronomical, so real engines layer on: **move ordering** (to
maximize pruning), **transposition tables** (hash-cache visited positions - the same
memoization idea as DP), **iterative deepening**, and eventually replace exhaustive search
with **heuristic evaluation at a depth cutoff** or **Monte Carlo Tree Search (MCTS)**.
Alpha-beta's effectiveness is entirely gated by move ordering.

**Use cases.** Perfect-information, zero-sum games (Tic-Tac-Toe, checkers, chess,
Connect-4), and any adversarial planning where you can enumerate opponent responses.

**Advantages.**

- **provably optimal**play against an optimal opponent (for fully searched trees).
- Alpha-beta gives a large constant/exponent reduction with zero loss of correctness.
- Tiny, allocation-free memory footprint (`O(depth)`).

**Disadvantages.**

- **Exponential blow-up**- infeasible for large branching factor/depth without heuristics
  or MCTS.
- Requires a full, accurate model of the game (perfect information, known rules).
- Pruning quality depends heavily on move ordering.
- Doesn't handle chance or hidden information without extension (expectininimax, etc.).

______________________________________________________________________

## Comparative Summary

| Aspect               | CART Decision Tree                         | Behavior Tree                                 | Minimax + alpha-beta                 |
| -------------------- | ------------------------------------------ | --------------------------------------------- | ------------------------------------ |
| Paradigm	           | Statistical Learning	                      | Reactive control                              | Adversarial search                   |
| Built by             | Learned from data                          | Hand-authored                                 | Generated on the fly by search       |
| Hot operation        | Predict: `O(depth)`                        | Ticka `O(nodes visited)`                      | Search: `O(b^(d/2})` best case       |
| Build/train cost     | Expensive, offline (`O(m n 1og n)`)        | One-time construction                         | None (built implicitly)              |
| Memory               | `O(nodes)`                                 | `O(nodes)`                                    | `O(depth)` (stack only)              |
| Bound by             | Training compute; predict cache misses     | Tick frequency: virtual/pointer overhead      | Compute (exponential); move ordering |
| Optimality           | Greedy, overfits                           | Only as good as authored logic                | Provably optimal (If fully searched) |
| Main win             | Interpretable, cheap inference             | Modular, composable, designer-friendly        | Optimal play, big pruning speedup    |
| Main risk            | Overfitting/variance                       | Cache-hostile at scale; ad-hoc state          | Exponential explosion                |
| Typical fix at scale | Ensembles: flatten tree for SIMD Inference | Data-oriented flatten: cache Running frontier | Transposition tables, ordering, MCTS |

## Cross-cutting performance lessons (staff/principal level)

- **The expensive phase differs per tree.** CART front-loads cost into *training* (predict
  is cheap); behavior trees pay *every tick* forever; minimax pays *per decision* and that
  cost is exponential. Optimize the phase that dominates your workload.
- **pointer-based trees are cache-hostile.** All three, as written, chase `unique_ptr`s /
  virtuals. The universal production optimization is the same: **flatten into a contiguous
  array with index links**, drop virtual dispatch, and make traversal
  branch-predictable/SIMD-friendly. This is the single biggest real-world speedup for tree
  inference and BT ticking.
- **Memoization/caching is the lever for search trees.** Alpha-beta prunes; transposition
  tables memoize - both attack the `b^d` explosion, the same principle as the kV-cache in
  LLM inference and DP tables.
- **Allocation discipline matters.** Minimax's in-place make/unmake on a stack
  `std::array` (zero heap) is why it's fast; the CART/BT `unique ptr` allocations are fine
  offline/at construction but would need pooling if built on a hot path.
- **Interpretability vs. optimality vs. authorability is the real design axis.** CART is
  interpretable but suboptimal; minimax is optimal but opaque and expensive; behavior
  trees are authorable but not optimal or learned. Pick per the problem's actual
  requirement.

## Interview Q&A

**Q: "Decision tree" is ambiguous. Name the three distinct kinds and what class of problem
each solves.** A: A CART classification tree (statistical learning - learns axis-aligned
splits from data to partition the feature space), a behavior tree (reactive control for
game AI/robotics - composites orchestrate action/condition leaves), and a minimax game
tree with alpha-beta pruning (adversarial search -picks optimal moves against an optimal
opponent). They differ in how they're built: learned from data, hand-authored, and
generated on the fly by search, respectively. *Follow-up:* Which is a learning system and
which are not? - Only CART learns from data; behavior trees are entirely hand-authored,
and minimax searches a known game model -neither learns. *Red flag:*Treating all three
as the same thing or assuming "decision tree" always means the ML variant.

**Q: For a CART tree, why is training expensive but prediction cheap, and what's the
complexity of each?** A: Training's dominant cost is, at each node, sorting each feature
and scanning candidate `feature <= threshold` splits to maximize Gini impurity reduction -
naively about O(m•n²•log n) because it re-sorts and rebuilds subsets, though production
implementations pre-sort each feature once and keep running impurity counts to reach
o(m-n-1og n). Prediction just walks root-to-leaf, o(depth) = O(1og n) for a balanced tree.
So training is the heavy offline phase; prediction is extremely cheap. *Follow-up:* If
prediction is O(1og n), why can it still be slow in practice? - It's cache-unfriendly
pointer-chasing with data-dependent branches the CPU mispredicts; latency-critical engines
flatten the tree into a contiguous array and compile to branchless/SIMD code. *Red flag:*
Claiming training and prediction have similar cost, or ignoring the cache-hostility of
pointer-chasing.

**Q: Explain the three return states of a behavior tree node and how Sequence and Selector
use them.** A: Each node's `tick()` returns Success, Failure, or Running. A Sequence is
logical AND: it runs children in order and stops at the first non-Success. A Selector
(Fallback) is logical OR: it runs children in order and stops at the first non-Failure.
The Running state naturally models long-running/async actions, letting a tick report
"still working." *Follow-up:* Why are behavior trees more maintainable than finite-state
machines as complexity grows? - FSMS suffer O(statesz) transition explosion; BTs are
modular and composable - you add behavior by adding reusable subtrees, not wiring new
transitions. *Red flag:* Omitting the Running state, or describing BTs as just nested
if/else with no composability benefit.

**Q: What is alpha-beta pruning and how much does it help, and on what does its
effectiveness depend?** A: Alpha-beta skips branches that cannot affect the result - once
a branch is proven worse than an already-found alternative, it's cut. Plain minimax is
O(b^d); with perfect move ordering alpha-beta reaches O(b^(d/2)) best case, effectively
doubling the searchable depth for the same work, with zero loss of correctness. Its
effectiveness is entirely gated by move ordering bad ordering degrades it back toward
O(b^d), *Follow-up:* Why is minimax's memory footprint so small? - O(depth): only the
recursion stack, because the board is mutated in place and undone (make/unmake), so there
are no per-node board copies, *Red flag:* Claiming alpha-beta changes the answer, or that
it always gives O(b^(d/2)) regardless of ordering.

**Q: Debugging/scenario- a CART tree gets 100% on training data but generalizes poorly,
and the boundary is diagonal, What's happening and what do you do?** A: A single deep tree
has high variance and overfits - it memorizes noise, and greedy axis-aligned splits can
only staircase-approximate a diagonal/correlated boundary (e.g., `x0+x1>10`), needing
excessive depth, Remedies: 1imit depth or pruner and above all ensemble - random forests
or gradient-boosted trees (Boost/LightGBM) average/boost many trees to cut variance and
are the workhorse of tabular ML, "Follow-up;* Why doesn't greedy splitting find the best
tree? - It optimizes impurity reduction locally at each node, which isn't globally
optimal, and it's unstable - small data changes yield very different trees. *Red flag:*
Treating 100% training accuracy as success, or not knowing ensembling is the standard fix
for single-tree variance.

**Q: The expensive phase differs across these three trees, Where does each pay its cost
 and what's the implication?** A: CART front-loads cost into training (offline, expensive)
 while prediction is cheap; a behavior tree pays every tick, forever, at a fixed frequency
 (e.g., 60 Hz); minimax pays per decision and that cost is exponential in depth. The
 implication is to optimize the phase that dominates your actual workload - don't
 micro-optimize CART prediction 1f training dominates, and don't ignore per-tick BT
 overhead at high tick rates. *Follow-up:* What's the common per-tick optimization for
 behavior trees? - Flatten to a data-oriented contiguous layout (indices not pointers, no
 virtuals) and cache the Running frontier so a tick resumes where it left off instead of
 re-walking from the root. *Red flag:* Assuming all three have the same cost profile, or
 optimizing the wrong phase.

**Q: Design trade-off - given CART, behavior tree, and minimax, what's the real axis you
choose along, and what single optimization applies to all three?** A: The real design axis
is interpretability vs. optimality vs. authorability: CART is interpretable but
suboptimal, minimax is provably optimal but opaque and exponentially expensive, and
ehavior trees are authorable/designer-friendly but neither optimal nor learned - pick per
the problem's requirement. The universal production optimization is the same for all three
as written (they chase `unique_ptr`s/virtuals, which is cache-hostile): flatten into a
contiguous array with index links, drop virtual dispatch, and make traversal
branch-predictable/SIMD-friendly. *Follow-up:* What's the analogue of alpha-beta's pruning
for reusing computation in search trees? - Transposition tables memoize visited positions
- the same principle as the KV-cache in LLM inference and DP tables - attacking the b^d
explosion, *Red flag:* picking a paradigm by familiarity rather than the
interpretability/optimality/authorability requirement, or missing that flattening is the
shared speedup,
