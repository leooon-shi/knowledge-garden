Kl--performance/memory/memory_usage.md -->
# Measuring Memory Usage
The practical tools for reading system and per-process memory usage on Linux:
*free, top / ps, and /proc/meminfo, and how to interpret every column.
## free: system memory and swap
*  free shows physical memory ("Men") and swap ("Swap") usage. Output varies by procps version; modern versions support "-w" to split buffers from cache.
*   Verified on Ubuntu 24.04 / procps-ng, "free -w" splits buffers and cache $ free -Wtotal
Men:
Swap:
32866224
free
6337240
shared buffers
cache
available
5652
652176 24282092
30787172
Without "-w*, buffers and cache are combined into one "buff/cache" column:
7611064
Columns:
-   **total** - total installed memory.
-   **used** - memory in use (includes shared memory; modern "free computes it as
*  total - free - buffers - cache®).
-  **free** - completely unused memory.
-   **shared** - shared memory (mostly " tmpfs").
-   **buffers"* - block-device buffer cache (data queued for/from raw disk blocks).
**cache** - page cache for file contents, plus reclaimable slab.
- *available» - an estimate* of memory available to new processes without swapping. It includes free memory **plus** reclaimable cache, so it is normally larger than free". This is the number to watch, not "free.
## top / ps: per-process memory
• Press "N" in top to sort by memory
KiB Mem : 8169348 total, 6871440 free, 267096 used, 1030812 buff/cache KiB Swap:
e total,
• free,
e used. 7607492 avail Men
PID USER
430 root
1875 root
1048 root
1 root
PR
19
20
20
20
NI
-1
VIRT
RES
SHR 5 %CPU %MEM
122360
35588 23748 S
0.0
0.4
771860 22744 11368 S
0.0
0.3
170904 17292
9488 S
78020
9156
0.0
0.2
6644 S
0.0
0.1
TIME+ COMMAND
0:32.17 systemd-journal
0:38.89 snapd
0:00.24 networkd-dispat
8:22.92 systemd
The memory columns:
1.    **VIRT** - total virtual memory the process has requested. Counts *any* memory the process has asked for, even if no physical page has been allocated yet.
2.    **RES** (Resident Set Size) - the non-swapped physical memory the process is actually using right now. Excludes swapped-out and (counts but shares) shared pages,
3.    **SHR** - shared memory: memory shared with other processes, loaded shared libraries, and the program's own code/text segment. Note that 'SHR is not necessarily shared* in practice - a program's code segment and private (non-shared) libraries are counted here too.
4.    **%MEN** - RES as a percentage of total physical memory.
*   Caveat: summing "RES" across processes over-counts, because shared pages (11bc,
*   shared mappings) are counted in every process. For a true per-process footprint
*   use **pSS** (Proportional Set Size) from '/proc/<pid>/smaps* or "smem".
•ps gives the same data as a scriptable snapshot;
3 ps -eo pid, comm, sz, r55,%mem--sorts-pss | head
- "VS' = VIRT, "rss' = RES (both in KB),
##/proc/meminfo: the source of truth
"free" and top" are summaries of /proc/meminfo, Key flelds:
$ head -30/proc/meminfo
MemTotal:
32866224 kB
MemFree:
6343064 kB
MemAvailable:
30793064 kB
Buffers:
652176 kB
Cached:
23324932
kB
SwapCached:
0 кв
Active:
10939004 kB
* basis for free's "available"


Inactive:
•••
14216860
AnonPages:	1175620	
Mapped:	401240	kB
Shnem:	5652	KB
KReclaimable:	957228	KB
Slab:	1173280	KB
SReclaimable:	957228	kB
SUnreclain:	216052	KB
PageTables:	18056	
*   anonymous (heap/stack) pages
*   tmpfs/shared memory
• reclaimable slab (counted in "avallable")
* memory used by page tables themselves
- **MemAvailable** - kernel's oun estimate of allocatable memory; "free''s
"available column derives from it.
**Active/Inactive** - LRU lists used by reclaim (see
"memory recycle.md").
-   **SReclaimable** - slab the kernel can reclaim under pressure, which is why it counts toward available.
-  **PageTables** - grows with many processes / large sparse mappings.
#* Troubleshooting Workflow
1.    "free -w' - is 'available shrinking, or is it just cache (which is fine)?
2.    "mstat 1" - watch "si"/'so* (swap in/out). Non-zero sustained swap - pressure.See *swap.md".
3.    "top sorted by "%MEN", or ps --sort--rss, to find the heaviest process.
4.    "pidstat -r 1 ‹pid› - watch •minflt/s vs "majflt/s.
5.    If a process keeps growing, suspect a leak → "memory_leak.md".
6.    For cache vs. app memory confusion, check "cachestat / cachetop" →"buffer_cache.md".
# Common Pitfalls
-   **"Low free memory" is usually fine** - Linux uses free RAM for page cache. Look at "available, not "free.
-  **Summing RES double-counts shared pages** - use PSS for real footprint.
-   **VIRT is meaningless as a pressure signal** on its own.
-   "used" in old vs. new •free is computed differently (old versions folded cache into "used"), so cross-version comparisons mislead.
## Likely Interview Questions
-   What is the difference between •free" and available in "free''s output?
-   Explain VIRT VS. RES vs. SHR vS. PSS.
-   Why is low "free" memory not necessarily a problem on Linux?
-   Which /proc/meminfo fields feed "MemAvailable®?
-   How would you find the process with the largest real memory footprint?
## References
-   "man free, "man top, "man ps", "man vmstat", "man pidstat".
-   Linux "proc(5)" man page - /proc/meminfo, /proc/<pid›/smaps.
-   kernel *Documentation/filesystems/proc.rst® - MemAvailable definition.


