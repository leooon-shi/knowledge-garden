• memory_interview_answers.md ....
<!-- performance/memory/memory_interview_answers-ma -→>
# Memory - Interview Question Answers
Answers to the "Likely Interview Questions" across the files in
• performance/memory/. Organized per source file.
G arm_check.cp
## swap. md
**Which pages can be swapped, and which cannot?**
Only **anonymous** pages (heap, stack - memory with no backing file) can be swapped out to a swap device/file. Clean **file-backed** pages are never swapped"; they are simply dropped and re-read from their file on demand. Dirty file pages are written back to their file (not swap). Pages that are *Locked* (mlock"), kernel-reserved, or otherwise pinned cannot be reclaimed/swapped at all.
**Does
No.
swappiness=0* disable swap? What does swappiness really control?**
*vm. swappiness" is a *bias* (ratio), not an on/off switch. It tunes how the kernel splits reclaim between **evicting file/page cache** and **swapping anonymous pages**, Range is 0-200 on modern kernels (historically 0-100); default
60. Lower = prefer dropping page cache; higher = more willing to swap anon.
•swappiness-0" only makes the kernel avoid anon swapping until it is nearly the only option - to truly disable swap use swapoff -a (or "memory.swap.max=f" per cgroup).
w#zram vs.
zswap - explain the difference.**
Both compress pages in RAM, but differently:
-   **zram** = a compressed block device in RAM used *as* a swap device. Swappedpages are compressed and kept entirely in memory, no disk. RAM-only.
-   **zswap** = a compressed *cache in front of* a real on-disk swap device. Pagesare compressed into a RAM pool first; only when the pool fills are they written out to the backing disk. Reduces swap I/0 while keeping a disk fallback.
They are used one or the other on a given path, never stacked.
*  *Is non-zero used swap a problem? What metric tells you swapping hurts?** Used swap by itself is **not** a problem - the kernel may have swapped idle pages long ago and never touched them again. What matters is *active* swapping: sustained non-zero "si / so in vmstat 1° (especially " si", swap-in, which is a major fault from disk), or •pswpin/s /pswpout/s* in 'sar -W. That ongoing traffic is what adds latency.
*  *How do you disable swap for a single group/container?**
Set **memory. swap-max-0*** for that group (cgroup 2). This disables swap for just that group while the rest of the system can still swap. Kubernetes uses this to gate swap per pod/Qos.
**Why can disabling swap make OOM *more* likely?**
Without swap, **anonymous pages become unreclaimable** - the kernel cannot evict heap/stack memory anywhere. Under pressure there is no graceful-degradation path, so instead of slowing down (paging), the kernel jumps straight to the **0OM killer**, Swap converts a hard OOM into a (slow) soft landing.
## buffer_cache.md
**Difference between "Buffers" and "Cached".**
Both are reclaimable kernel caches of disk data. **Cached** is the *page cache* - file contents keyed by (inode, offset); the big number on most systems.
**Buffers** is the block/buffer cache - raw block-device I/0 and filesystem metadata (superblocks, direct block access). Since the 2.4 kernel unified the two,
"Buffers" stays small (blocks/metadata) and file data flows through "Cached".
Mnemonic: **Buffers = blocks, Cached = file content.**
**Walk through a buffered read and a buffered write through the page cache.**
-   *Read*; check the page cache; a hit returns from RAM (no disk). A miss reads from disk into the cache, then copies to the process, This is why re-reads are fast.
-   Write* (buffered): update the cached page and mark it **dirty**; "write() returns immediately. The actual disk write happens later via writeback threads.
*  O_DIRECT bypasses all of this and hits the device directly,
*  *What are dirty pages and what controls when they are written back?** Dirty pages are modified cached pages not yet persisted, Writeback is controlled by:
•vm.dirty_background_ratio*/_bytes* (background async flush starts),
"vm.dirty_ratio"/_bytes* (a writing process is *throttled* and flushes synchronously - causes stalls), and •vm.dirty_expire_centisecs' /
• vm.dirty_writeback_centisecs (age-based flushing and flusher wake interval).
*  sync forces a flush.
*  *What does "drop_caches do, and why isn't it a fix?**
*   echo 1|2|3 > /proc/sys/vm/drop_caches drops page cache (1), dentries+inodes (2), or both (3). It is a **diagnostic/benchmarking** tool to observe cold-cache behavior. It is not a fix because the kernel already reclaims cache automatically under pressure; manually dropping it just forces expensive re-reads from disk later..
*  *How do you measure page-cache hit ratio per process?**
*  cachetop (bcc) for per-process READ_HIT% / WRITE_HIT%;
cachestat (bcc) for
the system-wide "HITRATIO. A falling hit ratio under a read workload means the working set no longer fits in cache.


*  *Why does "O_DIRECT" change the picture?**
*  O_DIRECT bypasses the page cache entirely - every access goes straight to the device (DMA to/from user buffers). So caching/read-ahead don't help, repeated reads don't get faster, and the page cache hit ratio is irrelevant. Databases use it to avoid double-caching when they manage their own buffer pool.
## memory_mapping-md
**What happens, step by step, on a page fault?**
The CPU's MMU tries to translate a virtual address and finds no valid page-table entry, so it raises a page fault trap into the kernel. The kernel's handler decides: if it is a valid-but-unbacked address (e.g. first touch of malloc'd memory) it allocates a physical page, updates the page table, and returns -
*  *minor fault**. If the data must come from disk (swap-in, or an uncached file-backed page) it issues I/0 first - **major fault**. If the address is invalid it delivers SIGSEG. After fix-up the faulting instruction is retried.
*  *Minor vs. major page faults and their cost.**
Minor = satisfied from RAM (new page, or page already in cache) - cheap. Major =
requires **disk I/0** (swap-in or reading a file page not yet cached) - expensive, orders of magnitude slower. Watch with •pidstat -r 1° (minflt/s vs "majfit/s): rising majors mean disk/swap involvement.
*  *Why does "malloc" not allocate physical memory immediately?** malloconly reserves virtual* address space (and asks the kernel to extend the heap via "brk / mmap*). Physical pages are backed lazily, on first **access**, via a (minor) page fault - demand paging. This is why VIRT can be huge while RSS is small, and why RSS lags behind allocation.
*  *when does glibe use "brk" vs "mmap", and why does it matter?»* Small allocations (< ~128 KB, the dynamic "MMAP_THRESHOLD°) use ***brk()*** - moving the heap top; freed memory is cached in the heap for reuse (not returned to the 0S), reducing faults but risking fragmentation and keeping RSS high after
• free(). Large allocations (> ~128 KB) use ***mmap()*** directly; freeing returns the memory to the kernel, so each new region faults fresh. "mmap"-heavy churn causes page-fault storms and more kernel overhead.
**What is the TB and how do context switches affect it?**
The **T** (Translation Lookaside Buffer) is a small, fast cache in the MMU of recent virtual→physical page-table entries. A context switch to a different address space can flush/invalidate TB entries, so the next accesses miss the TLB and pay full page-table walks. Fewer context switches → higher TB hit rate → faster memory access. Huge pages also reduce TB pressure (fewer entries cover the same memory).
**How does the OOM killer choose a victim, and how do you protect a process?** Each task gets an **oom_score** driven mainly by its memory footprint, adjusted by **/proc/<pid›/oom_score_adj*** (range -1000..1008). Higher score = preferred victim. Set 'oom_score_adj--1000° to effectively exempt a critical process. (The legacy
"oom_adj, range -17..15, is deprecated.) Note: CPU usage does **not** lower
the score on modern kernels - scoring is memory-based.
**Buddy system vs, slab allocator - what does each manage?** The **buddy system"* is the kernel's *page*-level allocator; it hands out power-of-two page blocks and merges adjacent free pages to limit fragmentation.
The **slab allocator** sits on top of the buddy system to allocate/free small
*kernel objects* efficiently (an object cache). Reclaimable slab shows as
" SReclaimable",
## memory_recycle.md
*  *Walk through what happens as free memory crosses the low then min watermark.** Each zone has min/low/high watermarks, When free drops below **low**, the kernel wakes ***kswapd*** to reclaim in the *background* until free rises back above
*  *high**; allocations keep succeeding meanwhile, When free hits **min**, allocating tasks enter **direct reclaim** - the faulting process itself reclaims synchronously and *stalls* until enough is freed, Low-kswapd is healthy; min-direct reclaim is the painful, latency-inducing state,
*  *kswapd (background) vs, direct reclaim - latency implications,** kswapd reclaims asynchronously in a kernel thread, so application allocations don't block - invisible to app latency, Direct reclaim happens *in the context of the allocating task*, which blocks until it frees pages - this 1s what causes latency spikes and stalls, Watch sar -B*: " pgscank/s (kswapd) vs pgscand/s (direct) - direct scanning is the red flag.
*  *Why can a box with "free" cache still OOM if it has no swap?** clean file cache is reclaimable, but **anonymous pages need somewhere to go**, and with no swap they are unreclaimable. If the pressure comes from anon memory, the kernel cannot evict it; once reclaim of cache isn't enough, the OOM killer fires even though cache looks droppable. (And some anon pressure can't be relieved by dropping cache at all.)
*  *How does the OOM killer pick a victim, and how do you protect a process?** Same as above:oom score from memory footprint+
•oom_score_adj: protect with
*oom_score_adj--1000. With group v2, each group has its own OOM behavior at
"memory.max*:
memory.oom. group-1 kills the whole group together, and

memory.events tracks oom / oom_kill.
*  *What is MGRU and why was it introduced?**
*  *Multi-Generational LRU** (kernel 6.1+) replaces the two active/inactive LRU lists with multiple aging *generations* per page type. It tracks page age more finely, making better reclaim (eviction) decisions and lowering CPU overhead under memory pressure. Introduced to improve reclaim quality and reduce thrashing versus the coarse two-list scheme.
*  *What does "swappiness" actually control?** (see swap.md above - it's the file-cache-vs-anon reclaim bias, a ratio, not an on/off switch.)
## how_to_optimize_performance.md
**When would you lower
"swappiness'
and what does it actually control?**
Lower it when you'd rather the kernel drop reclaimable page cache than swap out anonymous working-set pages (latency-sensitive apps whose anon memory you don't want paged). It controls the reclaim bias* between evicting file cache and swapping anon - a ratio, not a switch.
**How do huge pages improve performance, and when do they hurt?**
2MB/1 GB pages mean far fewer page-table entries, so fewer TB misses - a win for large-memory workloads (databases, JVMs). They hurt when they waste reserved memory for workloads that don't benefit, and **Transparent Huge Pages (THP) ** can cause latency spikes (allocation stalls, compaction) for latency-sensitive apps - which is why many databases disable THP.
*  *How do you stop a critical process from being 0OM-killed?** Set /proc/<pid>/oom_score_adj =-1000° (effectively exempts it). Also cap other processes with group limits so they are the preferred victims.
*  *cgroup v2 "memory high" vs "memory•max" - the difference.** memory high" is a **soft** limit: above it the kernel aggressively reclaims and
*  throttles* the group (slows it down) but does not kill.
"memory max" is the
*  *hard** limit: exceeding it triggers the group OOM killer. high smooths pressure; max" is the hard ceiling.
*  *What is the first thing you measure before optimizing memory?**
* Baseline with free -w,
*   vmstat 1°,
*  pidstat -r 1 to determine whether you are
merely cache-rich-but-fine or genuinely under pressure - i.e. measure before touching any knob, and watch available, not "free.
## performance_indicators_metrics.md
**Differentiate buffer vs. cache in Linux memory accounting.**
Buffer = temporary storage for raw disk **blocks** (data about to be written,
letting the kernel coalesce scattered writes into sequential I/o; also block-device
metadata). Cache = the page cache of **file contents** read from disk (speeds
subsequent reads), plus reclaimable slab (SReclaimable). Both are reclaimable.
**Difference between a minor and a major page fault.**
Minor = served from physical memory (first touch of a fresh page, or a page
already in page cache) - cheap. Major = requires disk I/0 (swap-in, or reading an
uncached file-backed page) - slow. Watch "pidstat-r -r (minflt/s/ majflt/s).
**Which tools would you chain to confirm and localize a memory leak?**
" vmstat" / free -w /' sar -r° to see steadily growing usage → "top /"pidstat -r' to attribute to a process → "memleak" (bcc) to print the leaking allocation stack (or "valgrind / heaptrack* /ASan in dev/test).
**How do you tell whether rising swap is due to reclaim pressure?** correlate swap activity (si/so",
*sar -W*) with reclaim signals: compare free
memory against zone watermarks in "/proc/zoneinfo, watch kswapd"/direct-reclaim scan rates (sar -B*),Risingswap *together with*scanning near the min watermark
- reclaim pressure; stale used-swap with no "si/so* is not.
**What does "available" include that "free" does not?**
" available" (from "MemAvailable") - free memory **plus** reclaimable cache (page cache + reclaimable slab) the kernel can hand to a new process without swapping, It is normally much larger than 'free', and it is the number to watch.
## jvm_memory.md
**Why does a container's RSS exceed •-Xmx*? Enumerate the off-heap regions, **
• -Xmx* bounds only the **Java heap**, RS also includes off-heap/native memory:
**Metaspace** (class metadata),
*  *thread stacks** (~0.5-1 MB each, thread count),
*  *direct byte buffers** (NIO/Netty,
"MaxDirectMemorySize* ), **JIT code cache**
(ReservedCodeCacheSize ),
**GC structures** (remembered sets, card tables),
symbol tables, and native allocations by JNI/z11b/crypto. The container 11mit Aust cover heap + all of these or the kernel ooM-kills it with a "healthy" heap.
**How do you diagnose a native (off-heap) memory leak in the JV?**
Enable **Native Memory Tracking** (*-XX:NativeMemoryTracking-summary|detail), take a jmd <pid› VM.native_memory baseline, then "summary.diff" over time to see which region grows (Metaspace → classloader leak; Direct - NIO buffer leak; 


Thread runaway threads; otherwise native/JNI -confirm with jemalloc profiling)
*  *What makes a JVM container-aware, and what breaks without it?**
*   -XX:+UseContainerSupport (default on JDK 10+, backported to 8u191+) makes the JVM read the **cgroup** memory/CPU limits instead of the host's. Without it (old JDKs) the JV sizes the heap and G/ForkJoinPool threads for the whole node and gets OOM-killed in a small container.
*  *Metaspace vs. the old Permen - what changed in JDK 8P**
JDK 8 removed Permen (fixed-size, in-heap, prone to "OutfMemoryError: Permen") and replaced it with **Metaspace**, which stores class metadata in **native memory** and grows dynamically (bound by *-XX:MaxMetaspaceSize ). Classloader leaks now surface as Metaspace growth.
**Why prefer "MaxRAMPercentage" over *-Xmx in Kubernetes?**
• -XX:MaxAMPercentage sizes the heap as a fraction of the container's* cgroup limit, so it adapts automatically when you change the pod's memory limit and leaves headroom for off-heap. A fixed -Xmx" is brittle - easy to set too high (0OMK111ed) or too low, and it must be re-tuned whenever the limit changes.
## memory_usage.md
**Difference between 'free' and 'available in "free's output.**
*  free* = completely unused RAM. available" = an estimate of what a new process can use without swapping - free **plus reclaimable cache**. Watch available; low
-   free is normal because Linux uses spare RAM for cache.
**Explain VIRT vS. RES vs. SHR vs. PSS.**
-   **VIRT** - all virtual address space requested (even untouched/un-backed); not a pressure signal on its own.
-   **RES (RSS)** - non-swapped physical memory actually in use now; counts shared pages fully.
-   **SHR** - shared memory: shared mappings, loaded libraries, and the code segment (not necessarily shared in practice).
-   **PSS** - Proportional Set Size: a process's private pages plus its *fair share* of shared pages. Summing RSS double-counts shared pages; PSS (from "smaps/" smem") gives the true per-process footprint.
*  *Why is low "free" memory not necessarily a problem on Linux?** Because Linux deliberately uses otherwise-idle RAM for the page cache, which is reclaimable on demand. Low 'free just means the kernel is using RAM productively; the real signal is available.
*  Which "/proc/meminfo fields feed MemAvailable ?**
*  MemFree plus the reclaimable portions of cache: reclaimable page cache (largely the inactive file LRU) and **"SReclaimable ** (reclaimable slab). It is the kernel's own estimate exposed as the MemAvailable field.
*  *How would you find the process with the largest real memory footprint?**
*  top sorted by
*  %MEM" or
*  ps
-eo pid, comm, rss --sort=-rss® for a quick ranking -
but for the true* footprint use **pss** (/proc/<pid>/smaps* or
"smem -s pss) to
avoid double-counting shared pages.
## memory_leak,md
*  *How do you tell a memory leak apart from healthy cache/allocator growth?** A leak is **monotonic, unbounded RSS/anon growth** with no corresponding workload growth and no plateau - it keeps rising until ooM. Page-cache growth (reclaimable) and allocator caching that plateaus (brk-cached heap) are *not* leaks. Watch long enough to distinguish growth from steady state (vmstat", pidstat-r").
*  *Which tool for a running production service, and why?**
*  *memleak* (bcc/eBPF)** - it attaches to malloc/ free via eBPF with low overhead, no recompilation, and works on a live process, printing the stacks of outstanding allocations, valgrind is far too slow for production and requires relaunching.
*  *valgrind vs. ASan vs, memleak - trade-offs,**
"memleak": low overhead, no rebuild, works live - best for production C/C++. valgrind" (memcheck); very high overhead (10-50x), no rebuild but must relaunch, extremely precise (also catches invalid/use-after-free) - best for dev/test.
- ASan/LSan: medium overhead, requires compile-time instrumentation, relaunch - best for CI/unit tests.
*  *What does "definitely lost" vs "still reachable" mean in valgrind?**
*  Definitely Lost* = blocks with **no remaining pointer** to them - true leaks.
*  Still reachable* = blocks a pointer still exists for at exit (often global caches
/ one-time allocations) - usually not real leaks. (*Indirectly Lost* - reachable only via a definitely-lost block.)
**How would you find a leak in a JVM or Go service?**
goroutine leaks.

## bcc_tools.md
**Why use eBPF tools over "free / vmstat/ pidstat*?**)
The coarse tools tell you *that* memory is under pressure; eBPF tools (BCC, bpftrace) tell you *where and why* by attaching probes to kernel functions, tracepoints, and user-space malloc/free - safely in production, with low overhead and no recompilation, down to exact stacks.
*Which tool to find a leak in a running C service?**
memleak-bpfcc -p<pid›- reports outstanding allocations with stacks.
**How do you measure page-cache hit ratio per process?**
• cachetop-bpfcc (per-process "READ_HIT% / WRITE_HIT% ): "cachestat-bpfce* for
system-wide.
**How can you observe 00M kills as they happen?**
•oomkill-bpfcc"- prints each 00M k111 live with the trigger PID, victim, pages, and load average.
**Prerequisites for BCC tools.**
Root (or "CAP_BPF / CAP_PERFON*), kernel headers/**BTF**, and target symbols/ debuginfo for readable stacks. On Debian/Ubuntu the tools carry a •-bpfcc" suffix (package " bpfcc-tools").
# Memory Interview question Answer
#* memory_k8s.md
*  *Difference between a memory *request* and *Limit*, and what each affects.**
*  *Request** is the scheduling budget - the node must have that much available (sum of other requests) before the pod is placed; it does not cap runtime usage.
*  *Limit** is the hard runtime ceiling enforced by the group (memory max"); exceeding it triggers the kernel group OOM killer (exit 137).
*  *Explain the three Qos classes and their eviction/OOM order.**
-   **Guaranteed** - requests == limits for CPU *and* memory on every container; most protected, evicted last.
-   **Burstable** - has some request/limit but not Guaranteed; evicted afterBestEffort.
-   **BestEffort** - no requests/limits; evicted first under node pressure.
The kubelet encodes this via "oom_score_adj", so BestEffort pods are the kernel's preferred OOM victims.
*  *OOMKilled vs. Evicted - which component acts and why?**
*  *DOMKilled** (exit 137) = the *kernel's* cgroup OOM killer killed a container for
hitting its own "memory.max". **Evicted** = the *kubelet* evicted a pod because the
*  node* was under memory pressure (node-level, based on working set vs eviction thresholds). Different triggers, different actors.
*  *What is "working_set" and how does it differ from RSS?**
*  working_set = memory.usage_in_bytes - inactive_file* (reclaimable page cache) -
roughly, memory that *cannot* be trivially reclaimed. It is what "kubectl top" reports and what the kubelet compares against the limit for eviction. RSS is raw resident set and over-counts reclaimable cache, so sizing on RSS misleads.
**HOW do
"memory.high" and "memory max" differ in group v2?**
" memory.high" = soft limit; kernel reclaims aggressively and *throttles* the group
above it (no kill).
"memory.max" = hard limit; exceeding it invokes the group OOM
killer, Kubernetes
(MemoryQos) can set "high" below max" to smooth pressure.
**How does Kubernetes handle swap today?**
Historically kubelet refused to start with swap on. Via the "NodeSwap® feature (alpha›beta), on cgroup v2 nodes **Burstable** QoS pods can get limited swap bounded by memory,swap,max;Guaranteed and BestEffort generally get none. Swap is off by default and gated.
## go_memory-md
**Describe Go's GC: concurrent? generational? compacting?**
Concurrent **tri-color mark-and-sweep** with a write barrier, tuned for low latency.
Marking/sweeping run alongside the program; stop-the-world pauses are brief (Sub-millisecond) and occur only at phase transitions, It is **not generational** and **not compacting** - objects never move, so there is no compaction pause, but also no defragmentation, A background scavenger returns unused pages to the oS lazily.
**What do
"GOGC and "GOMEMLIMIT control, and how do they interact?**
"GOGC"
(default 100) is the heap-growth target: a cycle starts when the heap
grows
*GoG% over the 1ive heap left by the last cycle (109 - when it doubles?
Higher = less GC CPU, more memory; off disables the proportional trigger.
*GOMEMLIMIT" (Go 1.19+) is a **soft** celling on total runtime memory (heap + runtime overhead). As usage nears it, GC runs more often regardless of GOG".
- Interaction: GC runs at whichever trigger fires first.
•GOG" sets the pace under
normal load; "GOMEMLIMIT takes over near the celling. Setting "GOGC-off with a
*   GOMEMLIMIT means GC runs only when you approach the 1imit. Because the 11mit 1s soft, the runtime will go over it rather than spin forever in GC (there is a built-in cap on GC CPU to prevent a death spiral).
*  *How do you configure a Go service so it doesn't get OOMKilled in Kubernetes?** Set "GOMEMLIMIT to about 90% of the container memory limit (with GOG" left at

	**¿nuse_space vs. alloc_space in a pprof heap profile?**
	"inuse_space /'inuse_objects" show memory that is **live right now*"; use them to
	hunt leaks and retention. "alloc_space/'alloc_objects" show **everything allocated
	since the program started**, including memory already freed; use them to find
	allocation churn that drives GC CPU.
actory is very tight, so the GC respects the group
limit and not the node's RAM. Leave headroom for non-heap memory (goroutine stacks, cgo/native allocations). Also find and fix goroutine leaks.
*What is escape analysis and why does it matter for memory?** At compile time the compiler decides whether each value can live on the goroutine's **stack** (freed automatically when the function returns, no GC cost) or must **escape to the heap** (managed by the GC). Fewer escapes mean fewer heap allocations, less GC work, and lower latency. View the decisions with
•go build -gcflags= -m'.
**How can a goroutine leak present as a memory leak?**
A goroutine that blocks forever (on a channel nobody sends to, or a missing cancel/timeout) keeps its stack and everything it references alive. Since those objects stay reachable, the GC cannot free them, so RS keeps climbing. You can spot it as a goroutine count that rises steadily in the /debug/pprof/goroutine" profile.

