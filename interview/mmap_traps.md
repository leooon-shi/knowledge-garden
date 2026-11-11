<!-- interview/mmap_traps.md -->

# The Traps of Using map

`mmap` looks deceptively simple - map a file or memory region into your address space and
access it like an array -but it hides many sharp edges that surface as crashes, silent
data loss, latency spikes, and security holes on production Linux servers. The unifying
theme: **`mmap` moves work you can normally see (syscalls, error returns) into the
invisible machinery of the page-fault handler and the virtual memory subsystem - so
failures show up as signals and stalls instead of clean error codes.**

## 1. Errors become signals, not return values

The most fundamental trap. With `read()`/`write()`, an I/O error or truncated file gives
you an error return. With mmap, the actual I/O happens lazily inside the page-fault
handler when you *touch* memory, so:

- **`SIGBUS`** is delivered if you access a page that can't be backed - most commonly when
  the underlying file is **truncated** (by you or another process) so the mapped offset no
  longer exists, or a disk-backed file becomes unreachable. Your innocent-1ooking `buf[i]`
  throws a signal, not an exception, and by default kills the process.
- **`SIGSEG`** if you access beyond the mapping or violate protection (e.g., write to a
  `PROT_READ` mapping).

The trap: **you can no longer handle I/O errors at the call site.** Robust use requires
either guaranteeing the file can't be truncated/shrunk under you, or installing a `SIGBUS`
handler (genuinely hard to do correctly and largely defeats the simplicity map promised).
This is why many systems avoid `mmap` for flles other processes may modify.

## 2. Mapping past end-of-file / truncation races

Mappings are page-granular (typically 4 KB). If a file is 100 bytes, you map a full 4 KB
page; bytes 100-4095 read as zero, which is fine. But:

- Accessing beyond the last page the file backs → **SIGBUS**.
- If another process (or you) **`ftruncate`'s the file smaller** after mapping, previously
  valid pages become invalid and touching them → **SIGBUS**.
- Extending a file via `mmap` doesn't work as people expect - you must
  `ftruncate`/`fallocate` the file to size *first*, then map; writing to a mapped region
  beyond the file's current size does not grow the file.

## 3. msync, dirty pages, and durability illusions

With `MAP_SHARED`, writes modify the page cache and *eventually* get written back - but
**when** is not guaranteed:

- Data is **not durable** until you `msync(MS_SYNC)` (and metadata durability may need
  `fsync`/`fdatasync` on the fd). Assuming writes hit disk because "I wrote to the
  mapping" is a classic data-loss bug on crash.
- Write-back timing is controlled by dirty-page thresholds (`vm.dirty_ratio`,
  `dirty_background_ratio`); under memory pressure it happens at unpredictable times,
  causing **latency spikes**.
- `MAP_PRIVATE` (copy-on-write) writes are **never** written back to the file - a trap if
  you expected persistence. COW pages diverge from the file after the first write, so you
  also stop seeing subsequent updates other writers make to those pages.

## 4. Page-fault latency and the "it's just memory" illusion

Accessing mapped memory *Looks* free, but the first touch of each page triggers a **major
page fault**: a synchronous disk read inside your access.

- **Unpredictable tail latency.** A loop over a large mapped file isn't a CPU loop - it's
  thousands of disk I/Os hiding as memory accesses. p99 explodes.
- **No batching / readahead control by default.** Sequential access gets some kernel
  readahead; random access thrashes. Use **`madvise`** (`MADV_SEQUENTIAL`, `MADV_RANDOM`,
  `MADV WILLNEED` to prefault, `MADV_DONTNEED` to drop) to hint the kernel - forgetting
  `madvise` is a common performance trap.
- **Blocking in unexpected places.** A page fault can block the thread on I/O while
  holding a lock, stalling others. In an event loop this is deadly - a single fault can
  freeze the 1o0p for milliseconds.

## 5. TLB pressure and mapping churn

Every mapping consumes **page-table entries** and **TLB** capacity:

- Mapping huge regions with KB pages creates enormous page tables and heavy **TLB miss**
  rates on random access, Consider **huge pages** (`MAP_HUGETL` on transparent huge
  pages) for large mappings - but THP has its own traps (latency from background
  compaction, memory bloat),
- **Frequent map/unmap churn is expensive.** `mmap`/`munmap` modify the process's VMA
  (virtual memory area) red-black tree under the map semaphore, and `munmap` forces **TLB
  shootdowns** - cross-CPU IPIs to Invalidate other cores' TLBS. Doing this on a hot path
  serializes cores and destroys scalability, Map once, reuse.

## 6. Address space and VMA 11mits

- Each distinct mapping is a VMA; the kernel caps them at **`vm.max_map_count`** (default
  ~65530), Applications mapping many small files or many separate regions can hit this and
  get `ENOMEM` from `mmap` even with plenty of RAM free - a surprising production failure.
- On 32-bit systems, address space exhaustion/fragmentation is a real limit; on 64-bit
  it's mostly VMA count and physical memory.

## 7. Alignment and offset rules

- The `offset` argument must be a **multiple of the page size** (`sysconf(_SC_PAGESIZE)`),
  or `mmap` fails with`EINVAL`. To map an arbitrary flle offset, map the page-aligned
  offset below it and index into the returned pointer. Forgetting this is a common
  first-time bug.
- The returned address is page-aligned; length is rounded up to a page internally but you
  must only access within the length you asked for.

## 8. Concurrency and shared-memory hazards

- With `MAP_SHARED` across processes, you have **shared memory with no built-in
  synchronization**, Data races, torn reads/writes, and the need for atomics or
  cross-process locks (futexes, `pthread_mutex` with `PTHREAD_PROCESS_SHARED`) all apply.
  People treat map'd shared memory as magically safe; it isn't.
- **Memory ordering** across processes needs the same atomics/fences as threads.
- If you store **pointers** in shared memory, they're only valid if the region is mapped
  at the **same address** in every process (`MAP_FIXED`, fragile) - otherwise use
  **offsets/relative pointers**, not raw pointers. A classic shared-memory data-structure
  trap.

## 9. fork, MAP_FIXED, and lifetime traps

- **`MAP_FIXED`** blindly unmaps whatever was already at the target address - a great way
  to clobber your own heap/stack/libraries and cause memory corruption. Prefer
  `MAP_FIXED_NOREPLACE` (newer kernels) if you must pin an address.
- Across **`fork`**, `MAP_SHARED` mappings are shared; `MAP PRIVATE` are COW-duplicated.
  Confusing the two leads to "why didn't the child see my write?" or "why did the child's
  write not persist?" bugs.
- **Lifetime:** the mapping outlives the fd - you can `close()` the fd after `mmap` and
  the mapping stays valid. Conversely, forgetting to munmap leaks address space (process
  exit reclaims it).

## 10. Security traps

- **`PROT_WRITE | PROT_EXEC`** (W^X violation) mappings are a code-injection risk and are
  blocked by many hardening policies (SELinux, `mprotect` restrictions). JITs must map
  writable, fill, then `mprotect` to executable - never both at once.
- Mapping **untrusted files** and parsing them via pointers means a truncation by an
  attacker becomes a `SIGBUS` Dos, and malformed content is parsed directly from the page
  cache with no I/O boundary to validate at.

## How to use mmap safely - the checklist

1. **Only map files whose size is stable** for the mapping's lifetime, or install a
   `SIGBUS` handler and be prepared for the complexity. Never map a file another process
   may truncate without a plan.
2. **`msync + fsync`** if you need durability; don't assume writes persist.
3. **`madvise`** to match your access pattern; **prefault** (`MADV WILLNEED` /
   `MAP POPULATE`) if you can't tolerate page-fault latency on the hot path.
4. **Map once, reuse; avoid map/unmap churn** to dodge TB shootdowns and VMA-lock
   contention.
5. **Watch `vm.max_map_count`** if you map many regions.
6. **Page-align offsets**; use relative offsets (not raw pointers) in shared memory;
   synchronize shared mappings explicitly.
7. **Never `PROT_WRITE|PROT_EXEC`**; use `mprotect` transitions for JITs.
8. **Know when *not* to use it:** for small files, streaming, or files that change under
   you, plain `read`/`write` (or `pread`/`pwrite`) is simpler, gives clean error handling,
   and is often faster. `mmap` wins for large, repeatedly-accessed, randomly-indexed,
   stable files (databases, indexes) and for genuine shared memory.

## Interview Q&A

**Q: What is the single most surprising failure mode of `mmap` compared to
`read`/`write` ?** A: Errors arrive as signals, not return values. Because the actual I/O
happens lazily in the page-fault handler when you touch memory, a failure such as the
backing file being truncated delivers `SIGBUS` on an ordinary `buf[i]` access, and a
protection violation delivers `SIGSEG`. You lose the ability to handle I/O errors cleanly
at a call site, which is why many systems avoid `mmap` for files other processes can
modify. *FoLLow-up:* How would you make a `SIGBUS` from truncation survivable? Install a
`SIGBUS` handler that uses `siglongjmp` or marks the page, but it's genuinely hard to do
correctly and largely defeats mmap's simplicity - better to guarantee the file size is
stable, *Red flag:* Claiming `mmap` returns an error code when the mapped file is later
truncated.

**Q: A service maps thousands of small files and starts getting `ENOMEM` from `mmap`
with gigabytes of RAM free, What's going on?**A: Each distinct mapping is a VIA, and the
kernel caps VMA count per process at `vm.max_map_count` (default ~65530). Mapping many
small files or regions exhausts that 11mlt and `mmap` fails with `ENOMEM` even though
physical menory is avallable, Check `wc -l < /proc/<pid>/maps` against
`cat /proc/sys/vm/max_map_count`; the fix is to map fewer/larger regions on raise the
sysctl, *FolLow-up:* Why not just raise `vm.maxmap_count` unconditionally? Each VMA costs
kernel memory and lengthens VMA red-black-tree walks, so a huge count degrades map/munmap
and fault performance - raise it, but also question the design that needs so many
mappings, *Red flag:* Assuming `ENOMEM` always means physical memory is exhausted.

**Q: Why can `mmap` give you unpredictable tail latency where plain `read` would not?** A:
The first touch of each page triggers a major page fault - a synchronous disk read hidden
inside a memory access, A loop over a large mapped file 1s secretly thousands of disk
I/Os, so p99 explodes, and a fault can even block a thread while it holds a lock, stalling
an event loop. `read`/`pread` make that I/O explicit and batchable. *FolLow-up:* How do
you mitigate fault latency when you must use `mmap`? Use madvise" (`MADV_NILLNEED`,
`MADVSEQUENTIAL`) or `MAP POPULATE` to prefault, and `MADV_RANDOM` to disable
counterproductive readahead. *Red flag:* "It's just memory access, so it's free and
constant-time."

**Q: With `MAP_SHARED`, I wrote to the mapping - Is my data on disk?** A: No. Writes go to
the page cache and are written back at an unspecified time governed by
`vm.dirty_ratio`/`dirty_background_ratio`. You must msync (`MS_SYNC`) for data durability
and may need `fsync`/`fdatasync` on the fd for metadata; assuming the write persisted is a
classic crash-data-loss bug. With `MAP_PRIVATE`, writes are copy-on-write and never reach
the file at all. *FolLow-up:* After the first `MAP_PRIVATE` write to a page, do you still
see other writers' updates to that page? No - the COW copy diverges, so you stop observing
subsequent updates to those pages. *Red flag:* Treating `MAP_SHARED` writes as
synchronously durable.

**Q: Why is frequent `mmap`/`munmap` on a hot path a scalability killer on a multicore
box?** A: `munmap` must invalidate other cores TLentries via TLB shootdowns - cross-CPU
IPIs - and both operations modify the process MA tree under the map lock. Doing this per
request serializes cores and destroys scalability. The rule is map once and reuse, not
map/unmap churn. You can see the IPI cost with `perf stat -e tlb:tlb_flush` or by watching
`grep TLB /proc/interrupts` climb. *FolLow-up:* Would huge pages help here? Yes for TLB
pressure on large mappings (fewer PTEs/TLB entries), but THP adds its own latency from
background compaction and can bloat memory, so it's a separate trade-off. *Red flag:*
Treating `munmap` as a cheap local operation with no cross-core impact.

**Q: You store a linked data structure in a `MAP_SHARED` region used by multiple processes
and it crashes in the second process. Likely cause?** A: Raw pointers in shared memory are
only valid if every process maps the region at the same address. Different processes
usually get different base addresses, so stored pointers dereference garbage. Use
offsets/relative pointers from the region base instead of raw pointers, and synchronize
access with cross-process atomics or a `PTHREAD_PROCESS_SHARED` mutex - shared memory has
no built-in synchronization. *Follow-up:* Isn't `MAP_FIXED` a fix for the address problem?
It can pin the address but blindly unmaps whatever was there, risking corruption; prefer
`MAP_FIXED_NOREPLACE` and relative pointers over forcing addresses. *Red flag:* Assuming
mmap'd shared memory is automatically thread/process safe, or that pointers are portable
across mappings.

## Synthesis

The through-line of every `mmap` trap: **it trades explicit, checkable syscalls for
implicit virtual-memory behavior, so errors arrive as `SIGBUS`/`SIGSEG` instead of return
codes, I/O latency hides inside ordinary memory accesses, durability is not automatic, and
the VM subsystem (page tables, TLB, VMAs, dirty-page wrIteback) becomes your performance
and scalability limit.** `mmap` is a genuinely powerful tool for large stable files and
shared memory, but it demands that you reason about the page-fault path, truncation
safety, explicit synchronization and flushing, and mapping lifecycle. The senior instinct
is to reach for `mmap` deliberately - when the access pattern justifies it - and otherwise
prefer `pread`/`pwrite`, which keep failures visible and behavior predictable.
