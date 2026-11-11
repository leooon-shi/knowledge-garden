- pertormance/memory/bcc_tools.md -
# BCC / bpftrace Memory Tools
The ePF-based tool set (BCC and bpftrace) for observing Linux memory behavior with low overhead on a running system - page-cache hits, allocation leaks, page faults, oM kills, and swap activity, Topic-specific usage is cross-referenced to sibling files.
## Nhy eBPF Tools
Traditional tools ("free,
"vmstat, "pidstat®) tell you that* memory is under
pressure; ePF tools tell you where" and why*by attaching probes to kernel functions, tracepoints, and user-space "malloc/' free'- safely, in production, with minimal overhead and no recompilation of the target.
Two front-ends:
- **BCC** - Python/C tools, installed as •*-bpfec* on Debian/Ubuntu (e.g. cachestat-bpfcc"). Package: "bpfec-tools.
**bpftrace** - a high-level tracing language for quick one-1iners.
Most require root (or "CAP_BPF / CAP_PERFMON) and kerned headers/BTF. This host runs kernel 6.8, which has BTF and supports the full tool set.
* Page Cache: cachestat / cachetop
Heasure page-cache hit ratio (detail and output columns in
•buffer_cache.md").
* System-wide cache hits/misses per second $ cachestat-bpfce 1
HITS MISSES DIRTIES HITRATIO BUFFERS_MB CACHED_MB
652
233241
# Per-process cache activity
$ cachetop-bpfcc
PID
UID
CMD
12345 root mysqld 40321
1325 MISSES DIRES REAMSTATT
Use these when repeated reads are slow or the working set may not fit in cache.
## Leak Detection: memleak
Reports outstanding (un-freed) allocations with stacks (full workflow in
*  memory_leak.md®).
*   Trace a running PID, print top outstanding allocations every 5 s $ memleak-bpfcc -p 4242 5
[13:40:01] Top 10 stacks with outstanding allocations:
48969 bytes in 1e allocations from stack my parse_record+8x3f [myapp]
handle_request+0x1a2 [myapp]
Also traces kernel allocations when run without •-p°.
## Allocation Churn / Latency: a quick bpftrace one-liner
Histogram of user-space allocation sizes for a process:
$ bpftrace -e 'uprobe:/lib/x86_64-1inux-gnu/libc.so.6:malloc { @bytes - hist(arge): 3°
@bytes:
[16, 32)
132,
64)
1203
402
## Page Faults
Attribute minor/major faults to processes and code:
• BCC: trace page faults
$ profile-bpfcc-e page-faults -p 4242
* bpftrace: count major faults by process
%.bpftrace -e software: major-faults: & OLcomm) = count): t°
Major faults mean disk/swap involvement (see
"performance_indicators metrics.md").
## O0M Kills
observe OOM-kJl1 events 11ve, with the victim and context:
$ oomk111-bpfcc
Tracing OOM kil1s... Ctr1-C to stop.
13:51:22 Triggered by PID 2321 ("stress"), OOM k111 of PID 2319 ("python"),
1215999 pages, loadavg: 2.1 1.8 1.2
...
Pairs with the reclaim path in "memory_recycle.md".

## Swap Activity
Trace swap-ins/outs by process:
$ bpftrace -e kprobe:swap_readpage @swapin[comm] - count(): y°
(See "swap.md" for interpreting swap pressure.)
## Slab / Kernel Memory
٠٠٠
* Kernel slab allocations by cache $ slabratetop-bpfce
* top slab caches by alloc rate
Useful when 'sunreclaim/slab in /proc/meminfo grows (kernel object leak).
## Tool Map (by symptom)
| Symptom | Tool | Sibling doc |
-a----|------|-------------1
I Slow repeated reads | cachestat, cachetop | "buffer_cache.md |
Growing RSS/ leak I"memleak"| memory_leak.md |
High major-fault rate | profile, bpftrace faults | °performance_indicators_metrics.md
OOM kills | oomkill | memory_recycle.d |
Swap thrash | bpftrace swap probes | "swap.md" |
Kernel/slab growth Islabratetop "memory_mapping.nd |
## Troubleshooting Workflow
1.    Start with coarse tools (free -w, "vmstat®) to classify the problem.
2.    Pick the matching eBPF tool from the table above to localize it.
3.    Capture stacks (memleak, "profile) to reach the exact code/kernel path.
4.    Keep runs short in production and prefer per-PID filtering to limit overhead.
## Common Pitfalls
-   Missing kernel headers/BTF or debug symbols → tools fail or show raw addresses.
-   Forgetting the •-bpfcc" suffix for BCC tools on Ubuntu/Debian.
-   Running without root/" CAP_BPF* .
-   Leaving a heavy trace running system-wide instead of filtering by PID.
-   Assuming eBPF is zero-overhead - it is low, not nil; filter aggressively.
## Likely Interview Questions
-   Why use ePF tools over "free / vmstat/pidstat"?
-   Which tool would you reach for to find a leak in a running C service?
-   How do you measure page-cache hit ratio per process?
-   How can you observe ooM kills as they happen?
-  What are the prerequisites (kernel features, privileges) for BCC tools?
## References
-   ¿ovisor/bec project - tool reference ('cachestat", "cachetop", "memleak", | "oomki11", "slabratetop"),
-   Brendan Gregg, *BPF Performance Tools* (memory chapter).
-   bpftrace reference guide,
