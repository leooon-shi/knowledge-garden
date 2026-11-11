* pertormance/memory/buffer_cache.md -->
# Buffers vs. Cached, the Page Cache, and Dirty Writeback
memory_k&
how dirty pages get written back, and how to measure cache effectiveness with
## Buffers vs. Cached
th are kernel-managed caches of disk data, counted in free //proc/meminfo d both are reclaimable. The modern distinctior
-   **Cached (page cache** - caches **file contents** addressed by (inode, offset).When you read or write a file, its pages live here. This is the big number on most systems.
-   **Buffers (buffer cache)** - caches **raw block-device metadata and block 1/0** (filesystem metadata, superblocks, and direct block-device access). Since the 2.4 kernel unified the buffer and page caches,
*Buffers" is small and mostly
block/metadata; file data goes through "Cached".
• Verified on this host (Ubuntu 24.04, props-ng): "-w" splits the two $ free -W
Mem:
Swap:
From "/proc/meminfo:
Buffers:
Cached:
SwapCached:
Dirty:
Writeback:
652176 kB
23324932 kB
0 кв
60 kB
0 кв
*   block/metadata buffer cache
*   file page cache (minus swap cache)
*  modified pages not yet written back
*   pages currently being written to disk
A simple mnemonic: **Buffers = blocks**, **Cached = file content**. Both are "free
memory the kernel is using productively" and will be given back under pressure.
# The Page Cache
Nearly all file I/0 goes through the page cache:
-   A **read** checks the cache first (a hit* avoids disk); a miss* reads from disk into the cache, then copies to the process. This is why re-reading a file is fast and why "first run slow, second run fast".
-   A **write** (buffered) updates the cached page and marks it **dirty**; the actual disk write happens later via writeback.
•O_DIRECT bypasses the page
cache entirely (every access hits the disk - see the I/0 example in
•../cpu.md").
The page cache is the main reason Linux "free" memory looks low and why you should watch available, not "free (see "memory_usage.md").
## Dirty Pages and Writeback
Dirty pages are flushed to disk by kernel writeback threads, controlled by:
-   ** vm.dirty_background_ratio*** / ***vm. dirty_background _bytes ** - when dirty memory exceeds this, writeback starts in the background* (async, no app stall).
-   ** vm.dirty_ratio***/** vm.dirty_bytes*** - when dirty memory exceeds this, a writing process is *throttled* and must do synchronous writeback (app stalls).
-   **ym.dirty_expire_centisecs***/*** vm.dirty writeback_centisecs ** - age-based flushing and how often the flusher wakes.
$ sysctl vm.dirty_ratio vm.dirty_background_ratio
vm.dirty_ratio = 20
vm.dirty_background_ratio = 10
...
Tuning these trades off write latency smoothness vs, the amount of data at risk on a crash, Force a flush with sync,
# drop_caches (diagnostic only)
You can drop cache to observe cold-cache behavior:
•••
* 1 - page cache, 2 - dentriestinodes, 3 - both. Requires root. NOT a fix. $ sync && echo 3 | sudo tee /proc/sys/vm/drop_caches
This is for benchmarking/diagnostics only - the kernel reclaims cache on its own, and dropping it just forces re-reads from disk later.
# Measuring Cache Effectiveness: cachestat / cachetop
bcc tools (see "bcc_tools.md") measure page-cache hit ratio:

٠٠٠
* System-wide page-cache hits/misses per second $ cachestat-bpfcc 1
HITS
MISSES
53401
٠٠٠
22
DIRTIES HITRATIO
28
99.96%
BUFFERS_MB
652
CACHED MB
23324
-   **HITS / MISSES** - page-cache lookups served from cache vs. from disk.
-   **DIRTIES** - pages newly marked dirty this interval.
-   **HITRATIO**- the headline number; a falling ratio under a read workload means the working set no longer fits in cache.
٠٠٠
* Per-process cache activity $ cachetop-bpfcc
PID
12345
UID
CMD
root mysqld
HITS
40321
MISSES DIRTIES READ_HIT WRITE_HIT%
12
99.9%
0.1%
## Troubleshooting Workflow
1.    "free -w+"/proc/meminfo® - is memory "cache" (fine) or app RSS (watch it)?
2.    Slow repeated reads? → 'cachestat/ cachetop for hit ratio; a low ratio means cache thrash or a too-small cache.
3.    Periodic write stalls? → check "Dirty / Writeback" in /proc/meminfo and the
•vm.dirty_" thresholds; the appmay be hitting "dirty_ratiothrottling.
4. Suspect direct I/o bypassing cache? → "strace for "O_DIRECT* (see ./cpu.md).
1# Conmon Pitfalls
-   Thinking high "Cached is a leak - it is reclaimable and healthy.
-   Using •drop_caches in production as a "fix".
-   Confusing "Buffers" (blocks/metadata) with "Cached (file content).
-   Ignoring "dirty_ratio throttling as a cause of latency spikes in write-heavy apps.
## Likely Interview Questions
-   Difference between 'Buffers" and Cached in Linux.
-   Walk through a buffered read and a buffered write through the page cache.
-   What are dirty pages and what controls when they are written back?
-   What does "drop_caches" do, and why isn't it a fix?
cony a t at a fae written bee cache
-   How do you measure page-cache hit ratio per process?
-   Why does "O_DIRECT change the picture?
#* References
-   kernel "Documentation/admin-guide/sysct1/vm.rst* - "dirty_* knobs, | "drop_caches,
-   bec tools; "cachestat", "cachetop".
-   Linux "proc(5)* - "/proc/meminfo ("Buffers", "Cached", "Dirty", "Writeback").
- 
