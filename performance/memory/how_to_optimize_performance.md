<!-- performance/memory/how_to_optimize_performance.md -›
# How to Optimize Memory Performance
A checklist of memory optimization strategies for Linux systems and the reasoning behind each. These are system/application-level levers; topic-specific detail lives in the sibling files referenced below.
## Optimization Strategies
1.    **prefer to disable Swap,** If swap must stay on, lower *vm.swappiness" to reduce the kernel's tendency to swap anonymous pages during reclaim. (Details and the group v2 swap controls in "swap.md".)
2.    **Reduce dynamic memory allocation. ** Use memory pools and/or huge pages (HugePage) to cut per-allocation overhead and TB pressure. (Mapping and huge pages in "memory mapping-md.)
3.    Use caches and buffers deliberately.** Declare fixed buffers (e.g. on the stack, or pre-allocated arrays) for data you cache, or offload caching to an external component such as Redis to optimize data access patterns.
4.    **Limit per-process memory with cgroups.** Cap memory so a misbehaving process cannot exhaust the whole system. On group v2 use "memory high (soft throttle) and "memory max" (hard limit). (Container specifics in memory_k8s.md.)
5.    **protect critical apps from the OOM killer** via
* /proc/<pid›/oom_score_adj (range **[-1000, 1000]**; *-1000° effectively exempts). This keeps core services alive under memory pressure. (The legacy
"oom_adj knob is deprecated.)
## Applying These in Order
1.    **Neasure first** - •free -w, "vmstat 1, "pidstat -r 1°. Know whether you are cache-rich-but-fine or genuinely under pressure. (See"performance_indicators_metrics.md".)
2.    **Attribute** the pressure to a process or to the kernel (slab).
3.    **Fix the root cause** (leak, over-allocation) before reaching for tuning knobs - see "memory_leak.md".
4.    **Then tune*: swappiness, group limits, huge pages, allocator choice.
5.    **Guard"* critical processes with "oom_score_adj and verify with a load test.
## Common Pitfalls
-   Turning knobs (swappiness, THP) before confirming the real bottleneck.
-   Disabling swap on a memory-constrained box, turning slow degradation into hard | ooM kills.
-   Setting huge pages for workloads that do not benefit, wasting reserved memory.
-   Capping "memory.max" too aggressively, causing in-container OOM kills that look like app crashes.
# Likely Interview Questions
-   When would you lower " swappiness", and what does it actually control?
-   How do huge pages improve performance, and when do they hurt?
-   How do you stop a critical process from being OOM-killed?
-   group v2 "memory.high" vs "memory max' - what is the difference?
-  What is the first thing you measure before optimizing memory?
## References
-   "man 5 proc" - "oom_score_adj".
-   kernel Documentation/admin-guide/group-v2.rst* - memory controller.
-   kernel "Documentation/admin-guide/sysct1/vm.rst* - "swappiness.
