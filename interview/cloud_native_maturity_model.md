<!-- interview/cloud_native_maturity_model.md -->

# The Cloud Native Maturity Model (CNMM) - Assessment Levels and Design Axes

**Accuracy note:** there is no single canonical "Cloud Native Maturity Mode1." Several
exist - the **CNCF Cloud Native Maturity Model** (CNCF/Cartografos working group) plus
vendor/community variants. This describes the widely-used CNCF model; exact level *Labels*
vary by source, but the progression and dimensions* are the stable, well-established
part. The CNCF *Landscape** and **Trail Map** are the companion artifacts (what tools
exist / a suggested adoption order).

Companion docs: "cloud_native_features md" (feature/tool catalog),
`cloud_native_vs_traditional.md` (benefits vs. traditional + cost).

## Part 1 - The model

### What it is and why it exists

A maturity model assesses **where an organization sits on its cloud-native journey** and
what the next steps are. It is deliberately **holistic**: cloud-native transformation is
**people, process, and culture** as much as containers and Kubernetes. Core message: you
cannot buy cloud-native maturity by installing Kubernetes; you progress across *multiple
dimensions* over time, and skipping the organizational/process changes is why many "we
adopted k8s* efforts stall.

### The five levels (CNCF)

1. **Level 1 - Build (Getting Started / No Process).** First experiments: containerize an
   app, run a PoC, maybe stand up a cluster. Ad-hoc, individual-driven, learning phase.
2. **Level 2 - Operate.** Can run cloud-native workloads in production. Kubernetes
   deployed and operated; basic CI/CD; some standardization. Often still lift-and-shift;
   learning reliable operation.
3. **Level 3 - Scale.** Practices spread across teams. Automation matures (IaC, GitOps),
   observability in place, autoscaling used, security being integrated. Platform supports
   multiple teams; processes repeatable.
4. **Level 4 - Improve / Optimize.** Shift from "make it work" to "make it better":
   performance, **cost (FinOps)**, reliability (SRE/SLOs), security hardening, developer
   experience. Continuous, data-driven improvement.
5. **Level 5 - Adapt.** Fully cloud-native and *self-adapting*: fluid response to change,
   continuous experimentation, platform innovation, cloud-native embedded in culture.
   Autonomy and business agility are the norm.

(Progression often written **Build -> Operate -> Scale -> Improve -> Adapt**; labels vary
by source.)

### How organizations use it

- **Baseline** - honestly score each dimension (Part 2) today.
- **Gap analysis** -find lagging axes (often people/process/culture, not tech).
- **Roadmap** - plan incremental moves to the next level per dimension, not a big-bang
  jump.
- **Alignment** - shared vocabulary for "where we are / what's next"; justify investment
  (platform team, training) beyond tooling.

An org can be **Level 4 on technology but Level 2 on culture** - surfacing that imbalance
is the model's point. Real maturity means advancing the *Lagging* dimensions, which
usually gate the technical ones.

## Part 2 - The dimensions as design axes

The CNMM dimensions aren't just an assessment scorecard - each is a **design lens** posing
a distinct architectural question. A rigorous design examines the system through *every*
axis and checks the levels are *balanced* (the weakest axis gates the whole). For each
axis: the **design question*, **what to analyze**, a **maturity gradient**, and the **key
tension**.

### Axis 1 - Cloud-Native Services (infrastructure & platform)

**Design question:** what primitives does the app consume, and how much undifferentiated
heavy lifting do we offload to the platform/provider?

- **Design:** self-managed vs, managed split - compute (VM - containers - serverless),
  data (self-hosted -> managed -> serverless DB), messaging (self-run Kafka -> managed
  streaming); build on Taas / Kubernetes (Caas) / Paas / Faas.
- **Analyze:** portability vs, lock-in, operational burden vs, velocity, cost model,
  whether each service is truly cloud-native (elastic, API-driven, managed lifecycle) or a
  lifted legacy component,
- **Gradient:** VMs/1ift-and-shift -> containers on orchestrator -> managed services ->
  serverless/scale-to-zero.
- **Tension:** more managed - less toil, more lock-in. Consciously choose where on the
  abstraction ladder to stand.

### Axis 2 - Application-Centric Design (architecture)

**Design question:** how is the app decomposed, and does its structure exploit the cloud
model (stateless, resilient, independently deployable)?

- **Design:** monolith / modular monolith / microservices; boundardes via DDD bounded
  contexts; statelessness (push state to backing services - 12-factor): API contracts;
  sync (REST/gRPC) vs. async (events); data ownership per service; resilience patterns
  (circuit breakers, retries, bulkheads, graceful degradation).
- **Analyze:** coupling/cohesion (boundaries aligned to domains + teams - Conway's law?),
  failure blast radius, consistency strategy (ACID vs. eventual/saga; name the CAP/PACELC
  trade explicitly), independent deploy/scale.
- **Gradient:** monolith -> coarse services -> well-bounded microservices -> event-driven,
  self-contained, independently scalable, failure-designed.
- **Tension:** decomposition buys independence, pays distributed-systems complexity -
  match to team/scale, not fashion.

### Axis 3 - Automation & Delivery (CI/CD, GitOps)

**Design question:** how does code get from commit to production, and how
repeatable/safe/fast is that path?

- **Design:** CI/CD pipelines; **immutable artifacts**; **Iac** (Terraform/Pulumi) +
- **GitOps** (Argo/Flux) for declarative version-controlled state; progressive delivery
  (canary/blue-green/flags); automated rollback.
- **Analyze:** the **DORA metrics**- deploy frequency, lead time, change-failure rate,
  MTTR; manual toil; config drift; reproducibility.
- **Gradient:** manual -> scripted CI -> full CI/CD -> Gitops reconciliation + progressive
  delivery + auto rollback.
- **Tension:** automation cost vs. payoff - high-value once change velocity matters; the
  reconciliation loop is the unifying idea.

### Axis 4 - Observability

**Design question:** can we understand behavior and diagnose fatlures across distributed
components in production?

- **Design:** three pillars (metrics/logs/traces) via OpenTelemetry; structured logging;
  distributed tracing across hops; **SLIs/SLOs** + error budgets; alert on user-facing
  symptoms, not just causes.
- **Analyze:** end-to-end request traceability; alerting on SLO burn vs. noise; MTTD;
  telemetry cardinality/cost; are signals actionable?
- **Gradient:** host monitoring -> centralized metrics/logs -> distributed tracing ->
  SLO-driven ops with error budgets gating releases.
- **Tension:** depth vs. cost (telemetry storage can rival compute) - instrument what
  drives decisions.

## Axis 5 - Security (DevSecOps)

**Design question:** is security designed in across lifecycle + runtime, or bolted on?

- **Design:** zero-trust (TLS via mesh, network policies/micro-segmentation), RAC/Least
  privilege, secrets mgat (Vault/external-secrets), **shift-left** CI scanning.
  **supply-chain** integrity (Sigstore signing, SBOMS, SLA), **policy-as-code**
  (OPA/Gatekeeper, Kyverno), runtime security (Falco).
- **Analyze:** attack surface/blast radius; trust boundaries; automatic admission-time
  policy enforcement; supply-chain provenance; compliance.
- **Gradient:** perimeter/bolt-on -> shift-left scanning -> automated policy + zero-trust
  -> continuous, supply-chain-hardened, runtime-enforced.
- **Tension:** security friction vs. velocity - policy-as-code makes guardrails automatic,
  not gatekeeping.

### Axis 6 - People / Process / Culture (the org - often the true bottleneck)

**Design question:** do team structure, ownership, and culture support the architecture?

- **Design:** team topologies (stream-aligned teams, a **platform team** offering
  self-service via an **internal developer platform / IDp** such as Backstage, enabling
  teams); "**you build it, you run it**"; blameless postmortems; DevOps/SRE.
- **Analyze:** Conway's-law alignment (service boundaries vs. team boundaries), cognitive
  load per team, on-call/ownership clarity, skills gaps.
- **Gradient:** siloed dev/ops - Devops collaboration -> platform-enabled self-service
  (platform engineering) - autonomous, experimentation-driven.
- **Tension:** usually *Lags* and *gates* every other axis - high tech + low org maturity
  fails.

## Axis 7- Governance & Cost (FinOps / policy)

**Design question:** are cost, compliance, and policy first-class automated concerns?

- **Design:** cost attribution/showback per team/service, rightsizing, autoscale-to-zero,
  spot/reserved mix, quota/policy governance, multi-tenancy controls.
- **Analyze;** cost per unit of business value, egress/inter-service cost, resource
  sprawl, policy compliance, budget ownership.
- **Gradient;** ungoverned/unattributed spend -> cost visibility -> FinOps -> automated
  cost + policy optimization,
- **Tension:** elasticity that saves money enables runaway spend without governance.

## Part 3 - Running a design/analysis using the axes

1. **Score each axis independently** (current state) -> an uneven radar chart.
2. **Find the gating axis** - the least-mature dimension caps realized value; prioritize
   it (often culture/process or observability).
3. **Check cross-axis coherence** - the axes are coupled; incoherence is where designs
   fail;
   - Microservices (App) *require observabllity (tracing) + automation (independent
     deploy) + org (team-per-service); advancing one alone yields a **distributed
     monolith** you can't operate.
   - Serverless (Services) shifts security + observability models (ephemeral, vendor
     telemetry).
   - Zero-trust (Security) depends on a mesh (Services) + CI policy (Automation).
4. **Set a per-axis target matched to requirements** - don't max every axis; target what
   scale/velocity/availability/comp11ance need.
5. **Roadmap incrementally**, advancing lagging + coupled axes together.

## Staff / Principal-level framing

- **Punctures the "we installed Kubernetes, so we're cloud-native" fallacy.** Technology
  is the *easy* axis; people/process/culture determine whether cloud-native delivers
  value. High-tech/low-culture - the costs without the benefits.
- **The system moves at the speed of its least-mature axis** (an Amdahl's-law-like
  constraint on transformation). Teams over-invest in the comfortable axis (usually
  Technology/App) and under-invest in the gating ones (Culture, Observability,
  Governance); forcing an explicit pass over every axis is the value.
- **Coherence across axes beats height on any one.** The **distributed monolith** is the
  canonical failure: high App-decomposition, low Automation/observability/org - you paid
  for microservices and got only the costs.
- **Conway's law is the hidden coupling** between App and People axes - architecture
  mirrors org; design them together.
- **progressincrementally and value-driven, not level-chasing.** Advance to the maturity
  your business (scale/velocity/reliability) justifies; over-maturing a small,
  steady product is over-engineering.
- **Use objective metrics per axis** - DORA for Delivery, SLOs for
  Observability/Reliability, cost-per-value for FinOps - so maturity is measured, not
  asserted.
- **A compass, not a scorecard.** Not precisely standardized (multiple versions); its
  worth is as a structured conversation and roadmap tool. Take labels loosely, dimensions
  and direction seriously.

## Likely interview questions

- *"We run Kubernetes in prod - are we mature?"* - Not necessarily: score all seven axes;
  tech-only maturity with siloed ops and no SLOs is Level 2 at best.
- *"Where would you invest first?"* - The gating axis, found by scoring; usually
  org/platform or observability, justified with DORA/SLO data.
- *"What's a distributed monolith and how do you detect it?"* - Services that must deplay
  together, shared DB, no tracing; detect via coupled releases and cross-service
  change-failure rate.
- *"How does Conway's Law affect your design?"* - Draw service boundaries along team
  boundaries (or reorganize teams - the inverse Conway maneuver).

## Synthesis

The Cloud Native Maturity Model is a **holistic framework for assessing and guiding an
organization's cloud-native journey across five levels** - **Build -> operate -> Scale ->
Improve -> Adapt** (CNCF/Cartografos; labels vary). Its dimensions double as **design
lenses**: **Services** (abstraction ladder, lock-in), **Application design**
(decomposition, statelessness, resilience), **Automation/Delivery** (GitOps, DORA),
**Observability** (SLO-driven ops), **Security** (shift-left, zero-trust, supply chain),
**People/Process/Culture** (platform teams, the usual gating axis), and
**Governance/Cost** (FinOps), Use it to baseline honestly, find the **least-mature gating
axis**, ensure **cross-axis coherence** (or get an unoperable distributed monolith), set
**per-axis targets matched to real requirements**, and roadmap incrementally, Balance and
coherence matter more than height on any single axis,
