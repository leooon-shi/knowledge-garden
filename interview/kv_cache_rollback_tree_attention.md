<!-- interview/kv_cache_rollback_tree_attention.md -->

# KV-Cache Rollback and Tree Attention

## Background: the KV cache and why these mechanisms exist

In a transformer decoder, generating each token requires attending over all previous
tokens. Recomputing the keys (K) and values (V) for every prior token at every step is
O(n^2) wasted work, so inference engines maintain a **KV cache**: for each layer and each
attention head, the and vectors of every token processed so far are stored so a new
token only computes its own Q, K, V and attends against the cached K/V. This turns
per-step cost from "reprocess the whole prefix" into "process one token." The KV cache is
the single largest piece of state (and memory consumer) in autoregressive inference.

Both **KV-cache rollback** and *tree attention** are techniques for exploring *multiple
candidate continuations* efficiently without paying to rebuild that cache from scratch.
They are central to speculative decoding, bean search, backtracking samplers, and
agentic/branching workloads.

## 1. KV-Cache Rollback

**Concept.** Rollback means **truncating the KV cache back to an earlier position** so you
can discard tokens you have tentatively generated and continue from an earlier point -
without recomputing the prefix you keep.

Because the KV cache is append-only per position (token at position `i` contributes K/V
entries at index `i`), "undoing" the last `k` tokens is conceptually just **moving the
logical sequence-length pointer back by `k`**: `seq_len -= k`. The K/V memory for those
positions can be overwritten by whatever comes next. Everything up to the rollback point
stays valid and reusable.

**Where it's used:**

- **Speculative decoding.** A small draft model proposes several tokens; the large model
  verifies them in one batched forward pass. Accepted tokens are kept; on the first
  rejection, you **roll back** the KV cache to the last accepted position and resume.
  Rollback is what makes speculative decoding cheap - you don't rebuild the cache for the
  rejected tail.
- **Backtracking / constrained decoding.** If a sampled token violates a grammar/JSON
  schema or a stop condition, roll back and resample from the last good state.
- **Beam search and agentic retry.** Abandon a branch, return to a checkpoint.

**Practice - how engines implement it:**

- **Contiguous cache:** trivial - reset the length counter; the tail slots are logically
  free. But contiguous per-sequence caches waste memory and are inflexible for branching.
- **Paged KV cache (vLLM-style PagedAttention):** the cache is split into fixed-size
  **blocks** (pages) referenced by a block table, like virtual memory. Rollback =
  **free/trim the trailing blocks** from the block table and adjust the length within the
  last partial block. Cheap and non-fragmenting.
- **Care items:** rolling back must also reset any position-dependent state (RoPE position
  indices, attention masks, sampler RNG state if you want determinism). Partial-block
  rollback needs to zero or mask the reused slots so stale /V from discarded tokens
  cannot leak into attention.

## 2. Tree Attention

**Concept.** Instead of a single linear sequence, you maintain a **tree of tokens**
sharing a common prefix, and attend over the tree in **one batched forward pass** rather
than one pass per branch. Each node attends to its **ancestors along the path to the
root**, not to tokens in sibling branches. The shared prefix's K/V is computed and stored
**once** and reused by every branch.

Two ingredients:

**(a) A tree/branch attention mask.** Normally causal attention uses a lower-triangular
mask (token `i` sees tokens `0..i`). For a tree flattened into a single sequence, replace
the triangular mask with a **tree mask**: token `j` may attend to token `k` **iff `k` is
an ancestor of `j` in the tree** (including itself). Sibling branches are masked out from
each other, This packs many candidate branches into one sequence and one attention call
while preserving correct per-branch causality.

```
        compt (shared)
        /           \
     A                 X
    / \               / \
   B   C             Y   Z
```

Flattened as `[prompt..., A, B, C, X, Y, Z]`, node `B` attends to `{prompt, A, B}` only;
`Y` attends to `{prompt, X, Y}` only. The prompt K/V is shared by all six.

**(b) Position encoding per path.** Each token's positional index (for RoPE/AL181) is its
**depth along its own path**, not its offset in the flattened buffer - so `A` and `X` are
both at depth `len(prompt)`, `B` and `Y` both at depth `len(prompt)+1`, etc.

**Where it's used;**

- **Speculative decoding with multiple drafts (the big one):** Medusa, EAGLE, SpecInfer,
  and "tree-of-drafts" methods propose a *tree* of candidate continuations, then verify
  the whole tree in **one** large-model forward pass using tree attention. The verifler
  walks the tree, accepts the longest matching path, and rolls back the rest. This
  multiplies tokens-per-forward-pass and is why modern speculative decoding uses trees,
  not single chains.
- **Beam search / parallel sampling** sharing a common prompt.
- **Prefix sharing across requests** (generalization: many requests sharing a system
  prompt reuse one prefix cache - RadixAttention in SGLang) .

## 3. How rollback and tree attention combine

Complementary and usually used together in speculative decoding:

1. Build a **draft tree** of candidate tokens.
2. Run **one tree-attention** forward pass over the flattened tree to score all branches.
3. Accept the longest verified path.
4. **Roll back** the KV cache to drop the K/V of all non-accepted branches, keeping the
   accepted path.
5. Repeat.

Tree attention lets you *evaluate* many futures in one pass; rollback lets you *cheaply
discard* the ones you didn't take. Together they turn branching exploration from "N
separate sequences each rebuilding cache" into "one shared-prefix pass + a
pointer/blocktable trim."

## 4. Serving context: how big the KV cache is, and what shrinks it

**Size per token** = 2 (K and V) x layers × KV heads × head dim x bytes per element.
Example, Llama-3-8B (32 layers, 8 KV heads, head_dim 128, FP16): 2 x 32 x 8 x 128 x 2 B =
**128 KiB/token** - a 32k-token context is 4 GiB for one sequence. The KV cache, not
weights, usually limits batch size.

- **MQA/GA:** queries keep many heads but share fewer K/V heads (multi-query = 1 KV
  head; grouped-query groups, e.g. 32Q heads: 8 KV heads in Llama-3) -> 4-32x smaller
  KV cache with small quality loss.
- **MLA (multi-head latent attention, DeepSeek-V2/V3):** caches a compressed low-rank
  latent per token and reconstructs K/V on the fly; even smaller cache than GOA, with RoPE
  handled by a separate decoupled component.
- **Quantized KV cache:** FP8 (vLLM `--kv-cache-dtype fp8`), INT8/INT4 with per-channel or
  per-token scales; halves or quarters memory, small accuracy cost.
- **Sliding-window / local attention** layers (Mistral, Genma interleaved local/global)
  cap the cache for those layers; **attention sinks** (StreamingLLM) keep a few initial
  tokens plus a window for streaming.
- **Offloading / tiering:** move cold KV blocks to CPU memory or SSD (LMCache, vLLM CPU
  offload); **prefix caching** reuses KV across requests sharing a prompt prefix (vLLM
  automatic prefix caching, SLang RadixAttention) - the serving-side basis of provider
  prompt caching (see `llm_agent_protocols_mcp_a2a.md`).

**Scheduling:**

- **Continuous (in-flight) batching** (Orca, vLLM): the batch is re-formed every decode
  step - finished sequences leave and waiting ones join - instead of waiting for the whole
  static batch to finish. Large throughput gains; requires paged KV so sequences of
  different lengths coexist.
- **prefill vs decode:** pref1ll (processing the pronpt) is conpute-bound and parallel;
  decode (one token per step) is memory-bandwidth-bound. **Chunked prefill** splits long
  prompts into chunks mixed into decode batches so a long prompt doesn't stall everyone's
  token latency. **Disaggregated prefill/decode** (Distserve, Mooncake, NVIDIA Dynamo,
  vLLH's disaggregated mode) runs them on different GPU pools and ships KV between them.
- **Metrics:** TTFT (time to first token - prefill + queueing), TPOT/ITL (time per output
  token - decode), throughput (tokens/S/GPU); SLOs usually on p99 TTFT and TPOT.
- **Preemption:** when KV memory runs out, the scheduler preempts sequences (swap blocks
  to CPU or recompute later) - another reason rollback/free must be cheap.

**Speculative decoding speedup math** (Leviathan et al. 2023): with per-token acceptance
rate a (probability the target model accepts a draft token) and γ draft tokens per step,
the expected tokens produced per target-model forward pass is
`E = (1 - a^(γ+1)) / (1 - a)`. E.g. a = 6.8, γ = 4: E = (1 - 6.8⁵)/0.2 ≈ 3.36 tokens per
target pass. Real speedup = E divided by the relative cost of a step (one target pass + γ
draft passes, with draft cost ratio c): `speedup ≈ E / (γ.c + 1)`. With c = 0.05: 3.36 /
1.2 ≈ **2.8x**. Speculation pays when a is high (predictable text, code, structured
output) and c low, and it helps latency more than throughput: at large batch sizes the GPU
is already compute-saturated and the extra verification work cuts into throughput.
Rejection sampling keeps the output distribution **identical** to the target model's -
speculative decoding is lossless.

# 5. Staff / Principal-level considerations

- **Memory layout dominates.** Naive tree attention with a dense per-token mask is
  O(tree_size^2) in mask memory and attention compute. Real Implementations exploit the
  shared prefix (compute prefix K/Vonce) and use sparse/structured masks or custom
  kernels (FlashAttention varlants taking a tree mask / custom bias), Paged KV caches with
  copy-on-write blocks let branches **share the prefix's physical blocks** and only
  allocate new blocks where they diverge - the branching analogue of the COW ideas from
  the previous questions,

- **Rollback correctness is about *all* the state, not just length.** RoPE positions.
  attention masks, block tables, sampler RNG, and any incremental normalization state must
  roll back consistently. A common bug is trimming the KV length but leaving a stale
  position index or not masking a reused partial block, causing subtle attention leakage.

- **Tree shape is a tuning problem.** Wider/deeper trees verify more candidates per pass
  (higher acceptance, fewer large-model calls) but cost more attention compute and memory
  per pass. The optimal tree (branching factor, depth) depends on draft-model accuracy and
  hardware; EAGLE/Medusa tune this. A throughput-vs-latency-vs-memory tradeoff, decided
  empirically.

- **Batching interacts with both.** Serving many concurrent requests, each with its own
  tree and rollbacks, stresses the KV allocator. Paged/radix caches with prefix sharing
  (SLang RadixAttention, vLLM) make this tractable at scale - the same
  watermark/reference-counting reclamation problem as MCC versions: a physical block is
  freed only when no branch/request references it.

- **Kernel support is the gating factor.** Tree attention needs an attention kernel that
  accepts an arbitrary (tree) mask or attention bias efficiently; off-the-shelf causal
  FlashAttention doesn't. This is why tree-based speculative decoding required custom
  kernels to actually be faster in practice.

## Interview Q&A

**Q: What is the KV cache and why does it exist?** A: In a transformer decoder, generating
each token requires attending over all previous tokens; recomputing and for every
prior token at each step is 0(n*) wasted work. The KV cache stores, per layer and head,
the Kand Vvectors of every token processed so far, so a new token only computes its own
Q/K/V and attends against the cache. That turns per-step cost from "reprocess the whole
prefix" into "process one token." It's the largest piece of state and usually the memory
bottleneck in autoregressive inference. *Follow-up:* Why isn't Q cached too? Each step
only needs the current* token's Q: past queries are never reused, whereas past K/V are
attended to repeatedly. *Red flag:* Thinking the cache is an optimization for weights
rather than for avoiding recomputation of past K/V.

**Q: How does KV-cache rollback work, and why is it cheap?** A: Because the cache is
append-only per position (token at index `i` contributes /Vat index `i`), undoing the
last `k` tokens is conceptually just moving the logical length pointer back:
`seq_len -= k`, and the freed slots get overwritten by whatever comes next. Everything
before the rollback point stays valid and reusable, so you never rebuild the prefix. With
a paged cache you free/trim the trailing blocks from the block table and adjust the length
in the last partial block. *Follow-up:* What's the classic rollback bug? Trimming the
length but leaving stale position indices or an unmasked reused partial block, so stale
K/V leaks into attention - you must roll back *all* state (RoPE positions, masks, sampler
RNG), not just length. *Red flag:* Treating rollback as freeing length only, ignoring
position/mask/RNG state.

**Q: Where is rollback essential, and why does it make speculative decoding cheap?** A: In
speculative decoding a small draft model proposes several tokens and the large model
verifies them in one batched pass; accepted tokens are kept, and on the first rejection
you roll back the cache to the last accepted position and resume. Rollback is what makes
this cheap - you discard the rejected tail without rebuilding the prefix K/V. It's also
used for grammar/JSON backtracking and beam-search/agentic retry to a checkpoint.
*Follow-up:* Is speculative decoding lossy? No - rejection sampling keeps the output
distribution identical to the target model's, so it's lossless. *Red flag:* Claiming
speculation changes or approximates the model's output distribution.

**Q: Explain tree attention. How does the mask differ from standard causal attention?** A:
You maintain a tree of candidate tokens sharing a common prefix and attend over the whole
Flattened tree in one pass. Normal causal attention uses a lower-triangular mask (token
`i` sees `0..i`); tree attention replaces it with a tree mask where token `j` attends to
token `k` iff `k` is an ancestor of jon its root path (including itself), masking
sibling branches from each other. The shared prefix's K/Vis computed once and reused by
every branch. *Follow-up:* What must change about positions? Each token's positional index
(for RoPE/ALiBi) is its depth along its own path, not its offset in the flattened buffer -
so two siblings at the same depth share a position value. *Red flag:* Letting sibling
branches attend to each other, or using flattened buffer offset as the position.

**Q: How do rollback and tree attention combine in tree-based speculative decoding?** A:
Build a draft tree of candidates, run one tree-attention forward pass to score al1
branches, accept the longest verified path, then roll back the KV cache to drop the K/V of
all non-accepted branches and keep the accepted path; repeat. Tree attention lets you
*evaluate* many futures in one pass; rollback lets you *cheaply discard* the ones you
didn't take. Together they turn "N sequences each rebuilding cache" into "one
shared-prefix pass plus a block-table trim." *Follow-up:* Name systems that do this.
Medusa, EAGLE, SpecInfer and "tree-of-drafts" methods. *Red flag:* Describing single-chain
speculation when the question is about trees.

**Q: Estimate the KV-cache memory and name the main techniques to shrink it.** A: Per
token it's 2 (K and V) × layers × KV heads x head_dim x bytes. For Llama-3-8B (32 layers,
8 KV heads, head_dim 128, FP16): 2x32×8×128×2 - 128 KiB/token, so a 32k context is ~4 GiB
for one sequence - the cache, not weights, usually caps batch size. Shrink it with GQA/MQA
(fewer KV heads, 4-32x smaller), MLA (cached low-rank latent, DeepSeek), quantized KV
(FP8/INT8/INT4), sliding-window/local attention with attention sinks, and
offloading/prefix caching. *Follow-up:* What is GOA concretely? Many query heads share
fewer K/V heads (e.g., 32 Q 8 KV in Llama-3), trading a little quality for a much
smaller cache; MQA is the extreme with 1 KV head. *Red flag:* Forgetting the factor of 2
for K and V, or thinking weights dominate inference memory.

**Q: Why do tree-based methods need custom kernels, and what's the memory-layout
pitfal1?** A: Off-the-shelf causal Flashattention assumes a triangular mask; tree
attention needs a kernel that accepts an arbitrary tree mask or attention bias
efficiently, which is why tree-based speculative decoding required custom kernels to
actually be faster, The naive pitfall is a dense per-token tree mask, which is
O(tree_size) in mask memory and compute; real implementations compute the shared prefix
once and use sparse/structured masks, with paged cow blocks so branches share the prefix's
physical blocks and only allocate where they diverge, *Follow-up:* How is freeing a shared
block decided? Reference counting - a physical block is freed only when no branch or
request references it, the same watermark/reclamation problem as MCC versions, *Red
flag:* Assuming standard FlashAttention handles tree masks, or using a dense mask at
scale.

**Q: Give the speculative-decoding speedup intuition with the acceptance-rate formula,**
A: With per-token acceptance rate a andy draft tokens per step, expected tokens per
target pass is E = (1 - a^(γ+1))/(1 - a); e.g. a=0.8, γ=4 gives ≈ 3,36. Real speedup ≈
E/(γ.c+ 1) where c is the draft-to-target cost ratio - with c=0.05, 2.8x. Speculation
pays when a is high (predictable text, code, structured output) and is low, and it helps
latency more than throughput: at large batch sizes the gipu is already compute-saturated
and the extra verification cuts into throughput. *Follow-up:* So when does speculation
*hurt*? Under high-throughput, compute-saturated serving, where the verification overhead
isn't amortized by idle compute. *Red flag:* Claiming speculation always improves
throughput regardless of batch size or acceptance rate.

## Synthesis

The KV cache stores per-token /V so decoding is incremental instead of O(n^2).
**Rollback** exploits the append-per-position structure to undo* tentative tokens cheaply
- trim the logical length / free trailing paged blocks - enabling speculative decoding,
backtracking, and beam search without rebuilding the prefix. **Tree attention** evaluates
many candidate continuations in a single forward pass by flattening a prefix-sharing tree
and replacing the causal mask with a tree mask (attend only to ancestors) plus per-path
positions, so the shared prefix's K/V is computed once. Combined, they power modern
tree-based speculative decoding: tree attention scores many futures per pass, rollback
discards the unaccepted ones. The hard parts at scale are memory layout (paged/COW block
sharing), rolling back *all* position/mask/NG state consistently, tuning tree shape, and
having attention kernels that accept a tree mask.
