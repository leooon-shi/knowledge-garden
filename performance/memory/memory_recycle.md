<1-- performance/memory/memory_recycle.md -->
# Memory Reclaim (Page Reclaim, kswapd, Watermarks, OOM)
How Linux reclaims physical memory under pressure: the LU lists, watermarks and
•kswapd", direct reclaim, and the OOM killer of last resort. Swap tuning is in
*swap.md"; buffer/page cache basics are in "buffer_cache.md".
## Why Reclaim Exists
Physical memory is finite. Linux deliberately uses "free" RAM for page cache, so when a new allocation cannot be satisfied from free pages, the kernel must
*reclaim** pages - evict clean cache, write back dirty pages, or swap out anonymous memory - to make room. If reclaim cannot keep up, the OOM killer fires.
## The LRU Lists
The kernel tracks reclaimable pages on per-node (per-memcg) LRU 11sts, split by type and activity:
-   **Anonymous** pages (heap/stack, no backing file) - can only be reclaimed by swapping them out. If there is no swap, anon pages are effectively unreclaimable.
-   **File-backed** pages (page cache) - clean file pages can be dropped for free; dirty ones must be written back first.
Each type has an **active** and **inactive** list (Active(anon),
*Inactive(anon)*, "Active(file), Inactive(file) in °/proc/meminfo). Pages are demoted from active → inactive as they age; reclaim takes from the inactive tail.
A second access promotes a page back to active. This two-list scheme approximates
LU while resisting one-off scans flushing the working set.
#*# MGLRU (Multi-Gen LRU)
Kernels 6.1+ include **Multi-Generational LRU**, which replaces the two active/inactive lists with multiple aging generations per type. It tracks page
"age"more finely, improving reclaim decisions and reducing CPU overhead under pressure. Enable/inspect via /sys/kernel/mm/lru_gen/*. (This host runs 6.8, where MGLRU is available.)
## Natermarks and kswapd
Each memory zone has three watermarks: **min**, **low**, and **high** (see
-Iproc/zoneinfo, fields "min"/"low /high; tunable base vía
*vm.min_free_kbytes®).
- Free memory drops below **low** → the kernel wakes the per-node **"kswapd'** kernel thread to reclaim in the *background* until free rises back above
**high**. Allocations keep succeeding meanwhile.
- Free memory hits **min**→allocating tasks enter **direct reclaim**: the faulting process itself reclaims synchronously and stalls until it frees enough pages. Direct reclaim is the painful, latency-inducing path.
So the healthy state is "kswapd quietly keeps free above low"; the bad state is
"everyone is in direct reclaim".
## Dirty Pages and Writeback
Reclaiming a dirty file page requires writing it back first. The writeback behavior is governed by
*   vm.dirty_ratio® / vm.dirty_background_ratio" (and the
*  *_bytes variants). See "buffer_cache.md" for "drop_caches and dirty writeback detail,
## swappiness
•vm. swappiness*
biases reclaim between evicting file cache and swapping anonymous pages. Lower -
prefer dropping file cache; higher = more willing to swap anon. It is a ratio*,
not an on/off switch, Details and group swap controls in "swap.md".
# NUMA and zone reclaim
On NUMA systems,
"vm, zone _reclaim mode controls whether the kernel reclaims
locally before allocating from a remote node, Aggressive local reclaim can cause surprising reclaim activity even with free memory on other nodes.
## The OOM Killer
When reclaim cannot free enough memory, the **OOM killer** chooses a victim to kill:
- Each task gets an **oom_score ** derived mainly from its memory footprint (adjusted by
*oom_score_adj").
-**/proc/<pid>/oom_score_adj*** ranges **[-1000, 1000]**•-1000* effectively exempts a process; higher values make it a preferred victim. (The legacy
*oom_adj, range [-17, 15], 1s deprecated.)
- With group v2, each group has its own OOM behavior when it hits " memory,max"; memory.oom.group-1" ki11s the whole group together. "memory,events" exposes
'oom/oom kill counts.
Inspect kills in the kernel 10g:
$ dmesg | grep -i -E 'oom|killed process•

Out of memory: Killed process 12345 (python) total-vm: ..., anon-rss:....
٠٠٠
## Troubleshooting Workflow
1.    "vmstat 1" - watch si/so* (swap) and free: sustained non-zero swap + high | 'sr (scan rate via "sar -B*) - reclaim pressure.
2.    "sar -B 1" - "pgscank/s* (kswapd scan) vs 'pgscand/s' (direct-reciaim scan).
| Direct-reclaim scanning is the red flag.
1.    Compare free memory to zone watermarks! cat /proc/zoneinfo | grep -E "min|low/high'*,
2.    "cat /proc/meminfo - Active/Inactive split, Dirty/Writeback.
3.    "dmesg" / "memory.events" - any OOM kilis?
4.    Mitigate: add memory, lower "swappiness', tune "min_ free_kbytes, f1x the leak (memory_leak.md"), or protect critical proes via "oom_score_adi".
## Common Pitfalls
-  **No swap means anon pages are unreclaimable** - pressure goes straight to 004.
-   ***drop_caches is a diagnostic, not a fix* - the kernel reclaims cache on its own; forcing it just re-reads from disk later.
-   Confusing background **kswapd** reclaim (fine) with **direct reclaim** (bad).
-   Assuming high "scan rate" always means trouble - brief bursts are normal.
#* Likely Interview Questions
-   Walk through what happens as free memory crosses the low then min watermark.
-   kswapd (background) vs. direct reclaim - latency implications?
-   Why can a box with "free" cache still OOM if it has no swap?
-   How does the OOM killer pick a victim, and how do you protect a process?
-   What is MGLRU and why was it introduced?
-   What does "swappiness" actually control?
## References
-   kernel "Documentation/admin-guide/sysct1/vm.rst" - "swappiness, I "min_free_kbytes, "zone reclaim_mode, dirty ratios.
-   kernel Documentation/admin-guide/mm/multigen_1ru.rst®.
-   Linux •proc(5)" - "/proc/zoneinfo, "/proc/meminfo, "oom_score_adj".
-   "man sar* (-B" paging statistics).
