<1-- performance/memory/performance_indicators_metrics.md --›
# Memory Performance Indicators and Metrics
A reference for the memory metrics a Linux performance engineer tracks, split into machine-level and process-level signals, plus the tools that expose them.
#* Machine-Level Memory Indicators
2.
*  *Used and free memory»* - memory already in use vs. not yet used.
*  *Shared memory** - implemented via tmpfs", so its size equals the "tmpfs" usage.tmpfs is itself a special kind of cache.
3. * Available memory** - the maximum a new process can use; it is free memory
**plus** reclaimable cache. (See "memory usage.md" for "NemAvailable* .)
4. **Cache** has two parts:
-   the **page cache** of file data read from disk, which speeds up subsequent reads of the same data; and
-   **reclaimable slab** ("SReclaimable) in the slab allocator.
5. **Buffer** - temporary storage for raw disk blocks, caching data *about to be written* to disk so the kernel can coalesce scattered writes into efficient sequential I/O. (Buffers vs. Cached is covered in "buffercache. md".)
#*Process-Level Memory Indicators
1.    *Virtual memory (VIRT)** - includes the process's code and data segments, shared memory, heap already requested, and memory already swapped out. Requested memory counts even if no physical page has been allocated yet.
2.    **Resident memory (RES)** - the physical memory the process actually uses; excludes swap and (shares) shared memory. This is the process's real memory usage.
3.   **Shared **Shared memory (SHR**- real shared memory used with other processes, plus loaded shared libraries and the program's code segment.
4.    **Swap memory** - memory that has been swapped out to disk.
#* Page Faults
**Minor fault** - the page can be satisfied directly from physical memory (e.g. first touch of a freshly allocated page, or a page already in the page cache).
Cheap.
**Major fault** - servicing the fault requires disk I/0 (e.g. swap-in, or reading a file-backed page not yet cached). A rising major-fault rate means more disk I/0 and much slower memory access.
Watch these with pidstat-r 1° (minflt/s, "majflt/s") or ps -o minflt,maj_flt®.
## Memory Performance Tools
1.    ***free,"top, "ps'** - baseline system and per-process usage.
2.    ***vmstat s* - dynamic view of memory change; distinguishes buffer vs. cache and swap-in vs. swap-out sizes.
3.    " cachestat" / "cachetop"** (bcc) - "cachestat" shows system-wide page-cache hit/miss; "cachetop" shows per-process cache hit/miss. See
-buffer_cache.md® and •bcc_tools.md".
4.
**Memory-leak hunting** - use "vmstat" to spot steadily growing usage, then
" memleak* (bcc) to confirm a leak; "memleak prints the allocation stack that points at the leak site. See "memory_leak.md".
S.
*sar-r*/sar-w** -historical trend of memory, buffers, and swap. Compare free memory against the zone watermarks in /proc/zoneinfo to tell whether rising swap is caused by reclaim.
6, "*Deeper tools** - "valgrind" (memcheck/massif) for leak and heap profiling;
"bpftrace /SystemTap for custom dynamic tracing.
# Troubleshooting Workflon
1.    Baseline with "free -w and " vmstat 1: is available falling and is swap active (si"/50")?
2.    Attribute to a process; "top" sorted by
"%MEM", or •pidstat -r 1°.
1.    Fault profile: high 'majflt/s → swap/disk involvement; high "minfit/s alone •just allocation churn,
2.    Separate cache from app memory; " cachetop" to see who drives the page cache.
3.    If usage grows unbounded leak hunt (memleak",
"valgrind").
If swap is rising → check reclaim (/proc/zoneinfo, "kswapd" activity) and
" swappiness ,
## Common Pitfalls
-   Treating a high •buff/cache as a problem - it is reclaimable and healthy,
-   Watching"free instead of available,
-   Ignoring the minor/major fault distinction when diagnosing "slow memory".
-   Summing per-process RES and over-counting shared pages (use PSS).
## Likely Interview Questions
-   Differentiate buffer vs. cache in Linux memory accounting.
-   What is the difference between a minor and a major page fault?
-   Which tools would you chain to confirm and localize a memory leak?
-   How do you tell whether rising swap is due to reclaim pressure?
-   What does available" memory include that "free" does not?
## References
- bec tools: "cachestat,
- "man vmstat", "man sar"» cachetop",
"man pidstat, "man free.
"memleak.
- Linux proc（S） - /proc/meminfo， proc/zoneinfo
