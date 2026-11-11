K!-- performance/memory/memory_mapping-md -->
# Memory Mapping and Virtual Memory
How Linux maps virtual addresses to physical memory, how process address space is laid out, and the allocators and reclaim mechanisms behind it all.
#* Physical vs. Virtual Memory
when a machine is described as having "8 GB", that refers to **physical memory*» (main memory), which on most computers is DRAM. Only the kernel can access physical memory directly; user processes see **virtual memory**,
Each process's virtual address space is split into **kernel space** and **user
space*：
- **32-bit systems:** 4 GB total per process - 1GB kernel space at the top,
3GB user space below it.
the low end and 128 TB of kernel space at the high end, with a large
levels; 5-level paging extends this further.)
Physical memory is only backed when virtual memory is actually used, and that backing is managed through **memory mapping**.
## Memory Mapping and Page Tables
Memory mapping is the translation from a virtual address to a physical address.
The kernel maintains a **page table** per process recording these mappings. The CU*s wU** (Memory Management Unit) walks the page table in hardware to resolve accesses.
table, then returns to user space and resumes the process.
The M'W manages memory in **pages**, typically 4 KB, so every mapping is aligned to a 4 KB (or multiple) boundary.
* Linux
$ getconf PAGE_SIZE
4096
* Classic Unix alias $ pagesize
### TLB (Translation Lookaside Buffer)
The **T** is a fast cache inside the MU holding recently used page-table entries, and it strongly affects memory-access performance. Because each process has its own address space, a context switch can invalidate (flush) TB entries.
Reducing context switches reduces TLB flushes, improving TB hit rate and thus memory-access performance.
### Multi-level page tables and huge pages
A single flat page table would need far too many entries. Linux uses two mechanisms:
1, **Multi-level page tables** - the address is split into block indexes plus an in-block offset, x86-64 uses a 4-level (or 5-level) page-table walk, so only the portions of the table that are actually used consume memory.
2 **Huge pages (HugePage")**-2MB or 1GB pages instead of 4 KB. Fewer entries means fewer TB misses, which helps large-memory workloads (databases, JVs).
See how_to_optimize_performance, md for when to use them.
** Process Virtual Address Space Layout
From low to high address:
-   **Read-only segment** - code (text) and constants,
-   **Data segment** - global/static variables,
-  **Heap**- dynamically allocated memory, grows upward from low addresses,
-   **File-mapping segment** - shared 11brarles, shared memory, "mmap" regions; grows downward from high addresses.
-   **Stack** - local variables and call frames; fixed size, commonly 8 Me (ulimit -5).
### malloc, brk, and mmap
Both the heap and the file-mapping segment are allocated dynamically; "malloc() and mmap() are the usual entry points.
-   **Small allocations (< ~128 KB):** glibc's "malloc" uses **brk()**, moving the top of the heap. Freed memory is *not* immediately returned to the kernel; it is cached for reuse. This reduces page faults and speeds up repeated allocation, but a busy allocate/free pattern can cause heap fragmentation.
-   **Large allocations (> ~128 KB):** *malloc* uses ***mmap() ** directly, carving a region out of the file-mapping segment. "mmap" memory *is* returned to the kernel on free, so each new emmap region triggers a fresh page fault. Under heavy

churn this produces many page faults and increases kernel management overhead.
The 128 KB threshold is glibc's MMAP_THRESHOLD, which is dynamie by default and tunable via malloptM_MMAP_THRESHOLD, ..•) •
Importantly, calling "malloc" does **not** Immediately allocate physical memory - only the first access* does, via a page fault that pulls the kernel in to back the page. This is why RSS lags behind VIRT. (See "memory_usage nd' for VIRT/RES/SHR.)
## Kernel Allocators
-   **Buddy system** - the kernel's page-level allocator. Like the MMU It works in page units and merges adjacent free pages to limit fragmentation.
-  **slab allocator**-built on top of the buddy system to allocate and free small kernel objects efficiently. Think of slab as an object cache layered over the page allocator. Reclaimable slab shows up as "SReclaimable in proc/meminfo.
## When Memory Runs Low: Reclaim
When memory gets tight, the kernel reclaims it through mechanisms such as:
1.    **Reclaim the page cache** using an LRU-style policy - evict the least-recently-used file-backed pages.
2.    **Swap out anonymous pages** - write infrequently used anonymous memory to the swap device/file.
3.    **OOM killer» - as a last resort, kill a process to free memory.
## The OoM killer
Pocess out an eom score the more mene a process uses the con
ocess with an *e
score and the more likely it is to be killed.
You influence this through /proc/<pid>/:
- The modern knob is **oom_score_adj***, range **[-1000, 1000]**. Setting it to
**-1000** effectively exempts the process from the OOM killer.
- The legacy knob **"oom_adj ** had range **[-17, 15]** where **-17** disabled
DOM for the process. "oom_adj" is deprecated; prefer "oom score_adj".
› footprint (plus "oom_score_adj"), not CPU usage.
MGLRU) and
"swap.md" for swap tuning.
## Troubleshooting Workflow
1.    Confirm the symptom: growing RSS? high page-fault rate? ooM kills in dmesg?
2.    ps -o pid, rss, vsz, comm" or "top® (sort by "%MEM) to find the process.
3.    pidstat-r1 to watch minor (minflt/s) vs. major ("majflt/s) faults.Rising **major»* faults mean swap/disk I/0 is involved - expensive.
4. Inspect the map; " cat /proc/<pid›/maps* /"smaps, or •pmap -x <pid›.
5, For huge-page or TB issues, check "grep Huge /proc/meminfo and
*/sys/kerne1/mm/transparent_hugepage/.
#* Common Pitfalls
-  **VIRT is not real memory,** Large VIRT with small RSS 1s normal (reserved but untouched address space),
-   **Freed "malloc" memory may not return to the 0S** (brk-cached), so process RSS can stay high even after free().
-  **mmap*-heavy churn** causes page-fault storms; batch lange allocations on pool them.
-  **Transparent Huge Pages** can cause latency spikes for some latency-sensitive apps; they are often disabled for databases.
## Likely Interview Questions
-   What happens, step by step, on a page fault?
-   Distinguish minor vs, major page faults and their cost.
-   Why does "malloc" not allocate physical memory immediately?
-   When does glibc use "brk" vs. "mmap, and why does it matter?
-   What is the TLB and how do context switches affect it?
-   How does the OOM killer choose a victim, and how do you protect a process?
-   Buddy system vs. slab allocator - what does each manage?
#* References
-   Linux •proc(5) man page - /proc/<pid>/maps, "smaps, "oom_score_adj.
-   man mmap,
*   man brk,man mallopt.
-   Documentation/admin-guide/mm/ in the Linux kernel tree.
