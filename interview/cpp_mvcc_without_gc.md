<!-- interview/cpp_mvcc_without_gc.md -->

# How a C++ Storage Engine Implements MVCC (Without a GC)

MCC (Multi-Version Concurrency Control) lets readers never block writers and writers
never block readers: each transaction sees a consistent **snapshot** as of some point in
time. The engine keeps **multiple physical versions** of each logical row, tagged with the
transaction that created (and later deleted) them; a reader picks the version visible to
its snapshot.

The consequence: old versions accumulate. In a GC language, dead versions are collected
once no reference remains. **C++ has no GC, so the engine must implement its own
reclamation** - decide when a version is provably invisible to every possible reader, and
only then free it. That "when is it safe to free?" question is the whole heart of the
problem, and it is fundamentally the same problem as **safe memory reclamation (SMR)** in
lock-free programming.

## 1. Concept - versions and visibility

Each version carries transaction timestamps, conceptually:

```cpp
struct Version {
    TxnId    xmin;      // txn that created this version
    TxnId    xmax;      // txn that deleted/superseded it (0 = live)
    Version* older;     // pointer to previous version (version chain)
    std::byte payload[]; // the actual tuple data
};
```
A transaction gets a **snapshot** (a read timestamp / commit-sequence-number, plus the set
of in-flight tans). Visibility rule, roughly: a version is visible if `xmin` is committed
and ≤ my snapshot, and `xmax` is either e or belongs to a tan not committed as of my
snapshot. Readers walk the version chain and pick the first visible version - no locks
needed.

Two schools of physical layout:

- **Append/delta storage (Postgres-style):** new versions are new heap tuples; the chain
  lives in the table. Dead tuples reclaimed by `VACUUM`.
- **In-memory version chains (Hekaton / HyPer-style):** each row points to a linked list
  of versions in a version store; newest first.

## 2. Practice - reclamation is the hard part in C++

You cannot free a version the moment its `xmax` commits, because a long-running
transaction with an older snapshot may still read it. Freeing too early is a
**use-after-free**; never freeing is a **memory leak / version-store bloat**. So you
compute a **low-water mark**: the oldest snapshot held by any active transaction. Any
version whose entire visible lifetime is below that watermark is dead to everyone and can
be freed.

This is exactly a **safe memory reclamation** problem. The standard C++ techniques:

### (a) Epoch-Based Reclamation (EBR)

Maintain a global epoch counter. Each thread, on entering an operation, "pins" the current
epoch (announces it in a per-thread slot). Retired versions go on a per-epoch limbo list
instead of being freed. Memory from epoch `e` can be freed only once every thread has
advanced past `e`. Cheap on the hot path (a relaxed store to pin), amortized reclamation
in the background. The workhorse for in-memory MVCC engines.

```cpp
// Simplified EBR
std::atomic<uint64_t> global_epoch{0};
thread_local uint64_t local_epoch;             // announced in a registry slot
std::array<std::vector<Version*>, 3> limbo;    // ring of 3 epochs

void retire(Version* v) {
  limbo[global_epoch % 3].push_back(v);       // don't free yet
}

void try_reclaim() {
    if (all_threads_past(global_epoch)) {       // grace period elapsed
        auto& bin = limbo[(global_epoch - 2) % 3];
        for (auto* v : bin) delete v;           // now provably safe
        bin.clear();
        global_epoch.fetch_add(1);
    }
}
```

### (b) Reference counting

Wrap versions in `std::shared_ptr` or a hand-rolled intrusive refcount; the version frees
when the last reader drops it, Simple and precise, but the atomic inc/dec on every read
causes cache-line contention on hot rows and adds latency - most high-performance engines
avoid `shared_ptr` on the read path for this reason. Intrusive refcounts with careful
placement are sometimes used.

## (c) Hazard pointers

Each reader publishes the pointer it is currently dereferencing; the reclaimer won't free
anything a hazard pointer references. More granular than EBR, bounded garbage (robust to
stalls), but higher per-access cost (a store + fence per protected pointer). C++26
standardizes std: hazard pointer.

### (d) Watermark/oldest-active-transaction GC (the DB-native approach)

Rather than generic SMR, the transaction manager tracks the minimum snapshot timestamp
among active transactions (`oldest_active_snapshot`). A background "vacuum" / "version
cleanup" thread walks version chains and prunes any version fully older than that
watermark. Postgres `VACUUM`, InnoD's **purge threads** (dropping undo-1og versions), and
Hekaton's cooperative + background Call work this way. This is really EBR with the
"epoch" being the transaction/commit sequence number.

## 3. InnoDB concretely (undo logs, not copies)

InnoDedoes not store full row copies. The current row lives in the clustered index; older
versions are **reconstructed on demand** by applying **undo log** records backward. Each
row has hidden `DB_TRX_ID` and `DB_ROLL_PTR` columns; a read view walks the roll pointer
through undo records to build the version its snapshot should see, Reclamation =
**purge**: once no read view can see a given undo record (its try id is below the minimum
active read view), background purge threads free the undo pages and remove delete-marked
records. Same watermark idea, applied to undo.

## 4. Staff / Principal-level considerations

- **Choosing the reclamation scheme is a latency/throughput/footprint tradeoff.** EBR:
  fastest reads, but a single stalled thread that forgets to unpin stalls *all*
  reclamation → unbounded memory growth (the classic EBR failure mode). Hazard pointers:
  bounded garbage, robust to stalls, but per-read cost. Recounting: precise and simple,
  but contention and cache traffic. Real engines use EBR/watermark for the common path
  with safeguards.

- **The long-running-transaction problem is operational, not just theoretical.** One
  idle-in-transaction reader pins the global watermark and prevents reclamation of
  *everything* newer → version-store/undo bloat, table bloat, degraded scans. Hence
  Postgres's `idle_in_transaction_session_timeout` and InnoDB's history-list-length
  monitoring. Principal-level answer: design back-pressure and monitoring around
  the watermark, not just the algorithm.

- **Batching and amortization.** Freeing one version at a time is cache-unfriendly and
  lock-heavy. Retire to per-thread limbo lists, reclaim in batches on epoch flip, and free
  memory back to a **slab/arena allocator** rather than the general allocator to avoid
  fragmentation and "malloc" contention. Version objects are ideal for pool allocation
  (fixed-ish size, high churn).

- **Allocator design matters as much as the algorithm.** Because versions churn
  constantly, engines use arena/slab allocators, sometimes with per-core heaps, so
  reclamation is "return chunk to pool," not `free()`. This is where "no GC" becomes an
  advantage: deterministic, contention-free reclamation with no stop-the-world pauses.

- **Correctness hinges on memory ordering.** Publishing a new version head must be a
  release store; readers acquire-load it. Epoch pinning needs the right fences so a reader
  can't be seen as "not pinned" while it still holds a pointer. Getting the
  `std:memory_order` right here is the subtle, principal-level part.

- **Interaction with the commit protocol.** Version visibility is tied to *commit*
  ordering, not creation ordering, so you need atomic commit-timestamp assignment (a
  monotonic counter or an LSN) and careful handling of the window between "assigned a
  commit ts" and "made visible." Group commit and the ordering of install versions vs.
  publish commit ts" is where real engines get subtle bugs.

## Interview Q&A

**Q: What is MVCC and what core problem does it create in a language without a GC?** A:
MVC (Multi-Version Concurrency Control) lets readers never block writers and vice versa
by giving each transaction a consistent snapshot: the engine keeps multiple physical
versions of each row tagged with the transaction that created and later deleted them, and
a reader picks the version visible to its snapshot. The consequence is that old versions
accumulate, In a GC language they'd be collected once unreferenced, but C++ has no GC, so
the engine must implement its own reclamation - deciding when a version is provably
invisible to every possible reader before freeing it. That "when is it safe to free?"
question is the heart of the problem. *Follow-up:* What well-known class of problem is
this reclamation? - Exactly safe memory reclamation (SMR), the same problem as in
lock-free programming. "Red flag;* Not recognizing that the hard part is reclamation, or
assuming you can free a version as soon as 1ts deleter comnits.

**Q: How does a reader decide which version is visible, and why does that mean you can't
free on delete-commit?** A: Each version carries `xmin` (creating txn) and `xmax`
(deleting/superseding txn, e if live) plus a pointer to the older version, A transaction
holds a snapshot (a read timestamp plus the set of in-flight txns); a version is visible
if `xmin` is committed and ≤ the snapshot and `xmax` is e or belongs to a txn not
committed as of the snapshot, Readers walk the chain and take the first wisible version,
no locks, You can't free a version the moment its `xmax` commits because a long-running
transaction with an older snapshot may still need to read it - freeing early is a
use-after-free, never freeing is a leak. *Follow-up:*What quantity lets you safely free?
- The low-water mark: the oldest snapshot held by any active transaction; versions whose
entire visible 11fetime is below it are dead to everyone. *Red flag:* Freeing on
delete-commit, or not knowing the oldest-active-snapshot watermark.

**Q: Explain epoch-based reclamation and its characteristic failure mode.** A: EBR
maintains a global epoch counter; each thread "pins" the current epoch (a cheap relaxed
store to a per-thread slot) when entering an operation. Retired versions go onto a
per-epoch limbo 1ist rather than being freed, and memory from epoch `e` is freed only once
every thread has advanced past `e`. It's cheap on the hot path and reclaims in batches in
the background - the workhorse for in-memory MVCC engines. Its classic failure mode: a
single stalled thread that forgets to unpin (never advances its epoch) stalls *all*
reclamation, causing unbounded memory growth. *Follow-up:* How is the DB-native watermark
GC related to EBR? - It's essentially EBR where the "epoch" is the transaction/commit
sequence number, pruning versions older than the oldest active snapshot. *Red flag:* Not
knowing a stalled pinned thread blocks all EBR reclamation.

**Q: Compare the SMR schemes - EBR, reference counting, and hazard pointers - for MVCC
version reclamation.** A: EBR gives the fastest reads (a relaxed store to pin) with
amortized background reclamation, but a stalled thread blocks everything. Reference
counting (shared_ptr or intrusive) is simple and precise - a version frees when the last
reader drops it-but the atomic inc/decon every read causes cache-line contention on hot
rows and adds latency, so high-performance engines avoid shared_ptr on the read path.
Hazard pointers are more granular (each reader publishes the pointer it's dereferencing;
the reclaimer won't free a referenced pointer) with bounded garbage robust to stalls, but
cost a store plus fence per protected pointer. Real engines use EBR/watermark on the
common path with safeguards. *Follow-up:* Which of these did C++26 standardize? -
`std::hazard_pointer` (and `std::rcu`). *Red flag:* Recommending shared_ptr refcounting on
a hot read path, ignoring its contention cost.

**Q: How does InnoD implement MCC, and how is it different from storing full row
copies?** A: InnoDB doesn't store full row copies. The current row lives in the clustered
index, and older versions are reconstructed on demand by applying undo-log records
backward: each row has hidden `DB_TRX_ID` and `DB_ROLL_PTR` columns, and a read view walks
the roll pointer through undo records to build the version its snapshot should see.
Reclamation is "purge": once no read view can see a given undo record (its try id is below
the minimum active read view), background purge threads free the undo pages and remove
delete-marked records - the same watermark idea applied to undo. *Follow-up:* Contrast
with the Postgres-style approach. -Postgres uses append/delta storage where new versions
are new heap tuples in the table, reclaimed by `VACUUM`; Hekaton/HyPer-style engines keep
in-memory version chains in a version store. *Red flag:* Claiming InnoDB keeps full
physical row copies per version.

**Q: Debugging scenario - the version store keeps growing and scans slow down, but write
volume is normal. What's the likely cause?** A: Almost certainly a long-running /
idle-in-transaction reader. One such transaction pins the global watermark (oldest active
snapshot), preventing reclamation of *everything* newer, so the version store/undo and
table bloat and scans degrade. It's an operational problem, not just algorithmic - the fix
is back-pressure and monitoring around the watermark, e.g., Postgres's
`idle_in_transaction_session_timeout` or watching InnoDB's history-list length.
*Follow-up:* Why doesn't the algorithm alone solve it? - Any correct SMR scheme must keep
versions a stalled/old reader can still see; the defense is operational (timeouts,
monitoring), not a cleverer free. *Red flag:* Treating unbounded version growth as a pure
allocator issue and ignoring long-running transactions.

**Q: Design trade-off - why is the allocator design as important as the reclamation
algorithm, and where is "no G" actually an advantage?** A: Because versions churn
constantly, freeing one at a time is cache-unfriendly and lock-heavy. Engines retire to
per-thread limbo lists, reclaim in batches on epoch flip, and return memory to a
slab/arena allocator (often per-core) rather than the general allocator - turning
reclamation into "return chunk to pool" instead of `free()`. This is where "no GC" becomes
a feature; deterministic, contention-free reclamation with no stop-the-world pauses.
Correctness still hinges on memory ordering (publish a new version head with a release
store, acquire-load it; pin epochs with the right fences) and on tying visibility to
conmit ordering via atomic commit-timestamp assignment, *Follow-up:* Why are version
objects ideal for pool allocation? - They're fixed-ish size and high churn, so slab/arena
allocation avoids fragmentation and malloc contention. *Red flag:* Focusing only on the
reclamation algorithm while ignoring allocator design and memory ordering.

## Synthesis

C++ has no GC, so an MCC storage engine **is its own garbage collector**, specialized to
the one fact that makes the problem tractable: a version is dead once it is older than the
oldest active snapshot, The engine computes that watermark (via epochs, hazard pointers,
refcounts, or an oldest-active-tn timestamp), retires dead versions to 1imbo lists, and
reclaims them in batches into arena/slab allocators. The absence of GC is a feature here -
it buys deterministic, pause-free reclamation - at the cost of having to solve safe memory
reclamation explicitly and defend against the long-running-transaction watermark-pinning
failure mode.
