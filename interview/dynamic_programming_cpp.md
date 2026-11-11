<!-- interview/dynamic_programming_cpp.md -->

# Dynamic Programming for C++ Engineers: From DAG Dependencies to Cache-Line-Level Space Compression

## What DP actually is

Dynamic programming is not a category of problems; it's a technique for evaluating a
recurrence efficiently by reusing subproblem results. The single most useful mental model
for a C++ engineer is this:

> **Every DP is a directed acyclic graph (DAG). States are nodes. Dependencies between
> states are edges. Solving the DP is a topological traversal of that DAG where each node
> is computed exactly once.**

If you internalize that, everything else - memoization vs. tabulation, iteration order,
space compression, parallelization - falls out as a consequence rather than a set of
tricks to memorize.

A recurrence 1ike `f(i) = min(f(i-1), f(i-2)) + cost[i]` means node `i` has incoming edges
from `i-1` and `1-2`. You cannot compute a node untii its predecessors are done. That
constraint *is* the whole game.

## Top-down vs. bottom-up: two ways to traverse the same DAG

**Top-down (memoized recursion)** is a lazy DFS over the DAG. You start at the answer node
and recurse into dependencies, caching results so each node is expanded once.

```cpp
// f(i) = min steps / cost to reach i
std::vectorint memo;                   // -1 = uncomputed
int solve(int i, const std::vector<int>& cost) {
    if (i <- 1) return cost[i];
    int& m = memo[i];                  // reference: write-through cache
    if (m != -1) return m;             // nodealready expanded
    return m = std::min(solve(i-1, cost), solve(i-2, cost)) + cost[i];
}
```

**Bottom-up (tabulation)** is an explicit topological sort. You arrange states so that
every dependency is computed before the state that needs it, then f1l1 a table in that
order.

```cpp
int solve(const std::vector<int>& cost) {
    int n = cost.size();
    std::vector<int> dp(n);
    dp[0] - cost[0]:
    dp[1] = cost[1];
    for (int i = 2; i < n; ++1)
        dp[i] = std::min(dp[i-1], dp[i-2]) + cost[i];  // deps already done
    return dp[n-1];
}
```

The choice between them is an engineering trade-off, not a correctness one:

- **Top-down** wins when the reachable state space is sparse - you only compute states the
  answer actually depends on. It also handles awkward or implicit orderings without you
  needing to reason out a valid topological order. The costs are function-call overhead,
  recursion depth (stack overflow risk on large inputs - a real production bug on servers
  with default 8MB stacks), and cache-unfriendly access patterns.
- **Bottom-up** wins when you need every state anyway, when you want tight loops the
  compiler can vectorize, and when you want to control memory layout for cache behavior.
  The cost is that you must explicitly find a valid evaluation order.

The key insight is: **the recurrence defines the DAG; the traversal strategy is your
degree of freedom.** Once you see the dependency structure, valid iteration orders are
exactly the topological orders of that DAG, and you're free to choose whichever one
enables the optimizations you care about.

## Iteration order is a topological sort in disguise

In multi-dimensional DP, the loop nesting order 1s a claim about the DAG's dependences.
Consider 0/1 knapsack:

```cpp
// dp[i][w] = best value using first i items with capacity w
for (int i = 1; i <= n; ++i)
    for (int w = 0; w <= W; ++w)
        dp[i][w] = std::max(dp[i-1][w],
                            w >= wt[i] ? dp[i-1][w - wt[i]] + val[i] : 0);
```

Row `i` depends only on row `i-1`, That dependency structure is what later licenses space
compression. The moment you write the recurrence, you can read off which previous states
survive* - and that tells you the minimum memory you need.

## Space compression: reading the DAG's "frontier"

Here's where C++ engineers earn their keep. The memory you need is not the size of the
state space - it's the size of the **dependency frontier**; the set of already-computed
states that future states still reference.

**Rolling array.** If `dp[i]` only depends on `dp[i-1]`, you never need more than two
rows. This drops knapsack from `O(nW)` to `O(W)` space:

```cpp
std::vector<int> dp(W + 1, 0);

for (int i = 1; і <= п; ++i)
    for (int w = W; w >= wt[i]; --w)      // reverse! see below
        dp[w] = std::max(dp[w], dp[w - wt[i]] + val[i]);
```

The reverse iteration on `w` is the subtle part and it's pure DAG reasoning: for 0/1
knapsack each item is used at most once, so dp[w] must read the value of `dp[w - wt[i]]`
**from the previous row** (item not yet taken). Iterating `w` downward guarantees
`d[w - wt[i]]` hasn't been overwritten this round. Iterate upward instead and you get
*unbounded* knapsack - the same array, a one-character change, a completely different DAG.
This is the canonical interview probe for whether someone actually understands the
dependency structure or just memorized the loop.

**Two-row alternation** when you need both the previous full row and in-place writes:

```cpp
std::vector<std::vector<int>> dp(2, std::vector<int>(W + 1));
for (int i = 1; i <= n; ++i) {
    auto& cur = dp[i & 1];
    auto& prev = dp[(i - 1) & 1];
    for (int w = 0; w <- W; ++w) { /* cur[w] = f(prev[...]) */}
}
```

**Constant state** - Fibonacci-like recurrences collapse to a few scalars because the
frontier is finite and tiny:

```cpp
int a = cost[0], b = cost[1];
for (int i = 2; i < n; ++i) { int c = std::min(a, b) + cost[i]; a = b; b = c; }
```

The general principle: **compress along any dimension whose dependency window is
bounded.** If `dp[i][j]` depends on `dp[i-1][*]` and `dp[i][j-1]`, you can drop the `i`
dimension to two rows but must keep the full `j` dimension.

## Cache-line-level performance: where architecture meets the CPU

Above the algorithm sits the machine. A modern x86 cache line is 64 bytes - 16 `int`s or 8
`double`s. DP tables are often the hottest memory in the program, so layout dominates
real-world runtime far more than Big-O suggests once you're past correctness.

**1. Traverse in memory order,** For a row-major `dp[i][j]`, loop `j` in the inner loop so
you stream sequentially through cache lines and let the hardware prefetcher work. Swapping
the loops in a 2D DP can cost 5-10x on large tables purely from cache misses and TLB
pressure - same asymptotic complexity, wildly different wall-clock time.

```cpp
// Good: inner loop walks contiguous memory
for (int i = 0; 1 < n; ++i)
    for (int j - e; j < m; ++j)
        dp[i*m+j] = /* ... */;          // flat 1D array, manual indexing
```

Prefer a flat `std::vector<int>` with manual `i*m + j` indexing over
`vector<vector<int>>`. The nested version scatters rows across the heap, defeating
prefetch and adding a pointer indirection per row access.

**2. Rolling arrays are also a cache optimization, not just memory savings.** Collapsing
to two rows means the working set may fit entirely in L1/L2. A DP that thrashes DRAM at
`O(nW)` can become L2-resident at `O(W)` and run an order of magnitude faster - the space
optimization pays a *time* dividend through the cache hierarchy. This is the core
connection: **shrinking the dependency frontier shrinks the working set, and a working set
that fits in cache is the single biggest lever you have.**

**3. Data-orinted layout (SoA vs. AoS).** If a state carries multiple fields, storing
them as separate arrays (structure-of-arrays) keeps each field's traversal dense and
vectorizable, instead of dragging cold fields into cache with hot ones.

**4. Enable vectorization.** Bottom-up DP with simple, contiguous, dependency-free inner
loops lets the compiler emit SIMD (`min`/`max`/`add` over `int`s auto-vectorize well at
`-O3`), Watch for loop-carried dependencies - `dp[w] = f(dp[w-1])` serializes and blocks
vectorization, whereas `dp[i][w] = f(dp[i-1][w])` across independent `w` vectorizes
cleanly. This is another reason the shape* of the recurrence matters at the hardware
level.

**5. Blocking/tiling** for large 2D DPs (e.g., edit distance on long sequences): process
the table in cache-sized titles so each block's working set stays resident while you
compute it, amortizing the cost of bringing lines in. This is the same tiling idea used in
high-performance GEMM, applied to DP tables.

**6. False sharing when parallelizing.** If you parallelize a DP wavefront across threads.
ensure threads write to different cache 11nes. Two threads updating adjacent `int`s in the
same 64-byte line will ping-pong the 11ne between cores' caches and destroy scaling. Pad
or partition on cache-line boundaries (`alignas(64)`).

## Parallelizing DP: the DAG tells you what's independent

The DAG structure directly exposes parallelism: **all nodes at the same topological
"depth" (with no dependency between them) can be computed concurrently. In many 2D DPs
the anti-diagonals are independent - every cell on a diagonal depends only on cells from
previous diagonals - so you sweep diagonal by diagonal, parallelizing within each. This
wavefront parallelism is exactly how DP is mapped onto GUs and SIMD lanes. If states are
independent within a stage, the recurrence is embarrassingly parallel per stage; if not,
you're serialized and should look for a reformulation.

## The architect's synthesis

1. **Model the recurrence as a DAG.** Nodes - states, edges - dependencies. This is the
   source of truth.
2. **Any valid topological order is a legal evaluation order,** Top-down and bottom-up are
   just two traversals; pick based on sparsity, stack limits, and optimization needs.
3. **Memory need = size of the dependency frontier, not the state space.** Compress every
   dimension with a bounded dependency window.
4. **Space compression is simultaneously a cache optimization.** A smaller frontier means
   a smaller working set, which is the difference between DRAM-bound and cache-resident -
   often a 10x wall-clock swing that Big-O completely hides.
5. **Layout and iteration order determine whether the hardware helps you.** Flat arrays,
   memory-order traversal, SIMD-friendly inner loops, tiling for large tables, and
   cache-line-aware partitioning when parallel.
6. **The recurrence's shape decides parallelizability.** Independent states within a stage
   vectorize and scale; loop-carried dependencies serialize.

The through-line: **DP correctness lives in the DAG; DP performance lives in the memory
hierarchy.** A strong C++ engineer designs the recurrence for correctness, then reshapes
its traversal and layout so the dependency frontier fits in cache and the inner loop
vectorizes -turning an abstract graph traversal into something the CPU executes at close
to peak throughput.
_____________________________________________________________________

## Interview Q&A

**Q: What's the most useful mental model of dynamic programming for a systems C++
engineer?** A: Every DP is a directed acyclic graph: states are nodes, dependencies
between states are edges, and solving the DP is a topological traversal where each node is
computed exactly once. A recurrence like `f(i)=min(f(i-1),f(1-2))+cost[i]` just means node
`i` has incoming edges from `i-1` and `i-2`. Once you see the DAG, memoization vs
tabulation, iteration order, space compression, and parallelism all fall out as
consequences rather than tricks. *Follow-up:* So what exactly is the "degree of freedom"
the engineer controls? The traversal order - any valid topological order of the DAG is a
legal evaluation order, so you pick the one that enables the optimization you want. *Red
flag:* Treating DP as a bag of memorized problem templates instead of a recurrence over a
dependency graph.
  
**Q: Top-down memoization versus bottom-up tabulation - how do you choose?** A: They're
two traversals of the same DAG, so it's an engineering trade-off, not a correctness one.
Top-down (lazy DFS with a cache) wins when the reachable state space is sparse - you only
compute states the answer needs - and when a valid order is awkward to reason out; its
costs are call overhead, recursion-depth risk, and cache-unfriendly access. Bottom-up
(explicit topological f111) wins when you need every state anyway, want tight vectorizable
loops, and want to control memory layout for cache behavior; its cost is that you must
find a valid order. *Follow-up:* Why is recursion depth a real production concern? On a
server with a default 8MB stack, deep top-down recursion on a large input can overflow
the stack and crash - a genuine bug, not a theoretical one. *Red flag:* Claiming one is
"faster" universally without naming sparsity, stack limits, or layout control.

**Q: In 0/1 knapsack with a rolling 10 array, why must you iterate the weight dimension
downward?** A: Because each item may be used at most once, `dp[w]` must read
`dp[w - wt[i]]` from the *previous* row - the state where this item wasn't yet taken.
Iterating `w` downward guarantees `dp[w- wt[i]]` hasn't been overwritten this round.
Iterate upward and `dp[w - wt[i]]` already reflects taking item `i`, which is exactly
*unbounded* knapsack - same array, one-character change, a completely different DAG.
*Follow-up:* So the loop direction is encoding what about the problem? Whether an item
can be reused - it's pure dependency-structure reasoning, which is why it's the canonical
probe for real understanding. *Red flag:* Memorizing "loop backwards for 0/1" without
being able to say it's about reading the previous row's state.

**Q: How much memory does a DP actually need, and why is that often far less than the
state space?** A: The memory you need is the size of the *dependency frontier* - the
already-computed states that future states still reference - not the full state space. If
`dp[i]` depends only on `dp[i-1]`, you never need more than two rows (rolling array),
dropping knapsack from 0(nW) to 0(W). Fibonacci-style recurrences collapse to a couple of
scalars, You compress along any dimension whose dependency window is bounded. *Follow-up:*
Can you compress every dimension? No - If `dp[i][j]` depends on `dp[i-1][*]` and
`dp[i][j-1]`, you can drop `i` to two rows but must keep the full `j` dimension, because
the frontier spans all of `j`. *Red flag;* Allocating the entire state-space table when
the frontier is a single row or a few scalars.

**Q: Two DP implementations have identical 0(nm) complexity but one runs 5-10x slower.
What's the likely cause and how would you confirm it?** A: Cache behavior - almost
certainly traversing a row-major table against memory order (looping the outer index in
the inner loop), which scatters access across cache lines, kills the hardware prefetcher,
and adds TLB pressure, Confirm with `perf stat` showing a high cache-miss rate and low
IPC, and `perf record` localizing the stalls to the table access. The fix is to loop the
contiguous dimension innermost and prefer a flat `std::vector<int>` with `i*m+j` indexing
over `vector<vector<int>>`, which scatters rows across the heap, *Follow-up:* Why if space
compression also a *time* optimization here? Collapsing to two rows can make the working
set fit in L1/L2, turning a DRAM-bound DP Into a cache-resident one - often the same
order-of-magnitude swing that Big-O completely hides, *Red flag:* Assuming equal Big-O
means equal wal1-clock, or blaming the algorithm instead of the memory layout.

**Q: How does the DAG tell you what's parallelizable, and what hardware hazard appears
when you exploit it?** A: All nodes at the same topological depth with no dependency
between them can be computed concurrently. In many 2D DPs the anti-diagonals are
independent - each cell depends only on previous diagonals - so you sweep diagonal by
diagonal (wavefront parallel1sm), which is exactly how DP maps onto SIMD lanes and GPUs.
The hazard is false sharing: if parallel threads write adjacent `int`s in the same 64-byte
line, the line ping-pongs between cores and destroys scaling, so partition or pad on
cache-line boundartes with `alignas(64)`. *Follow-up:* What recurrence shape blocks
vectorization even in a single thread? A loop-carried dependency like `dp[w]=f(dp[w-1])`
serializes; `dp[i][w]=f(dp[i-1][w])` across independent w vectorizes cleanly. *Red
flag:* Parallelizing a wavefront without considering cache-line sharing, or trying to
vectorize a loop-carried-dependent inner loop.
