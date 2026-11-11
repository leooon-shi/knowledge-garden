<!-- interview/tcmalloc-rseq-vs-jemalloc-tls.md -->

# tcmalloc REQ vs jemalloc TLS - Which Is Faster?

## Short Answer

For the fast path of an allocation (grabbing a small object from a per-CPU/per-thread
cache), **tcmalloc's REQ-based per-CPU caching is generally faster and more
memory-efficient than jemalloc's TLS-based per-thread arenas** on modern Linux - *but* the
win is context-dependent, not universal. REQ's advantage is less about raw cycles per
operation and more about **scaling behavior and memory footprint** as thread count grows.

## Layer 1: The x86-64 Assembly of the Hot Path

### jemalloc TLS fast path

Thread-local access on x86-64 Linux uses the %fs" segment register, which points at the
thread control block. A thread variable access looks like:

```asm
; load the tache pointer from TLS
mov    %fs:Oxfffffffffffffff8, %rax   ; TLS offset → tcache*
; index into the per-stze-class bin
mov    0x10(%rax), %rdx               ; bin-›avail (stack top index)
test   %rdx, %rdx
je     .Lslow                          ; empty go to slow path
; pop an object off the bin's array
mov    -0x8(%rbx,%rdx,8), %rax        ; ptr = bin->avail[--n]
dec    %rdx
BOV    %rdx, ex10(%rbx)
ret
```

The key property: **no atomics, no memory barriers,** Because the cache belongs to *this
thread*, and only this thread can touch it, plain loads/stores are safe. This is the
source of TLS's speed - it's the cheapest possible mutual exclusion (there is no exclusion
needed at all).

### tcmalloc REQ fast path

RSEQ needs to (a) read the current CPU, (b) index that CPU's slab, (c) pop an object, (d)
commit - all in a sequence that can be safely restarted. Simplified:

```asm
.Lstart:
    ; rseq_abi is in TLS; read current CPU id
    mov    %fs:_rseq_abi@tpoff+4, %ecx   ; cpu_id (kernel keeps this updated!)
    ; compute address of this CPU's slab header for the size class lea
    lea    (slabs_base, %rcx, slab_shift), %r9
    mov    (%r9), %r10                     ; current - header.current (freelist top)
    mov    (%r9), %r11                     ; begin/end bounds check
    cmp    end, %r10
    jae    .Lslow                          ; underflow→slow path
    mov    -8(%r10), %rax                  ; obj = *(current - 8)
.Lcommit:
    mov    %r10_dec, (%r9)                 ; header.current 8 COMMIT INSTRUCTION
    ret
.Labort:
    jmp   .Lstart:                         ; kernel redirects here on preempt/migrate
```

The magic is in **which instruction is the commit.** The `mov ... (%r9)` that updates
"header current" is registered with the kernel as the *commit point*. The kernel
guarantees:

- If the thread is preempted, migrated to a different CPU, or receives a signal **anywhere
  between `.Lstart` and `.Lcommit`**, execution is redirected to `.Labort`.
- If it reaches the commit instruction, it ran entirely on one CPU, so the
  single-instruction store is atomic-with-respect-to-migration *for free*.

So RSEQ replaces a `lock cmpxchg` (a full atomic RMW, ~20+ cycles with cache-line
ownership transfer) with a **plain `mov`** (~1 cycle). That's the core performance trick.

### The cycle comparison

|              | jemalloc TLS | temalloc RSEQ                |
| ------------ | ------------ | ---------------------------- | 
| CPU-id read  | not needed   | 1 load (kernel-maintained)   |
| Bounds check | 1 cmp        | 1 cmp                        |
| Pop object   | 1-2 loads    | 1-2 loads                    |
| Commit       | plain store  | plain store (kernel-guarded) |
| Atomics      | none         | none                         |
| Barriers     | none         | none                         |

Cycle-for-cycle they're **nearly identical on the uncontended hot path**, RSEQ pays one
extra load (the CPU id), TLS pays one extra indirection (thread pointer → tcache). The
difference is in the noise. **The win is not raw hot-path cycles - it's structural.**

## Layer 2: The RSEQ Kernel Mechanism in Detail

### Registration

At thread startup, glibc (2 2.35) issues:

```c
rseq_abi(&__rseq_abi, sizeof(struct rseq), 0, RSEQ_SIG):
```

This registers a per-thread `struct rseq` with the kernel. The critical field is `cpu_id`,
which **the kernel writes on every context switch/migration** so user space can read the
current CPU without a syscall.

### The critical-section descriptor

Each REQ critical section is described by a `struct rseq_cs` placed in a special ELF
section (`__rseq_cs`), containing:

```c
struct rseqcs {
    u32 version;
    u32 flags;
    u64 start ip;           // .Lstart
    u64 post_commit_offset; // length of the critical section
    U64 abort_ip:           // .Labort
};
```

Before entering the section, the code stores pointer to this descriptor into
`__rseq_abi.rseq_cs`.

# What the kernel does on a context switch

In the scheduler's resume path (`__rseq_handle_notify_resume` → `rseq_ip_fixup`), on every
return to user space after preemption/signal/migration:

1. Read the thread's `rseq_cs` pointer.
2. If the interrupted instruction pointer is within
   `[start_ip, start_ip + post_commit_offset)` - i.e., inside the critical section but
   *before* the commit - the kernel **rewrites the userspace IP to `abort_ip`**.
3. The `RSEQ_SIG` signature word placed just before `abort_ip` is checked to prevent
   jumping into arbitrary code (a security guard against attacker-controlled aborts).

This means the abort is not an exception the program catches - it's the kernel literally
moving your program counter back to the retry label. From user space it looks like the
critical section "just re-ran."

### Cost of an abort

An abort costs: the wasted work of the partial sequence (a few instructions) + the branch
back to start + re-reading the (now updated) CPU id. Typically **tens of cycles**. Cheap
individually, but under pathological oversubscription (10x more runnable threads than
cores) abort rates rise and this erodes the advantage.

## Layer 3: Data Structure Layout - The Real Differentiator

### jemalloc: arenas + tcache

```
Thread A --assigned--> Arena 0  --> bins --> runs --> pages
Thread B --assigned--> Arena 1
Thread C --assigned--> Arena 0   (arenas shared round-robin)
   |
   |_ each thread ALSO has: teache (per-thread, per-size-class free lists)
```

- **Arenas**: bounded set (default `4 x ncpus`), lock-protected, hold the actual page
  runs, Threads hash to an arena.
- **tcache**: fully thread-local, no lock. Holds a stack of free pointers per size class.
  Filled/flushed in batches from the arena.

Memory math: with T threads and S size classes, worst-case cached-but-idle memory ≈ **T x
S × (batch fli1)**, With 10,000 threads this is enormous - this is jemalloc's "cache
stranding" problem.

### tcmalloc: per-CPU slabs

```
CPU 0 --> [ slab: contiguous region, one region per size class ]
CPU 1 --> [ slab: ... ]
...
centralFreelist (shared, Jock-protected) > Pageheap -->OS
```

- Each CPU has **one contiguous slab** carved into per-size-class regions. The slab uses a
  "current pointer between begin/end" design - allocation is a pointer bump/pop, as shown
  in the assembly.
- Cached-but-idle memory = **ncpus x S x region_size**, Bounded by core count,
  *independent of thread count*.

Memory math: 128 CPUs vs 10,000 threads 1s a ~78x reduction in the number of caches.
**This bounded footprint is the dominant real-world advantage of the REQ design**, far
more than the per-op cycle count.

### Cache-line / TLB effects

- Fewer, denser caches (tcmalloc) → better L2/L3 residency of allocator metadata → fever
  misses on the hot path → *effectively* faster even 1f the instruction sequence is the
  same.
- Jemalloc's many thread caches spread metadata across menory worse locality at high
  thread counts.

## Layer 4: Slow Path & Central Structures

Both need a slow path when the local cache misses:

**jemalloc:** tcache empty → lock the assigned arena's bin → refill a batch + unlock.
Contention scales with threads / arenas*. More arenas reduce contention but increase
fragmentation and memory.

**tcmalloc:** per-CPU slab empty → go to the "TransferCache'/CentralFreeList
(lock-protected or itself sharded) → refill. Contention scales with
*threads-sharing-a-CPU, but since only one thread runs per CPU at a time, the effective
contention on a given CPU's slab is naturally low. Cross-CPU contention only happens at
the central layer.

Subtle point: with per-PU caching, **a free may happen on a different CPU than the
alloc** (thread migrated, or object passed between threads), tcmalloc handles this - the
free goes to whatever CPU the freeing thread is on. This can cause slabs to slowly
imbalance, which tcmalloc corrects with periodic rebalancing. Jemalloc's per-thread model
has an analogous issue: an object allocated by thread A and freed by thread B lands in B's
tcache ("remote free"), which can strand memory in the wrong cache.

## Layer 5: Returning Memory to the OS

Worth comparing since it affects steady-state footprint:

- **jemalloc:** decay-based. Dirty pages are `madvise(MADV_DONTNEED)`'d (or `MADV_FREE`)
  after a configurable decay time. Two-phase (dirty → muzzy → clean) tunable via
  `dirty_decay_ms` / `muzzy_decay_ms`. Quite aggressive and tunable, which is why jemalloc
  often wins on RSS in fragmented long-running services.
- **tcmalloc:** background thread scavenges free pages back via `MADV_DONTNEED`; per-CPU
  caches have size limits and get shrunk. The bounded per-PU cache count means less to
  reclaim in the first place.

This is orthogonal to TLS-VS-RSEQ but matters when someone asks "which uses less memory" -
the answer depends on workload and tuning, and jemalloc's decay machinery is genuinely excellent.

## Layer 6: Full Systems-Level Verdict

Putting every layer together:

**Speed (hot path):** Effectively a tie in raw cycles. REQ swaps an atomic for a plain
store, but LS never needed the atomic in the first place. Neither uses barriers.

**Speed (at scale / real workloads):** tcmalloc's per-CPU design wins, driven by:

1. Bounded cache count → smaller working set of metadata → better cache/TLB hit rates.
2. Less cross-thread cache stranding.
3. Lower central-layer contention because per-CUslabs are naturally single-writer at any
   instant.

**When jemalloc matches or wins:**

- Thread count = core count (per-thread waste is negligible).
- Older kernels without RSEQ (tcmalloc falls back, advantage gone).
- Non-Linux or heterogeneous platforms (RSEQ is Linux-only).
- Heavily oversubscribed systems where RSEQ abort rates climb.
- Workloads where jemalloc's superior decay tuning dominates the metric you care about
  (RSS).

**One-sentence close:**

> RSEQ doesn't make the individual allocation dramatically cheaper - it makes the
> *isolation unit the CPU instead of the thread",which bounds cached memory by core count
> rather than thread count, On a many-core, many-thread modern Linux server that
> translates to better cache behavior and lower footprint, so tcmalloc tends to be faster
> in practice; but on kernels without REQ, on non-Linux, or at threadscore counts, the
> two converge and other design choices decide the winner.

## Interview Q&A
**Q: On the uncontended fast path, is tcmalloc's RSE allocation meaningfully cheaper in
cycles than jemalloc's TLS?** A: No - they're effectively a tie in raw cycles. Both avoid
atomics and barriers entirely: jemalloc's per-thread tache needs no synchronization
because only the owning thread touches it, and REQ replaces a `lock cmpxchg` with a plain
`mov` commit the kernel guards. RSEQ pays one extra load (the kernel-maintained CPU id)
and TS pays one extra indirection (thread pointer → tcache); the difference is in the
noise. The real advantage is structural, not per-op, *Follow-up:* If neither needs
atomics, what was REQ's atomic-elimination actually replacing? A hypothetical per-CPU
cache without* RSEQ would need `lock cmpxchg` to be migration-safe; RSEQ lets st use a
plain store instead - but TLS never needed the atomic in the first place. *Red flag:*
Claiming RSEQ makes individual allocations dramatically faster cycle-for-cycle.

**Q: What does REQ's "restartable" actually mean, and how does the kernel enforce it?**
A: Each critical section is described by a struct rseqes" (start IP, length, abort IP)
registered via a per-thread struct rseq. On every return to user space after preemption,
signal, or migration, the scheduler's resume path checks whether the interrupted IP is
inside the section but before the commit instruction; if so, it rewrites the userspace
program counter to the abort label. So an abort isn't an exception you catch - the kernel
literally moves your PC back to the retry point, and from user space the section "just
re-ran." *Follow-up:* What stops an attacker redirecting aborts into arbitrary code? A
`RSEQ_SIG` signature word placed just before the abort IP, which the kernel verifies
before fixing up the IP. *Red flag:* Describing the abort as a signal handler or a syscall
rather than a scheduler-side IP fixup.


**Q: The whole pitch is "bounded footprint." Explain the memory math. ** A: jemalloc's
cached-but-idle memory scales with *thread* count: roughly T threads × S size classes x
batch fill, so 10,000 threads strand enormous cache memory (the "cache stranding"
problem). tomalloc's per-CPU slabs scale with *core* count: ncpus × s x region size,
independent of thread count. With 128 CPUs versus 10,000 threads that's a ~78x reduction
in the number of caches - a bounded footprint is the dominant real-world advantage of the
RSEQ design, far more than cycle count. *Follow-up:* How does fewer caches translate into
*speed*, not just memory? Fewer, denser caches keep allocator metadata L2/L3-resident, So
the hot path misses less - effectively faster even though the instruction sequence is
identical. *Red flag:* Attributing tomalloc's practical win to per-op cycles rather than
cache count and metadata locality.

**Q: With per-CU caches, what happens when a thread allocates on one CPU and frees on
another, and how do both allocators handle cross-thread frees?** A: Under temalloc a free
goes to whatever cUthe freeing thread currently runs on, so slabs can slowly imbalance;
tcmalloc corrects this with periodic rebalancing. jemalloc has the analogous "remote free"
issue - an object allocated by thread A and freed by thread B lands in B's tache, which
can strand memory in the wrong cache. Neither model makes cross-thread free free of
consequences; they just manage the imbalance differently. *Follow-up:* Which model is more
prone to stranding memory in the "wrong" cache? jemalloc's per-thread model, because there
are far more thread caches than CPU caches for objects to get stranded in. *Red flag:*
Assuming allocation and free always happen on the same CPU/thread.

**Q: When does jemalloc match or beat tcmalloc, and what environment facts force the
choice?** A: jemalloc matches or wins when thread count core count (per-thread waste is
negligible), on kernels without REQ where tcmalloc falls back and loses its advantage, on
non-Linux or heterogeneous platforms (RSEQis Linux-only), under heavy oversubscription
where REQ abort rates climb, and on RSS-sensitive workloads where jemalloc's superior
decay tuning (`dirty_decay_ms`/`muzzy_decay_ms`, dirty→muzzy→clean) dominates. The RSEQ
facts that matter: it needs kernel 24.18, and glibc registers RSEQ itself since 2.35,
which tcmalloc must coexist with. *Follow-up:* Under extreme oversubscription, why does
REQ's edge erode? More runnable threads than cores means more preemptions/migrations
inside critical sections, raising abort rates and wasting the partial-sequence work. *Red
flag:* Recommending tcmalloc unconditionally without checking kernel version, platform, or
thread-to-core ratio.

**Q: Summarize the verdict in one sentence a principal engineer would accept.** A: RSEQ
doesn't make an individual allocation dramatically cheaper - it changes the isolation unit
from the thread to the CPU, bounding cached memory by core count instead of thread count,
so on a many-core, many-thread modern Linux server tcmalloc tends to be faster in practice
through better cache behavior and lower footprint; but on kernels without RSEQ, on
non-Linux, or at threadscore counts, the two converge and other design choices (like
jemalloc's decay machinery) decide the winner. *Follow-up:* So what single question most
changes the recommendation? Is thread count much larger than core count on a recent Linux
kernel - if yes, tmalloc's bounded-footprint design is the step-change; if no, it's a
wash. *Red flag:* Giving a universal " is always faster" answer without naming the
context dependence.
