<!-- interview/multi_agent_avoid_unbounded_discussion.md -->

# Preventing Unbounded Discussion in Multi-Agent Systems

## Why unbounded discussion happens (diagnose first)

Multiple agents conversing can fail to terminate for specific reasons; the right controls
follow from the cause:

- **No termination criterion** - "done" was never defined, so agents keep elaborating.
- **No decision authority** - peers debate with no one empowered to conclude (a meeting
  with no chair).
- **politeness/agreement 100ps** - LLMs affirm-and-add ("great point, and also..."),
  expanding rather than converging.
- **Disagreement deadlock** - two agents restate positions forever (a **livelock** -
  active, no progress).
- **Circular delegation** - A asks B, B asks A; cycles in the agent graph.
- **No shared progress state** - agents can't tel1 they're repeating themselves.

Fundamentally the same **liveness/termination problem** as loops and distributed
protocols; the fixes borrow from both.

## 1. Hard limits (non-negotiable backstops)

- **Max turns / rounds** - cap total messages/rounds; on hit, force a decision or
  escalate. Simplest, most important safeguard.
- **Max wall-clock time and token/cost budget** - terminate on exceed (each message is an
  LLM call - chatter is expensive).
- **Per-agent invocation caps** - limit how often any one agent speaks; prevents
  domination.
- **Recursion/depth limits on delegation** - cap sub-agent spawning depth.

These are the multi-agent version of loop-iteration limits: a dumb but relfable circuit
breaker guaranteeing termination even if all else fails.

## 2. Explicit termination conditions (define "done")

- **Checkable success criteria up front** - a produced artifact, passing test, satisfied
  schema; agents stop when met, checked by a **verifier**, not the debaters.
- **Dedicated judge/verifier agent or function** evaluates goal completion and stops
  discussion - separating *doing* from *deciding-it's-done*.
- **Explicit done-signaling** - agents emit a termination token; end when all relevant
  agents signal done (quorum/consensus), or when no agent has anything new.

## 3. Structured orchestration topology (design the interaction)

Biggest lever: **don't use unstructured all-to-all chat.** Choose abounding topology:

- **Orchestrator / supervisor (recommended default):** a coordinator decomposes the task,
  assigns subtasks, collects results, and **decides when to stop** - the "chair" with
  authority. Workers report to it, not each other - no peer debate loops. Dominant robust
  pattern (orchestrator-worker).
- **Sequential pipeline (DAG):** agents run A-›B-C, each transforming and passing
  forward. No back-and-forth; terminates by construction.
- **Hierarchical decomposition:** tree of supervisors/sub-agents; sub-agents with fresh
  context return a compact result and *exit*. Bounded by tree depth.
- **Round-robin with fixed agenda:** each agent speaks once per round on a defined item;
  bounded rounds.
- Avoid **free-form group chat** except for short, explicitly-bounded brainstorming - the
  topology most prone to non-termination.

## 4. Decision/consensus mechanisms (break deadlocks)

- **Designated decider / tie-breaker** - one agent (or the orchestrator) has final
  authority; after N rounds it decides. Removes deadlock.
- **Voting / quorum** - odd number of agents, majority concludes.
- **Escalation to human**-after bounded attempts, hand off (human-in-the-loop) rather
  than loop.
- **"Disagree and conmit"**- after presenting a position once, agents defer to the
  decider; counters the restate-forever livelock.

## 5. Progress tracking & repetition detection

- **Shared task state / blackboard** - single source of truth for decided vs. open items,
  so settled points aren't re-litigated and progress is visible.
- **Repetition/stagnation detection**- if recent messages stop changing materially
  (semantic similarity above a threshold, or no task-state change for k rounds), force
  termination - the 11velock-detection analog,
- **Convergence metric** - track "distance to done"; if not decreasing over rounds, stop
  and escalate.

## 6. Prompt-level discipline (reduce tendency to expand)

- **Instruct brevity and decisiveness** - concise, no restating agreement, move toward
  conclusion; forbid "great point, additionally...".
- **Role clarity** - each agent a narrow responsibility; contributes its part and stops.
- **Structured output** - emit decisions/artifacts in a schema (not open prose), naturally
  bounding contributions.

## Staff / Principal-level framing

- **Distributed-systems termination problem - Layered defenses.** Never rely on agents
  "knowing when to stop." Combine a **guaranteed hard backstop** (max turns/budget -
  always terminates), **structural prevention** (orchestrator topology so debate can't
  form), and **graceful convergence** (done-criteria, decider, progress tracking).
  Backstop guarantees safety; structure makes hitting it rare.
- **Topology is the highest-leverage choice.** Unbounded discussion is mostly a symptom of
  unstructured all-to-all chat; orchestrator-worker or pipeline eliminates the class by
  construction - more effective than tuning free-form agents to converge. Design the
  graph; don't let it emerge.
- **Separate "decider" authority from "doer" roles** (the meeting-chair pattern) - endless
  debate is usually a missing-authority problem, not a reasoning problem.
- **Economics force the issue** - every message is an LLM call; unbounded discussion is
  unbounded cost/latency, so token/turn budgets are hard business constraints, not just
  correctness safeguards.
- **Observability is essential** - 10g the conversation graph, per-agent turn counts,
  convergence metrics; treat it like monitoring a control loop to see non-termination and
  tune limits.
- **Maps to the earlier concurrency answers** - non-termination = livelock/infinite loop;
  fixes (bounded iteration, tie-break to resolve deadlock, progress detection, an
  authority to serialize decisions) are the multi-agent versions of the same principles.

## Interview Q&A

**Q: Why do multi-agent systems fail to terminate, and why should you diagnose the cause
first?** A: Because the right control follows from the specific cause: no termination
criterion ("done" never defined), no decision authority (peers debate with no chair),
politeness/agreement loops ("great point, and also..."), disagreement deadlock (a livelock
- active but no progress), circular delegation (A asks B asks A), or no shared progress
state (agents can't tell they're repeating). Fundamentally it's the same
liveness/termination problem as loops and distributed protocols, so the fixes borrow from
both. *Follow-up:* Which cause does an orchestrator topology directly address? Missing
decision authority - it supplies a chair empowered to conclude. *Red flag:* Jumping
straight to "add a turn limit" without diagnosing why discussion diverges.

**Q: What are the non-negotiable hard limits, and why are they described as "dumb but
reliable"?** A: Max turns/rounds, max wall-clock time, token/cost budget, per-agent
invocation caps, and recursion/depth limits on delegation. They're dumb because they don't
understand the task, but reliable because they *guarantee* termination even if every
smarter mechanism fails - the multi-agent version of a loop-iteration limit or a circuit
breaker. *Follow-up:* What should happen when a hard limit is hit? Force a decision or
escalate (to a decider or a human), not silently drop the task. *Red fLag:* Relying on
hard limits alone and letting agents routinely slam into them instead of converging
gracefully.

**Q: How do you define "done" so the debaters don't decide it themselves?** A: With
checkable success criteria set up front - a produced artifact, a passing test, a satisfied
schema - evaluated by a dedicated judge/verifier agent or function, not by the agents
doing the arguing. This separates *doing* from *deciding-it's-done*. You can also use
explicit done-signaling where agents emit a termination token and you stop on
quorum/consensus or when no agent has anything new. *Follow-up:* Why not let the debating
agents declare completion? They have an incentive and tendency to keep elaborating; an
independent verifier avoids that bias and the agreement-loop trap. *Red flag:* "The agents
will agree when they're done with no external verifier.

**Q: What's the single highest-leverage design choice to prevent unbounded discussion?**
A: The orchestration topology - don't use unstructured all-to-all chat. An
orchestrator/supervisor (the recommended default) decomposes the task, assigns subtasks,
collects results, and decides when to stop, so workers report to it rather than debating
each other and no peer loop can form. A sequential pipeline (DAG) or hierarchical
decomposition terminates by construction. Designing the graph eliminates the failure
class, which is far more effective than tuning free-form agents to converge. *Follow-up:*
When is free-form group chat acceptable? Only for short, explicitly-bounded brainstorming
- it's the topology most prone to non-termination. *Red flag:* Proposing to fix a chaotic
group chat purely with better prompts instead of changing the topology.

**Q: Debugging scenario - two agents restate their positions forever. What is this and how
do you break it?** A: It's a livelock - active messaging with no progress (disagreement
deadlock). Break it by introducing decision authority: a designated decider or the
orchestrator makes the final call after rounds, or use voting/quorum with an odd number
of agents, or "disagree and commit" (present a position once, then defer to the decider).
Add stagnation detection so repetition triggers termination. *Follow-up:* How do you
detect the livelock automatically? Semantic-similarity checks on recent messages or no
task-state change for K rounds - the livelock-detection analog from concurrency. *Red
flag:* Adding more discussion rounds hoping the agents eventually agree.

**Q: How does a shared task state / blackboard help, and what does it detect?** A: It's a
single source of truth for decided-vs-open Items, so settled points aren't re-litigated
and progress is visible to all agents. It enables repetition/stagnation detection (no
state change over K rounds) and a convergence metric ("distance to done"); if that metric
isn't decreasing over rounds, you stop and escalate rather than loop, *Follow-up:* Why
track a convergence metric at all? So you can distinguish productive iteration from a
livelock and terminate the latter early, 11ke monitoring a control loop, *Red flag:* Each
agent keeping its own private view of progress with no shared, checkable state.

**Q: Beyond mechanisms, how does prompt-level discipline reduce the tendency to expand -
and why isn't it sufficient alone?** A: Instruct brevity and decisiveness (no restating
agreement, move toward a conclusion, forbid "great point, additlonally..."), give each
agent a narrow role so it contributes its part and stops, and require structured/schema
output that naturally bounds contributions, It's not sufficient alone because prompts only
*reduce* the tendency - you still need a guaranteed hard backstop and a bounding topology,
Since you can't rely on agents to stop themselves. *Follow-up:* How does this map to the
concurrency view? Non-termination = livelock/Infinite loop; the fixes (bounded iteration,
a tie-break authority, progress detection, serialized decisions) are the multi-agent
versions of the same principles. *Red flag:* Treating prompt wording as a complete
solution with no structural or hard-limlt safeguards.

## Synthesis

Unbounded agent discussion is a **termination/liveness problem** (missing done-criteria,
no decision authority, agreement/disagreement loops, circular delegation), so prevent it
with **layered defenses**. Always install **hard backstops** - max turns/rounds,
wall-clock + token/cost budgets, per-agent and recursion caps - guaranteeing termination
like a loop-iteration limit. Define **explicit termination conditions** with a
**verifier/judge** (checkable success criteria, done-signaling, quorum) so completion is
decided by an evaluator, not the debaters. Most importantly, **choose a bounded
orchestration topology** - **orchestrator/supervisor** (default: coordinator with
authority, workers report to it not each other), **sequential pipeline (DAG)**, or
**hierarchical sub-agents** - instead of free-form all-to-all chat (the main cause) - Break
deadlocks with a **designated decider, voting, disagree-and-commit, or human escalations**;
detect **stagnation/repetition** via shared task state and convergence metrics; and apply
**prompt discipline** (brevity, role clarity, structured output). Principal-level view:
treat it as distributed-systems termination - combine & guaranteed hard backstop with
structural prevention (topology) and graceful convergence (authority + done-criteria +
progress tracking), because you cannot rely on agents to stop themselves, and because
every message is real cost.
