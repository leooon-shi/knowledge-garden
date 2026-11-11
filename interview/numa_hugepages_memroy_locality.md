<!-- interview/numa_hugepages_memory_locality.md -->

# NUMA, Huge Pages, and Memory Locality

Expands the passing mentions in `cpp_multithreading_throughput.md`, `mmap_traps.md`, and
`io_uring_schedulers_allocators.md`. False sharing and cache-line mechanics are in
`cpp_memory_model_atomics.md §6`. (The study host is a single-NUMA-node VM - 8 vCPUs of an
EPYC 7713P - so the commands below are illustrative, not measured here.)

## 1. NUMA basics

- **Non-Uniform Memory Access:** each socket (and on AMD EPYC, potentially each
  CCD/quadrant with NPS2/NPS4 BIOS settings) has local memory controllers. Accessing
  another node's memory crosses the interconnect (Intel UPI, AMD Infinity Fabric).
- **Rough costs:** local DRAM ~80-100 ns; remote ~1.3-2x that, with lower bandwidth.
  Cross-socket cache-line transfers (contended atomics, false sharing across sockets) are
  much worse than cross-core on one socket.
- **Topology:** `numactl --hardware` (nodes, CPUs, distance matrix), `lscpu`, `lstopo`
  (huloc), `/sys/devices/system/node/`.
- Even single-socket chips have locality structure: AMD CCX/CCD L3 domains (cores sharing
  an L3), Intel sub-NUMA clustering (SNC). Threads that share data should share an L3.

## 2. Linux NUMA policies

- **First-touch:** a page is allocated on the node of the CPU that **first writes** it,
  not the one that `malloc`'d it. Classic bug: the main thread initializes a big array,
  then workers on other nodes process it -> everything is remote. Fix: initialize in
  parallel with the same thread-to-data mapping as the compute phase.
- **`numactl`*** at launch: `--cpunodebind=0 --membind=0` (strict local),
  `--interleave-al1` (stripe pages across nodes - good for large shared structures
  accessed by everyone, e.g. a database buffer pool or a hash table), `--preferred=N`.
- **In code:** `mbind()`, `set_mempolicy()`, `move_pages()`, libnuma
  (`numa_alloc_onnode`), `pthread_setaffinity_np`/`sched_setaffinity`.
- **Automatic NUMA balancing** (`kernel.numa_balancing=1`): the kernel periodically unmaps
  pages to sample access faults and migrates pages/tasks toward each other. Helps unpinned
  workloads; adds page-fault overhead and latency jitter. Latency-critical, already-pinned
  services often disable it.
- **`vm.zone_reclaim_mode:`** if enabled, the kernel reclaims local page cache rather than
  allocating remotely; usually leave at e for general servers (enabling caused well-known
  database stalls).
- **Observe:** `numastat -p <pid>` (per-node memory of a process), `numastat-m`,
  `/proc/<pid>/numa_maps`, `perf stat -e node-loads,node-load-misses` (or
  `mem_load_l3_miss_retired.remote_dram` on Intel), `perf c2c` for remote HITM.

## 3. Designing for NUMA

- **Shard by node or core:** per-node thread pools, allocators, quees; a request is
  handled entirely on one node (Seastar/ScyllaDB shard-per-core, DPDK per-socket
  mempools).
- **Allocators:** jemalloc arenas and tomalloc per-CPU caches are NUMA-oblivious by
  default - tcmalloc has NUMA-aware mode (`TCMALLOC_NUMA_AWARE`); memory freed on node 1
  can be reused on node e. Pinning threads keeps allocation local.
- **NIC/IRQ locality:** the NIC is attached to one socket's PCIe root. Put its IRQs
  (`/proc/irq/*/smp_affinity`, disable `irqbalance` or configure it) and the processing
  threads on that node. Check with `cat /sys/class/net/ethe/device/numa_node`.
- **Containers/Kubernetes:** the kubelet **Topology Managğr** (`single-numa-node` policy)
  \+ CPU Manager `static` + Memory Manager align CPUs, memory, and devices (GPUS/SR-IOV
  NICs) to one node for guaranteed-QoS pods.
- **Replicate read-mostly data per node** (routing tables, model weights) instead of
  sharing one copy.

## 4. Huge pages

### Why

x86-64 base pages are 4 KiB. The **TLB** holds only ~1.5-3k entries (L1 dTLB ~64, STLB
~1,5-2k),covering covering a few MiB with KiB pages. Larger working sets miss the TLB and pay a
**page walk** (up to 4-5 memory accesses, partially cached). **2 MiB** pages cover 512x
more per entry; **1 GiB** pages more still. Typical gains: 5-20% for large in-memory
workloads (databases, JVM heaps, ML inference, key-value stores); also smaller page tables
and fever page faults.

## Explicit (hugetlbfs) huge pages

- Pre-reserved pool; `vm.nr_hugepages=N` (or `hugepages=` on the kernel conmand 1ine,
  which is the only rellable way to get 1 GiB pages after boot fragments menory).
  Per-node: `/sys/devices/system/node/node0/hugepages/...`.
- Used via `mmap(MAP_HUGETLB | MAP_HUGE_2MB)`, `shmget(SHM_HUGETLB)`, on filles on a
  hugetlbfs mount, DPDK, Oracle/PostgreSQL (`huge_pages=on`), QEMU guest memory.
- **pros:** guaranteed, never split or swapped, predictable. **Cons:** reserved memory is
  unavailable to everything else; must be sized up front; allocation falls (SIGBUS on
  fault for MAP_NORESERVE-style overcommit) if the pool is exhausted.

### Transparent Huge Pages (THP)

- The kernel backs anonymous memory with 2MiB pages automatIcally when aligned 2 MiB
  regions are available. `/sys/kernel/mm/transparent_hugepage/enabled`: `always` /
  **madvise** (only regions with `madvise(MADV_HUGEPAGE)` - the study host's setting and
  a common distro default) / `never`.
- **khugepaged** collapses existing 4 KiB pages into huge pages in the background.
  - **Why THP has a bad reputation for latency:**
  - **Direct compaction** at fault time when no free 2 MIB block exists: the faulting
    thread stalls (ms-scale) while the kernel defragments. Controlled by `.../defrag`
    (`always` / `defer` / `defer+madvise` / `madvise` / `never`); `defer`/`madvise` avoids
    stalls for most allocations.
  - **Memory bloat:** a 2 MiB page is fully resident even 1f 4 KiB is touched;
    `madvise (MADV_DONTNEED)` on part of it splits the page. Allocators that return memory
    in small chunks fight THP (jemalloc/tcmalloc have THP-aware options: tcmalloc's
    hugepage-aware allocator "Temeraire", femalloc `metadata_thp`, `thp:` settings).
  - **khugepaged CPU** and copy-on-write of 2 MiB after `fork()` (a Redis BGSAVE latency
    issue - Redis warns if THP is `always`).
  - Hence long-standing advice from Redis, MongoDB (pre-8.0), Oracle and others to disable
    THP - though newer kernels and allocators make `madvise`-mode THP the reasonable
    default, and MongoDB 8.0 now recommends enabling it.
- **mTHP (multi-size THP, kernel 6.8+):** intermediate sizes (16 KiB-1 MiB) for anonymous
  memory, configurable per size under
  `/sys/kernel/mm/transparent_hugepage/hugepages-*kB/`: also helps ARM64 contiguous-PTE
  hints.
- **File-backed/page-cache huge pages:** large folios for file systems (XFS, ext4
  progressively), `CONFIG_READ_ONLY_THP_FOR_FS` for executable text;
  `-Wl,-zcommon-page-size=2097152 -zmax-page-size=2097152` + `MADV_HUGEPAGE` remapping of
  `.text` (Facebook's/Google's hot-text-on-huge-pages trick) reduces iTLB misses for big
  binaries.
- **Observe:** `grep -E 'AnonHugePages|HugePages_' /proc/meminfo`, `/proc/<pid>/smaps`
  (`AnonHugePages` per mapping), `/sys/kernel/mm/transparent_hugepage/khugepaged/`,
  `perf stat -e dTLB-load-misses,iTLB-load-misses`, `/proc/vmstat` (`thp_fault_alloc`,
  `compact_stall`).

## Likely interview questions

- *"Your service got 30% slower on a 2-socket box than on a 1-socket box with the same
  cores."* - Remote memory/cross-socket coherence; check `numastat`, pin and shard,
  first-touch.
- *"What is first-touch?"*
- *"When would you interLeave memory?"*- Large structures accessed uniformly by all
  nodes.
- *"Why do huge pages help, and why do some databases tell you to disable THP?"* - TLB
  reach vs compaction stalls, bloat, fork COW.
- *"hugetbfs vs THP?"* - Reserved/guaranteed vs automatic/best-effort.
- *"How do you check whether TLB misses matter?"* - `perf stat` dTLB/iTLB misses,
  page-walk cycles (`dtlb_load_misses walk_active`).

## Synthesis

On multi-socket (and chiplet) machines, memory and caches have **locality**: remote DRAM
costs ~1.3-2x and cross-socket coherence much more. Linux allocates on **first touch**, so
initialize data on the threads that use it; use `numactl`/`mbind` to bind or interleave,
consider disabling automatic NUMA balancing for pinned latency-sensitive services, and
align NIC IRQS, threads and memory on one node (Kubernetes Topology Manager does this for
pods). **Huge pages** extend TLB reach 512x (2 MiB) and cut page-walk overhead for large
working sets, **hugetibfs** is reserved and predictable; **THP** is automatic but can
cause compaction stalls, memory bloat and fork COW costs - "madvise"mode with THP-aware
allocators is the usual compromise, with mTHP and large follos widening the options on
newer kernels,
