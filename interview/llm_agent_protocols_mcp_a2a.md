<!-- interview/llm_agent_protocols_mcp_a2a.md -->

# Agent Interop and Production Concerns: Tool Calling, MCP, A2A, Prompt Caching, Evaluation, Guardrails

Extends `llm_agent_layered_architecture. md` (where tools sit in the stack),
`agent_context_overflow_handling.md`, and `multi_agent_avoid_unbounded_discussion.md`.
Protocol specs move fast - check current versions before quoting details.

## 1. Tool / function calling - the primitive everything builds on

1. The application sends the model a list of tools: **name, description, JSON Schema for
   the arguments**.
2. The model decides to call one and emits a structured tool-call block
   {`{"name": "...", "arguments": {...}}`} instead of (or alongside) text.
3. The **harness**- not the model - validates the arguments, executes the tool, and
   appends the result as a tool-result message.
4. Loop until the model returns a final answer.

Key points:

- The model never executes anything; the harness is the security boundary. Validate
  arguments against the schema; treat tool output as **untrusted input** (prompt injection
  rides in on web pages, files, tickets).
- **Tool descriptions are prompts,** Vague names and overlapping tools cause wrong
  selection; tool count and schema size cost context tokens every turn.
- **Structured outputs / constrained decoding** (JSON Schema-enforced generation) remove
  "invalid SON" failures but not "valid but wrong" ones.
- Parallel tool calls: the model may emit several independent calls in one turn; the
  harness can run them concurrently.

## 2. MCP - Model Context Protocol

**Problem solved:** N agent hosts × M tools - NM bespoke integrations. MCP (introduced by
Anthropic inNov Nov now widely adopted and governed as an open standard) defines one
protocol between **hosts/clients** (IDES, chat apps, agent runtimes) and **servers** that
expose capabilities. It's often described as "USB-C for AI tools."

- **Roles:** *Host* (the app containing the model) runs one *client* per connection to an
  MCP *server*.
- **Wire format:** **JSON-RPC 2.0**. Transports: **stdio** (local subprocess) and
  **Streamable HTTP** (remote; replaced the earlier HTTP+SSE transport). Session starts
  with an initialize handshake that negotiates protocol version and capabilities.
- **Server primitives:**
  - **Tools** - model-invoked functions (`tools/list`, `tools/call`) with JSON Schema
    inputs (and optional output schemas).
  - **Resources** - application-controlled read-only context addressed by URI
    (`resources/list`, `resources/read`, subscriptions).
  - **Prompts** - user-selected templates (`prompts/list`, `prompts/get`).
- **Client primitives (server -> client):** **sampling** (server asks the host's model for
  a completion), **elicitation** (server asks the user for input), **roots** (which
  filesystem/URI scopes the server may operate on).
- **Notifications** for list changes, progress, logging, cancellation.
- **Auth:** remote servers use **Auth 2.1** (the server acts as a resource server;
  protected-resource metadata for discovery).

**Security issues interviewers probe:**

- **Tool poisoning** - malicious descriptions/instructions hidden in tool metadata.
- **Prompt injection via tool results** (an issue tracker or web page telling the agent to
  exfiltrate secrets).
- **Confused deputy / token passthrough** - a server reusing the user's token against
  downstream APIs it wasn't issued for.
- **Rug pulls** - a server changing tool definitions after approval; pin versions,
  re-review on change.
- **Over-broad permissions** - local stdio servers run with the user's privileges.
  Mitigate with least privilege, sandboxing, human approval for side-effecting tools,
  allowlists.

## 3. A2A - Agent2Agent Protocol

**Problem solved:** MCP connects an agent to tools*; A2A (introduced by Google in Apr
2025, donated to the Linux Foundation) connects an agent to *other agents* - possibly from
different vendors and frameworks, treated as opaque peers.

- **Agent Card** - JSON metadata (commonly at `/.well-known/agent-card.json`) advertising
  the agent's name, endpoint, skills, supported modalities, and auth requirements. Used
  for discovery,
- **Task** - the unit of work, with a lifecycle (`submitted` → `working` →
  `input-required` → `completed` / `failed` / `canceled`), Long-running tasks are
  first-class,
- **Messages and Parts** (text, file, structured data) for conversation, **Artifacts** for
  outputs.
- **Transport:** JSON-RPC over HTTP(S); streaming via **SSE**; **push notifications**
  (webhooks) for long tasks, Enterprise auth schemes declared in the Agent Card.

**MCP VS A2A:**

|  | MCP | A2A |
| ----------- | ------------------------------------- | ---------------------------------------------- |
| Connects    | Agent ↔ tools/data                    | Agent ↔ agent                                  |
| Counterpart | Transparent: schema-defined functions | Opaque: autonomous peer with its own reasoning |
| Unit        | Tool call / resource read             | Task with lifecycle                            |
| Typical use | Give one agent capabilities           | Delegate across teams/vendors/orgs             |

They are complementary: an A2A agent often uses MCP internally. Inside a single codebase,
a plain function call or sub-agent invocation is usually simpler than either protocol.

## 4. Prompt caching (cost and latency lever)

- Providers cache the **KV state of a prompt prefix** so repeated prefixes skip prefill.
  Typical economics: cache reads billed at a fraction of normal input price (on the order
  of ~10%), plus lower time-to-first-token; cache writes may cost slightly more. Caches
  expire after minutes of inactivity (some providers offer longer TTLS).
- Anthropic uses explicit `cache_control` breakpoints; OpenAI and Gemini cache eligible
  prefixes automatically (minimun lengths apply).
- **Design implication: order the prompt from most to least stable** - system prompt, tool
  definitions, long documents, then conversation history, then the new turn. Anything that
  changes early in the prompt (timestamps, reordered tools, a per-request ID in the system
  prompt) invalidates everything after it.
- Interaction with compaction: summarizing history rewrites the prefix and causes a cache
  miss - compact in larger, less frequent steps. Serving-side analog: VLLM automatic
  prefix caching, Slang RadiAttention (`kv_cache_rollback_tree_attention.md`).

## 5. Evaluation - "how do you know the agent works?"

- **Offline eval set** built from real tasks with expected outcomes run on every
  prompt/model/tool change (regression testing for prompts).
- Grade the outcome** (task success, verified by tests, state checks, or exact answers)
  and the **trajectory** (right tools, no unnecessary steps, cost/latency/turn count).
- **LL-as-judge** for open-ended outputs: use rubrics, pairwise comparison, and calibrate
  against human labels; watch for position/verbosity/self-preference bias.
- Agents are **non-deterministic**: run each case several times and report pass rate
  (pass@k/pass^k for reliability).
- Public benchmarks for reference: SWE-bench (Verified), t-bench (tool use + policy),
  GAIA, WebArena, Terminal-Bench. Benchmarks ≠ your workload.
- **Online:** tracing every step (OpenTelemetry GenAI conventions,
  LangSmith/Langfuse-style tools), user feedback, A/B tests, cost dashboards.

## 6. Guardrails and safety

Defense in depth, because the model itself can't be fully trusted to follow instructions
under adversarial input:

- **Input:** classify for injection/jailbreak and PII; mark untrusted content clearly
  (spotlighting / delimiters) - helps, doesn't solve.
- **Capability limits:** least-privilege tools, read-only by default, sandboxed code
  execution, network egress allowlists, scoped credentials per task.
- **Human-in-the-loop** approval for irreversible or high-impact actions (payments,
  deletes, sending email, prod deploys).
- **Output:** schema validation, policy classifiers, secret/PII scrubbing,
  grounding/citation checks for RAG answers.
- **Budgets:** max turns, tokens, cost, wall-clock (see
  `multi_agent_avoid_unbounded_discussion.md`).
- The "lethal trifecta": **private data access + untrusted content + an exfiltration
  channel** in one agent is the dangerous combination - remove at least one leg.

## 7. Frameworks you'll be asked about (one line each)

- **LangGraph** - agents as explicit state graphs with checkpoints; good for controllable,
  resumable workflows.
- **OpenAI Agents SDK** (successor to Swarm) - agents, handoffs, guardrails, tracing.
- **AutoGen / Microsoft Agent Framework**, **CrewAI** - multi-agent conversation and
  role-based crews.
- **Google ADK**, **Claude Agent SDK** - vendor agent runtimes with MCP support.
- Common interview stance: frameworks speed up prototyping, but the core loop is ~100
  lines; production teams often own the loop for control over context, retries, and
  observability.

## Likely interview questions

- *"How does function calling actually work? Who executes the tool?"*
- *"What problem does MCP solve? What are its primitives and transports?"*
- *"MCP VS A2A - when would you use each?"*
- *"How would you secure an agent that reads emall and can call APIs?"* - Least privilege,
  approval gates, injection handling, break the lethal trifecta.
- *"How do you cut cost and Latency for a Long-running agent?"* - Prompt-cache-friendly
  ordering, compaction, smaller models for sub-tasks, tool-output truncation.
- *"How do you evaluate an agent before shipping a prompt change?"*

## Synthesis

Agents act through **tool calling**: the model emits schema-shaped calls, and the
**harness** validates and executes them - the harness is the trust boundary. **MCP**
standardizes agent-to-tool integration (JSON-RPC 2,0 over stdio or Streamable HTTP; tools,
resources, prompts; sampling/elicitation/roots; Auth for remote servers) and brings
tool-poisoning and injection risks that need least privilege and approval gates. **A2A**
standardizes agent-to-agent delegation between opaque peers (Agent Cards, Tasks with
lifecycles, streaming and push). In production, **prompt caching** rewards stable-prefix
prompt layout, **evaluation** needs repeated outcome-plus-trajectory testing on real
tasks, and **guardrails** layer input handling, capability limits, human approval, and
output validation, because no single layer stops prompt injection.
