<!-- interview/1im_agent_layered_architecture.md -->

# An LLM / Agent System as a Layered Architecture

See also: `llm_agent_protocols_mcp_a2a.md` (tool calling, MCP/AZA, prompt caching, eval,
guardrails).

## Framing

Each term (Prompt, Context, RAG, Tool/Skill, Agent, Loop, Harness) names a component that
solves one specific limitation of a raw LLM. A raw model is a **stateless,
fixed-knowledge, text-in/text-out function** that can't act remember, doesn't know
your private data, and does one forward pass. Every concept here overcomes one of those
limits; layering them clarifles what each solves and how they compose.

## Each concept by role and problem solved

| Concept          | Role                                                                                                                    | Problem it solves                                                                                            |
| ---------------- | ----------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------ |
|  **Prompt**      | The instruction/input to the model                                                                                      | Steer a general model toward a specific task (behavior, format, constraints)                                 |
| **Context**      | Everything in the model's window for a call (prompt + history + retrieved data + tool results)                          | Model is stateless with fixed knowledge - context injects task-relevant, current, private info               |
| **RAG**          | Retrieval-Augmented Generation: fetch external knowledge, inject into context                                           | Training data is fixed/stale and lacks private data; grounding reduces hallucination                         |
| **Tool / Skill** | A callable capability (function, API, code exec) the model can invoke                                                   | Model can't *act* or compute exactly - tools give it hands                                                   |
| **Agent**        | An LLM that plans, chooses tools, and pursues a goal over steps                                                         | A single forward pass can't do multi-step tasks needing decisions + actions                                           |
| **Loop**         | The cycle: model -> action -> observation -> model -> ... until done                                                    | One shot isn't enough; iteration enables reaction + self-correction                                          |
| **ReAct**        | A *pattern for each Loop turn*: interleave Thought -> Action -> Observation                                             | Blind acting (no reasoning) or pure reasoning (no acting) is weak; interleaving grounds reasoning in results |
| **Harness**      | Runtime/orchestration: runs the loop, assembles context, dispatches tools, manages memory, enforces safety, handles I/O | Something must *run* all the above - the engine/scaffolding around the model                                 |

## The layered architecture

```
+---------------------------------------------------------------+
|  Layer 6 - APPLICATION / GOAL                                 |
|  User task, UX, guardrails, success criteria                  |
+---------------------------------------------------------------+
        ^ goal in                              | result out
+---------------------------------------------------------------+
|  Layer 5 - HARNESS (Orchestration Runtime)                    |
|  Runs the LOOP, assembles CONTEXT, dispatches TOOLS,          |
|  manages MEMORY, enforces safety/limits, handles I/O          |
|  -- the "operating system" of the agent --                    |
+---------------------------------------------------------------+
       ^                    ^                       ^
+--------------+  +--------------------+  +--------------------+
| Layer 4      |  | Layer 4            |  | Layer 4            |
| AGENT LOGIC  |  | TOOLS / SKILLS     |  | MEMORY / STATE     |
| plan, decide,|  | APIs, code exec,   |  | short-term (chat)  | 
| reflect      |  | RAG-as-a-tool      |  | long-term store    |
+--------------+  +--------------------+  +--------------------+
     ^                    ^                       ^
+---------------------------------------------------------------+
|  Layer 3-CONTEXT (assembled per call)                         |
|  system prompt + task PROMPT + history + RAG results +        |
|  tool outputs + memory -> the model's working window          |
+---------------------------------------------------------------+
        ^                                       |
+---------------------------------------------------------------+
|  Layer 2 - KNOWLEDGE / RETRIEVAL (RAG)                        |
|  vector DB, search, embeddings - grounds context in           |
|  current/private data                                         |
+---------------------------------------------------------------+
        ^                                       |
+---------------------------------------------------------------+
|  Layer 1 - MODEL (the LLM)                                    |
|  stateless text-in -> text-out; reasoning + generation        |
+---------------------------------------------------------------+
```

Notes: **RAG spans L2-L3** (a knowledge source feeding context, also invocable as a tool).
The **Loop is the temporal dimension of the Harness (L5)** - the cycle it drives across
L1-L4, not a separate box.

## How they cooperate - one turn of the loop

1. **Application (L6)** hands the harness a goal ("resolve this ticket").
2. **Harness (L5)** assembles **Context (L3)**: system prompt + task **Prompt** +
   **Memory** + relevant **RAG (L2)** retrieval + prior tool outputs.
3. Sends context to the **Model (L1)**, which reasons and emits either a final answer or a
   **tool call** (this decision *is* the **Agent** logic, L4).
4. On a tool call: **Harness** dispatches the **Tool/Skill (L4)**, gets an observation
   (API result, code output, doc), appends it to **Context**.
5. **Loop:** back to step 2 with augmented context; repeat until done or a limit hits.
6. **Harness** returns the result to the **Application**, updating **long-term Memory**.

Flow: **prompt** shapes intent - **Context** carries what the stateless model needs ->
**RAG** grounds it -> **Mode1** reasons - **Agent** decides to act -> **Tools/Skills**
act - **Loop** iterates on results -> **Harness** orchestrates + bounds the cycle ->
serves the *Application"* goal.

## Loop vs. ReAct (a common conflation)

**A Loop is the generic control-flow mechanism; ReAct is one specific *strategy* for what
happens inside each loop iteration.** Different abstraction levels - 11ke confusing "a
`while` loop" with "the algorithm you run in the loop body."

- **Loop (mechanism):** the iteration cycle itself -> model -> action -> observation ->
  feed back -> repeat until termination. Structural; says *that* iteration happens and
  *when to stop*, nothing about *how* the model decides each step. Any multi-step agent
  has a loop; it's the harness's control flow (L5).
- **ReAct (pattern, "Reasoning + Acting", Yao et al, 2022):** convention for *what the
  model emits each turn* - an explicit **Thought** (reasoning) -> **Action** (tool call)
  -> **Observation** (result), then repeat. Its innovation: make the model **verbalize
  reasoning before acting** and **feed observations back into the next reasoning step**.
  Behavioral; realized through prompt structure. **ReAct runs *inside* a loop.**

|              | Loop | ReAct		|
| ------------ | ------------------------------------------- | --------------------------------------------------------------------------- |
| What	       | Control-flow mechanism (iterate until done) | Reasoningtacting pattern for each iteration                                 |
| Level        | Structural / harness                        | Behavioral / prompting strategy                                             |
| Concerns     | *That* iteration happens + termination      | *How* the model reasons and acts each turn                                  |
| Analogy      | The `while` loop                            | The algorithm in the loop body                                              |
| Alternatives | (none - it's the substrate)                 | Plan-and-Execute, Reflexion, Tree-of-Thoughts, plain tool-calling, CoT-only |

**Other in-loop strategies (ReAct is one of many):** plain native tool-calling (reasoning
implicit, no explicit Thought), **plan-and-Execute** (plan all steps up front, then run),
**Reflexion** (add self-critique after observations), **Tree-of-Thoughts** (branch over
multiple reasoning paths), **CoT-only** (reason step-by-step with no actions - ReAct minus
"Act"). All are carried by the same Loop.

So: **Loop = the iteration that carries any strategy; ReAct = Reasoning+Acting interleaved
as the content of loop turns.** ReAct's specific contribution is interleaving reasoning
with acting and grounding reasoning in observations -versus pure reasoning (CoT) or blind
tool calls.

# Worker analogy

- **Model** = brain (reasoning/language). **Prompt** = the instructions you give.
- **Context** - what's on the desk / in working memory. **RAG** - looking things up in the
  library.
- **Tools/Skills** = hands and instruments. **Memory**= notebook (short-term) + archives
  (long-term).
- **Agent** = judgment: planning/deciding next action. **Loop** = do -> check -> adjust
  rhythm.
- **Harness**-theworkplace/manager supplying materials, enforcing rules, coordinating
  all.

## What each layer fundamentally copes with

- **Statelessness** -> Context + Memory.
- **Fixed/stale/private knowledge & hallucination** -> RAG (grounding).
- **Can't act or compute exactly** - Tools/Skills.
- **Single-shot insufficiency** -> Agent + Loop (the *mechanism*), with
  ReAct/Plan-Execute/Reflexion as *strategies* inside it.
- **Steering a general mode1** -> Prompt.
- **Coordinating, bounding, operating safely** -> Harness.

## Staff / Principal-level framing

- **The LLM is a fixed function; everything else engineers its inputs, outputs, and
  iteration.** Context engineering (what's in the window) + orchestration (harness/loop)
  drive most real-world quality - why the same base model performs wildly differently
  across apps.
- **Context is the scarce central resource.** All Layers compete for the finite window;
  the hard engineering is *curation* - RAG relevance, memory summarization/compaction.
  tool-output trimming - so the model sees the right things, not everything. Context
  pollution/overflow is the dominant fallure mode.
- **The Harness is the real "operating system."** Reliability, safety, cost, latency are
  governed here: loop-termination 1im1ts, tool sandboxing, retries/error handling, budget
  caps, observability. Production agent quality is mostly harness quality.
- **The loop is a control system** with classic concerns - non-termination, error
  propagation, compounding hallucination, per-iteration cost blow-up - needing bounded
  iteration, guardrails, and evaluation. **Separate the loop (mechanism) from the in-loop
  strategy (ReAct, Plan-and-Execute, Reflexion, ...):** you tune reliability/cost via the
  loop (limits, retries) and reasoning quality via the strategy - they're independent
  knobs.
- **skills vs. tools is a granularity choice** - a "skill" often bundles prompt + tools +
  domain logic into a reusable higher-level capability (composition atop the tool layer),
  scaling competence without one giant prompt.
- **RAG is one grounding strategy, not the only one** - fine-tuning, long-context
  stuffing, tool-based lookup are alternatives/complements; the architecture treats
  grounding as a layer whose implementation you choose.

## Interview Q&A

**Q: What fundamental limitations of a raw LLM does this layered architecture exist to
overcome?** A: A raw model is a stateless, fixed-knowledge, text-in/text-out function that
can't act, can't remember, doesn't know your private data, and does a single forward pass.
Each layer targets one limitation: Context + Memory cope with statelessness; RAG grounds
it in current/private knowledge and reduces hallucination; Tools/Skills let it act and
compute exactly: Agent + Loop handle single-shot insufficiency: the Prompt steers the
general model; and the Harness coordinates, bounds, and runs everything safely.
*Follow-up:* Which layer does RAG span and why? L2-L3 - it's a knowledge/retrieval source
(L2) that feeds the assembled context (L3), and it can also be invoked as a tool. *Red
flag:* Treating these as interchangeable buzzwords rather than each solving a distinct
limitation.

**Q: People conflate "Loop" and "ReAct" - what's the precise difference?** A: A Loop is
the generic control-flow *mechanism*(model → action → observation feed back + repeat
until termination) - it governs *that* iteration happens and *when to stop*. ReAct is one
*strategy* for what the model emits inside each iteration: an explicit Thought → Action →
Observation, verbalizing reasoning before acting and feeding observations back. It's the
difference between a `while` loop and the algorithm you run in its body; ReAct runs
*inside* a loop. *Follow-up:* Name other in-loop strategies. Plain native tool-calling,
Plan-and-Execute, Reflexion, Tree-of-Thoughts, and CoT-only - all carried by the same
loop. *Red flag:* Saying "ReAct is the loop" or treating them as the same abstraction
level.

**Q: Why is the Harness described as the agent's "operating system"?** A: Because it's the
runtime that actually runs the loop, assembles context, dispatches tools, manages memory,
enforces safety/limits, and handles I/0 - nothing else executes the model's decisions.
Reliability, safety, cost, and latency are all governed here via loop-termination limits,
tool sandboxing, retries/error handling, budget caps, and observability, which is why
production agent quality is mostly harness quality. *Follow-up:* Give two independent
knobs the harness exposes. Loop controls (iteration limits, retries) tune
reliability/cost; the in-loop strategy (ReAct, Plan-and-Execute) tunes reasoning quality -
they're separate. *Red flag:* Thinking the model or the prompt alone determines production
reliability.

**Q: Why is context called "the scarce central resource," and what's the dominant failure
mode?** A: Every layer - system prompt, task prompt, history, RAG results, tool outputs,
memory - competes for one finite context window, and the hard engineering is curation:
getting the model to see the right* things, not everything. The dominant failure mode is
context pollution/overflow: unbounded growth (often from verbose tool outputs) that
overflows the window or degrades reasoning ("lost in the middle"). *Follow-up:* What
techniques keep context under budget? RAG relevance filtering, memory
summarization/compaction, tool-output trimming, and sub-agent decomposition with fresh
contexts. *Red flag:* Assuming a bigger context window removes the need for curation.

**Q: Debugging scenario - an agent loops forever or balloons in cost without finishing.
Where in the architecture do you look?** A: The loop is a control system, so look at the
Harness (L5): is there a bounded iteration/turn limit, a token/cost budget, and
error-handling that stops compounding hallucination? Non-termination, error propagation,
and per-iteration cost blow-up are classic control-system failures. Fix with hard
loop-termination limits and guardrails at the harness, and separately check whether the
in-loop strategy is thrashing (e.g., re-planning every turn). *Follow-up:* Why separate
the loop fix from the strategy fix? You tune termination/cost via the loop and reasoning
quality via the strategy - conflating them leads to tweaking prompts when you needed a
turn cap. *Red flag:* "Just tell the model to stop when done" and relying on the model to
self-terminate.

**g: How do a "tool" and a "skill" differ, and why does it matter?** A: A tool is a single
callable capability (a function, API, or code exec) the model invokes. A skill is a
granularity/composition choice - it typically bundles a prompt plus one or more tools plus
domain logic into a reusable higher-level capability. Skills let you scale an agent's
competence without one giant unwieldy prompt, composing atop the tool layer. *Follow-up:*
Where does RAG sit in this framing? As one grounding strategy that can be exposed as a
tool* ("RAG-as-a-tool"), alongside alternatives 11ke fine-tuning or long-context stuffing.
*Red flag:* Insisting tools and skills are the same thing with no granularity distinction.

**Q: Design trade-off - why is "the model is fixed and interchangeable" a useful mental
stance?** A: Because engineering leverage and most real-world quality differences come
from context engineering (what's in the window) and harness orchestration, not from the
base model -which is why the same model performs wildly differently across applications.
It directs effort toward curation, retrieval relevance, tool design, and loop/guardrail
quality rather than assuming a model swap fixes a poorly-engineered agent. *Follow-up:*
When is swapping the model actually the right lever? When you hit a genuine capability
ceiling (reasoning, context length, modality) that better inputs can't overcome. *Red
flag:* Attributing all agent quality to model chole and ignoring context/harness
engineering.

## Synthesis

These concepts form a layered LLM-application architecture, each solving one limitation of
a raw model: the **Model (L1)** is a stateless reasoning/generation function;
**RAG/Retrieval (L2)** grounds it in current, private knowledge; **Context (L3)** is the
assembled working window (system + **prompt** + memory + retreved data + tool results)
overcoming statelessness; **Agent logic, Tools/Skills, and Memory (L4)** add
decision-making, the ability to act/compute, and persistence; the **Harness (L5)** is the
orchestration runtime - the agent's OS - driving the **Loop** (model -> act -> observe ->
repeat), assembling context, dispatching tools, and enforcing safety/cost/termination; all
serving the **Application goal (L6)**. They cooperate cyclically: the **Prompt** shapes
intent, **Context** feeds the stateless **Model**, **RAG** grounds it, the **Agent**
decides to invoke **Skills/Tools**, the **Loop** iterates on observations, and the
**Harness** coordinates and bounds it. The principal-level insight: the model is fixed and
interchangeable; engineering leverage - and failure modes - live in **context curation and
harness orchestration**, with the finite context window as the central scarce resource and
the loop as a control system needing bounded, observable, safe iteration.
