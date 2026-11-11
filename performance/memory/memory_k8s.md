KI-- performance/memory/memory_k8s.md -->
# Kubernetes Memory
How Kubernetes requests/limits map onto Linux groups, how the scheduler and kubelet use them, and why pods get OOMKiled. JVM-specific container sizing is in
"Jvm_memory md"s the underlying group/reclaim mechanics are in
•memory_recycle.md" and "swap.md".
## Requests vs. Limits
Every container can declare resources. requests. memory" and
*resources.limits.memory"•
-   **Request** is your *budget* for scheduling. A worker node must have the requested amount available (based on the sum of all other pods requests already placed on it) before a pod can be scheduled there.
-   **Limit** is the hard ceiling enforced at runtime via the group ('memory max" on cgroup v2 / "memory.1imit_in_bytes" on v1). Exceed it and the kernel ooM-ki11s the container.
Why requests matter for scheduling: Kubernetes does not know that a pod named
"big-cassandra" needs more than one named "tiny-api". It is up to you to tell the scheduler what to expect once the pod grows to its full potential - hence treating the **request as your budget**.
#* QoS Classes
The kubelet derives a Quality-of-Service class from requests/11mits, and this drives eviction and OOM order:
-  **Guaranteed**-every containersets requests limits for both CPU and memory. Evicted last; most protected.
-   **Burstable** - at least one container has a request or limit, but not the Guaranteed condition. Evicted after BestEffort.
-   **BestEffort** - no requests or limits at all. Evicted first under node memory pressure.
Under the hood the kubelet sets each container's "oom_score_adj" based on Qos, so BestEffort pods are the kernel's preferred OOM victims. (See "oom_score_adj" in
"memory_recycle.md".)
#* working_set vs. RSS, and the OOM decision
The kubelet®s memory pressure and eviction decisions use ** working_set **, not raw RSS:
working_set - memory.usage_in_bytes- inactive_file (reclaimable page cache)
Roughly, working set = memory that cannot be trivially reclaimed. This is what
*kubectltop pod"reports and what the kubelet compares against the limit for eviction. The kernel's group OOM killer, however, acts when the group's charged usage hits "memory max".
- **OOMKilled** (exit 137) - the *kernel's* cgroup OOM killer killed a container because it hit its memory limit. Visible as reason: 00MKilled® in
*kubectl describe pod".
-**Evicted** -the *kubelet*evicted a pod because the node*was undermemory pressure (node-level, based on working set and eviction thresholds).
These are different mechanisms with different triggers - a common interview
distinction.
#* cgroup v2 Memory Controls
Kubernetes on modern distros uses group v2. Relevant knobs:
-   ***memory,max'** - hard limit; maps to the container's memory •limit. Exceeding it triggers the group OOM killer,
-   ** memory high"**-soft 1imit; the kernel aggressively reclaims and *throttles* the group above this, rather than killing. Kubernetes can set this below"memory.max (via the MemoryQos" feature) to smooth pressure instead of hardOOM,
-  ***memory min*/ memory, Low'**- reclaim protection (guaranteed/best-effortprotection of memory from reclaim),
-  ***memory.swap,max'**-caps swap usage for thegroup (see swap support below),
#* Swap Support
Historically kubelet refused to start if swap was enabled on the node, Swap support for Kubernetes workloads graduated through alpha/beta (NodeSwap" feature gate): on group v2 nodes with the feature enabled, Burstable Qos pods can be allowed 1imited swap, governed by group memory, swap,max", Guaranteed and BestEffort pods are generally not given swap. (General swap mechanics in
• swap.md".)
#Troubleshooting Workflow
1.
2.
"kubectl describe pod - 1ook for 'OOMK111ed* (exit 137) vs. "Evicted'.
*kubectl top pod" - compare working set against the limit.

1.    IfOoNkilled with low Java heap → off-heap growth; see "jvm_memory.md".
2.    Check the node: "kubectl describe node eviction events, node "free"/ vmstat".
3.    Inspect group directly on the node:
•cat /sys/fs/cgroup/.../memory.current", "memory.max", "memory.events" ('oom /oom_kill" counters).
6. Right-size: set request-typical working set, limit - peak+headroom for off-heap.
## Common Pitfalls
- Setting a memory **limit** without covering non-heap memory → oomkilled with a
"healthy" heap.
-   Confusing node **Eviction** with container **00MKilled**.
-   Reading RSS instead of working set when sizing.
-   BestEffort pods in production getting killed first under node pressure.
-   Assuming swap is available - it is off by default and gated.
## Likely Interview Questions
-   Difference between a memory *request* and *Limit*, and what each affects.
-   Explain the three Qos classes and their eviction/00M order.
-   ooMKilled vs. Evicted - which component acts and why?
-   What is "working_set" and how does it differ from RSS?
-   How do "memory.high" and "memory max" differ in cgroup v2?
-   How does Kubernetes handle swap today?
##References
-  https://rdzimmer.medium.com/memory-measurements-complexities-and-considerations-4e5125a7f12d
-   https://medium.com/expedia-group-tech/kubernetes-container-resource-requirements-part-1-nemory-a9fbe02c8a5f
-  https://mediun.com/@senthilrch/woot-kubernetes-adds-support-for-swap-memory-92541aad01a0
-   https://github.com/kubernetes/kubernetes/issues/43916
-   kernel Documentation/admin-guide/cgroup-v2.rst*; Kubernetes docs on Qos and node-pressure eviction.

