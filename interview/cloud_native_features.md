<!-- interview/cloud_native_features.md -->

# Cloud-Native: Features and Concepts

## What "cloud-native" means

Cloud-native builds and runs applications to **fully exploit the cloud model**: elastic,
on-demand infrastructure treated as commodity, with apps designed to be resilient,
scalable, observable, and continuously deliverable. The CNCF definition centers on
**containers, microservices, immutable infrastructure, and declarative APIs**, enabling
**loosely coupled systems that are resilient, manageable, and observable**, plus
automation to make **high-impact changes frequently and predictably with minimal toil**.

Mental model: stop treating servers as pets you nurse; treat compute as cattle - fungible,
replaceable, declaratively managed. Almost every cloud-native feature flows from that
shift.

## 1. Containers and orchestration

- **Containers (Docker/OCI):** Immutable image packaging app+ dependencies; consistent
  laptop-to-prod; lightweights. VMs (shared kernel, fast start) - density and
  portability.
- **Kubernetes** - de facto orchestrator:
  - **Declarative desired-state** - controllers reconcile reality toward what you declared
    (control-loop pattern).
  - **pods, Deployments, Replicasets** - scheduling/11fecycle.
  - **Self-healing** - restart failed containers, reschedule on dead nodes.
  - **HPA / Cluster Autoscaler** - scale pods/nodes to load.
  - **Services & Ingress** - stable VIPs, service discovery, L4/L7 routing.
  - **ConfigMaps/Secrets** - externalized config.
  - **Rolling updates / rollbacks** - zero-downtime deploys.
  - **Namespaces, RBAC, quotas** - multi-tenancy/isolation.
  - **CRDS+ Operators** - extend the API with custom resources + controllers encoding
    operational knowledge.

## 2. Microservices

Decompose a monolith into independently deployable, loosely-coupled services owning their
own data. Benefits: independent scaling/deploy, team autonomy, fault isolation, polyglot.
Costs: distributed-systems complexity (network/partial failure, data consistency,
cross-service debugging) - much cloud-native tooling exists to manage this cost.

## 3. Service mesh

- **Istio, Linkerd, Cilium** - offload cross-cutting network concerns into a **sidecar
  proxy** (or eBPF): mTLS, traffic routing (canary/A-B/mirroring),
  retries/timeouts/circuit-breaking, telemetry. **data plane** (proxies) + **control
  plane** (policy). Trend: sidecar -> **eBPF/ambient** mesh to cut overhead.

## 4. Declarative infrastructure & GitOps

- **IaC** - Terraform, Pulumi, CloudFormation: declarative, versioned, reviewable infra.
- **GitOps** - Argo CD, Flux: Git is the source of truth; a controller continuously
  reconciles the cluster to match. Auditable, revertible.
- **Immutable infrastructure** - replace, never patch in place; eliminates config drift.

## 5. CI/CD & progressive delivery

- Automated build -> test -> deploy (GitHub Actions, Gitlab CI, Tekton, Jenkins).
- **Progressive delivery** - canary, blue-green, feature flags for gradual rollout + fast
  rollback (Argo Rollouts, Flagger).
  
## 6. Observability

- **Metrics** - Prometheus (pull-based) + Grafana; alerting via Alertmanager.
- **Logging** - structured/centralized (Fluentd/Fluent Bit -> Loki/Elasticsearch).
- **Distributed tracing** - OpenTelemetry, Jaeger, Zipkin: trace requests across services.
- **OpenTelemetry** - converging instrumentation standard for metrics/logs/traces.
- Three pillars (metrics/logs/traces) + increasingly **SLO/SLI-driven** ops and eBPF deep
  visibility.

## 7. Serverless / Faas

- **AWS Lambda, Knative, Cloud Run, Azure Functions** - run code without managing servers;
  scale-to-zero; pay-per-use; event-driven, Trade-offs: cold starts, execution limits,
  statelessness, lock-in, **Knative** brings serverless to K8s.

## 8. Cloud-native storage & data

- **Stateful on K8s** - Statefulsets, PersistentVolumes/Claims, **CST** for pluggable
  storage.
- **CLoud-native DBs** - distributed/elastic (CockroachDB, Vitess, Cassandra) + managed
  cloud DBs; operator pattern for running DBs on K8s.
- **Event streaming** - Kafka, Pulsar, NATS for async, decoupled communication.

## 9. Networking (CNI) & API gateways

- **CNI** - pluggable pod networking (Calico, Cilium, Flannel); **Cilium/eBPF** for
  high-perf networking/policy/observability.
- **Network policies** - micro-segmentation / zero-trust between pods.
- **API gateways** - Envoy, Kong, ingress controllers for north-south traffic, auth,
  rate-limiting.

# 10. Security (DevSecops)

- **Zero-trust**, **mTLS** everywhere (mesh), **RBAC**, least privilege.
- **Supply-chain** - image signing (Sigstore/cosign), SBOMs, provenance (SLSA), scanning
  (Trivy).
- **Policy as code** - OPA/Gatekeeper, Kyverno admission control.
- **Secrets** - Vault, sealed-secrets, external-secrets.
- **Runtime security** - Falco (eBPF threat detection).

## 11. The 12-Factor App(app-level discipline)

Codebase in VCS; explicit deps; **config in enu**; backing services as attached resources;
build/release/run separation; **stateless processes**; port binding; concurrency via
process scaling; **disposability** (fast start/graceful shutdown): dev/prod parity: **1ogs
as event streams**; admin tasks as one-off processes. The contract that makes services
orchestrator-friendly.

## Staff / Principal-level framing

- **Cloud-native is a coping strategy for distributed-systems complexity, not a free
  lunch.** Microservices swap in-process calls for network calls - gain independent
  scaling/deploy, inherit partial failure, eventual consistency, operational sprawl. Adopt
  when scale/org pressure justifies it; a modular monolith is often the right first step.
- **The declarative control-loop is the unifying idea.** K8s controllers, GitOps
  reconcilers, operators are the same pattern: observe actual, compare to desired, act to
  converge.
- **Observability is load-bearing, not optional** - you can't debug a distributed system
  by attaching to one process; design SLO-driven metrics/traces/logs in from day one.
- **Real costs:** sidecar CPU/latency (hence ePF/ambient), control-plane operational
  burden, cognitive load. Managed platforms (EKS/GKE/AKS, Fargate) trade cost/lock-in for
  less toil.
- **portability vs. lock-in is a deliberate axis.**K8s+ OCI OCI+ CNCF standards
  (OpenTelemetry/CSI/CNI) buy portability; deep proprietary serverless/managed use buys
  velocity at lock-in cost.
- **Statelessness is the enabler** of elasticity/self-healing - push state to backing
  services so compute stays fungible; managing state remains the genuinely hard part.

## Interview Q&A

**Q: What does "cloud-native" actually mean, beyond "runs in the cloud"?** A: It means
building and running applications to fully exploit the elastic, on-demand cloud model -
treating compute as fungible commodity rather than hand-nursed servers. The CNCF
definition centers on containers, microservices, immutable infrastructure, and declarative
APIs, enabling loosely coupled systems that are resilient, manageable, and observable,
with automation to make high-impact changes frequently and predictably with minimal toil.
*Follow-up:* What's the one-line mental model? Treat servers as cattle, not pets -
fungible, replaceable, declaratively managed. *Red flag:* "Cloud-native just means we
deployed it on AWS/a VM in the cloud."

**Q: What is the "declarative reconciliation loop" and why is it called the unifying idea
of cloud-native?** A: It's the control pattern where you declare desired state and a
controller continuously observes actual state, compares it to desired, and acts to
converge them. Kubernetes controllers, Gitops reconcilers (Argo CD/Flux), and Operators
are all the same pattern at different scopes - which is why self-healing, autoscaling, and
rolling updates all fall out of it naturally. *Follow-up:* How does an Operator extend
this? Via CRDs plus a custom controller that encodes operational knowledge (eg-, how to
run and recover a specific database) as a reconciliation loop. *Red flag:* Describing
Kubernetes as imperative run these commands" rather than declarative desired-state.

**Q: Microservices are a cloud-native pillar - what do they cost, and when are they worth
it?** A: They swap in-process calls for network calls: you gain independent
scaling/deployment, team autonomy, fault isolation, and polyglot freedom, but inherit
distributed-systems complexity - partial failure, network latency, data consistency, and
cross-service debugging. They're worth it past a scale/organizational threshold; below it,
a modular monolith is often the right first step, and much cloud-native tooling exists
precisely to manage the cost microservices introduce. *Follow-up:* What makes a service
orchestrator-friendly enough to scale this way? 12-factor discipline, especially stateless
processes, config in env, and disposability (fast start/graceful shutdown). *Red fLag:*
Advocating microservices by default for every project regardless of scale.

**Q: What is a service mesh, and why is the industry moving from sidecars to
eBPF/ambient?** A: A service mesh offloads cross-cutting network concerns - mTLS, traffic
routing (canary/A-B/mirroring), retries/timeouts/circuit-breaking, and telemetry - into a
data plane (sidecar proxies) governed by a control plane, so application code doesn't
implement them, The move to eBPF/ambient (Cilium, Istio ambient) is to cut the per-pod
sidecar CPU/Latency/memory overhead, which is a real cost at scale. *Follow-up:* Which
concern is the most common first reason teams adopt a mesh? Usually TS everywhere
for zero-trust, or uniform traffic shifting for canaries, "Red flag;* Thinking a mesh adds no
overhead, or confusing it with an API gateway (north-south).

**Q: Debugging scenario - a request works but is intermittently slow across several
microservices, How does cloud-native observability help find the culprit?** A: You use the
three pillars: metrics (Prometheus) to spot which service/endpoint's latency SLI degraded,
distributed tracing (OpenTelemetry/Jaeger) to follow the single request across service
boundaries and see which span dominates the latency, and structured logs correlated by
trace ID for the detail. You can't debug a distributed system by attaching to one process,
which is why observability must be designed in from day one, SLo-driven, *Follow-up:* What
standard is converging instrumentation across metrics/logs/traces? OpenTelemetry, *Red
flag:* Proposing to SSH into one box and tall a 1og as the primary approach.

**Q: Design trade-off - what does serverless/Faas buy you and what does it cost?** A: It
runs code without managing servers, with scale-to-zero, pay-per-use, and event-driven
execution - great for spiky or intermittent workloads. The costs are cold starts,
execution time/size limits, enforced statelessness, and vendor lock-in. Knative brings the
model to Kubernetes to mitigate lock-in. *Follow-up:* When is serverless a poor fit?
Steady high-throughput or latency-sensitive workloads where cold starts and per-invocation
pricing hurt, or long-running/stateful jobs. *Red flag:* Treating serverless as
universally cheaper with no cold-start or lock-in caveats.

**Q: What makes statelessness so central to cloud-native, and where does the hard part
go?** A: Statelessness is the enahler of elastiritu and self-healin - if comnite holds no
durable state, any instance can be killed, replaced, or scaled without data loss, keeping
it fungible. The hard part doesn't disappear; it moves to backing services - databases,
object stores, event streams - and managing that state (consistency, durability, scaling)
remains the genuinely difficult problem, handled with StatefulSets, PersistentVolumes/CSI,
and cloud-native/managed databases. *Follow-up:* How do you run stateful workloads on
Kubernetes at all? StatefulSets + PersistentVolumeClaims backed by CSI drivers, often with
an Operator encoding the DB's operational logic. *Red flag:* Claiming cloud-native
"solves" state, rather than pushing it to backing services where it stays hard.

## Synthesis

cloud-native builds apps to fully exploit elastic cloud infrastructure on four pillars -
**containers, microservices, immutable/declarative infrastructure, and automation** -
yielding resilient, scalable, observable, continuously-deliverable systems. Concretely:
**containers (OCI)** orchestrated by **Kubernetes** (declarative state, self-healing,
autoscaling, operators/CRDs), **microservices** wired via a **service mesh**
(NTLS/traffic/telemetry), managed by **IaC + Gitops** with **CI/CD + progressive
delivery**, made debuggable by **observability** (Prometheus/OpenTelemetry), extended by
**serverless (Knative/Lambda)**, **cloud-native storage/streaming (CSI/Kafka)**, **eBPF
networking (Cilium)**, and hardened by **zero-trust/supply-chain security (mTLS, OPA,
Sigstore, Falco)**, with **12-factor** discipline at the app level. The unifying idea is
the **declarative reconciliation loop**; the honest caveat is that all of it is machinery
to manage the distributed-systems complexity microservices introduce - powerful when scale
justifies it, over-engineering when it doesn't.
