<!-- performance/io/disk_io_optimization.md -->

# Disk I/O Optimization

A layered guide to optimizing disk I/O: application level, filesystem level,
block/device level, and kernel writeback/group controls - with the trade-offs
of each knob.

## 1. Application level

The biggest wins are almost always in the application, by doing *less* I/O or
*larger, fewer* I/O operations.

- **Buffering**: batch many small writes into a buffer and flush in large
  chunks. Each syscall and each small random I/O has fixed overhead; a user-
  space buffer (e.g. `Bufferedwriter`, `fwrite`, stdio, a log aggregator) turns
  thousands of tiny writes into a few large sequential ones.
- **Batching / coalescing**: group related reads/writes (e.g. multi-row DB
  inserts, pipelined requests) to amortize round-trips and `fsync`s.
- **mmap**: map a file into memory and access it as bytes; the kernel faults
  pages in on demand and writes dirty pages back lazily. Avoids explicit
  `read`/`write` syscalls and an extra userkernel copy. Trade-off: page-fault
  latency is unpredictable, errors arrive as `SIGBUS`, and you still need
  `msync` for durability. (See `mmap_traps.md` for the sharp edges.)
- **Asynchronous I/O / io_uring**: don't block a thread per I/O. Legacy Linux
  AIO (`libaio`) only worked well with `O_DIRECT`. **io_uring** (kernel 5.1+) is
  the modern interface: shared submission/completion ring buffers between user
  space and kernel, batched submission, works for buffered *and* direct I/O,
  sockets, and more. It drastically cuts syscall overhead at high IOPS.
- **Fewer fsyncs**: every `fsync`/`fdatasync` is a device flush costing
  milliseconds (see the Redis AOF case: `appendfsync always` → `fdatasync` on
  every write → ~7-8 ms each). Group commits, relax the sync interval
  (`everysec`), or use `fatasync` instead of `fsync` where metadata isn't
  needed.

## 2. Filesystem level

Controlled mostly via mount options and the journal mode.

- **noatime/relatime**: stop updating the access time on every read.
  `noatime` is the biggest easy win for read-heavy workloads (otherwise each
  read dirties the inode and causes a metadata write). `relatime` (the modern
  default) only updates `atime` occasionally. Trade-off: breaks tools that rely
  on exact `atime` (rare).
- **ext4 journal mode (`data=`)**: `ordered` (default, safe) → `writeback`
  (faster, can expose stale data after a crash) → `journal` (safest, slowest,
  double writes). Pick `writeback` only when you can tolerate the weaker
  guarantee.
- **`nobarrier`/`barrier=0`**: hisorically disabled flush barries for speed -
  **dangerous**; risks corruption on power loss and is largely obsolete, Do not
  use on real data.
- **`commit=N`** (ext4): seconds between journal commits; larger = fewer
  metadata writes, more data at risk on crash.
- **Choose the right filesystem**: XFS for large, highly parallel, high-IOPS
  workloads; ext4 for general use.
- **Alignment**: align partitions/filesystem to the SSD erase block and RAID
  stripe to avoid read-modify-write.

## 3. Block / device level

Tunables under `/sys/block/<dev>/queue/`.

- **I/O scheduler**; "none" for fast Me, mq-deadline for SATA SSD/HDD with latency bounds,
"bfa" for fairness. Trade-off: fairness/ordering vs CPU
overhead and latency.
- **Read-ahead (`read_ahead_kb`)**: increase for sequential/streaming reads
  (fewer, larger I/Os); decrease for random to avoid wasted bandwidth and cache
  pollution.
- **Queue depth (`nr_requests`)**: deeper queues allow more merging and higher
  throughput, at the cost of higher worst-case latency.
- **`rotational`**; ensure it is `0` for SSD/NVMe so the kernel uses SSD-
  friendly defaults,
- **`add_random`**; set `0` on SSDs so block I/O doesn't feed the entropy pool
  (minor CPU/latency saving).

## 4. Kernel writeback (dirty page) sysctis

These control how much dirty page cache can accumulate before the kernel forces
writeback - critical for smoothing or bounding write bursts.

| Sysctl                        | Meaning                                                        |
|-------------------------------|----------------------------------------------------------------|
| `vm.dirty_background_ratio`   | % of total RAM of dirty pages at which background flusher starts (async).|
| `vm.dirty.ratio`              | % of RAM at which writing processes themselves block* and flush synchronously. |
| `vm.dirty_background_bytes` / `vm.dirty_bytes` | Same, in absolute bytes (set one pair; the bytes version overrides ratio). |
| `vm.dirty_expire_centisecs`   | Age (in 1/100 s) after which a dirty page is eligible for writeback. |
| `vm.dirty_writeback_centisecs`| How often the flusher wakes up.                                |

Trade-offs:

- **Large `dirty_ratio`** absors big waite bursts in RAM (low lateney during
  the burst) but creates a huge flush later, and risks more data loss on crash.
  On a 256 GB box, the default 20% = ~50 GB of dirty data - a flush storm that
  can stall everything. Large-RAM servers usually set `dirty_bytes` /
  `dirty_background_bytes` to fixed, modest values (e.g. a few hundred MB) for
  predictable latency.
- **Small ratios** flush sooner and more often: smoother, more predictable
  latency, but less batching/merging and potentially lower peak throughput.

## 5. cgroup v2 I/O control (`io.max`)

group v2 can throttle or weight block I/O per control group - essential for
noisy-neighbor isolation (containers, multi-tenant).

- **`io.max`** sets hard caps per device (`MAJ:MIN`) for `rbps`, `wbps`,
  `riops`, `wiops`:
  
  ```console
  # Limit group to 10 MB/s write and 1000 write IOPS on device 259:0
  $ echo "259:0 wbps=10485760 wiops=1000" > /sys/fs/cgroup/<grp>/io.max
  ```

- **`io.weight`** does proportional sharing (works with the BFQ scheduler /
  cost model) - fairer than hard caps when the device is contended but idle-
  friendly.
- **`io.latency`** sets a target latency; the kernel throttles other cgroups to
  protect a latency-sensitive one.

Trade-off: hard `io.max` caps waste capacity when the device is otherwise idle;
weights/latency targets only kick in under contention but are less predictable.

## Measurement-driven workflow

Always measure before and after a change (same workload):

1. Baseline with `iostat -x 1`, `pidstat -d 1`, PSI `/proc/pressure/io`.
2. Change **one** knob.
3. Re-measure latency (`r_await`/`w_await`), IOPS/throughput, and app-level
   latency.
4. Keep it only if the app metric improved; many device-level knobs don't move
   the end-to-end number.

## Pitfalls

- Tuning device knobs when the real problem is too many small fsync's in the
  app.
- Setting a huge dirty_ratio and getting periodic multi-second stalls during
  flush storms.
- `nobarrier` / disabling flushes for speed - silent corruption risk.
- Applying `io.max` caps so tight that latency gets worse than the contention
  you were avoiding.
- Benchmarking with warm cache and concluding the change helped.

## Likely interview questions

- Walk through optimizing a write-heavy service from app → fs → block → kernel.
- Explain `vm.dirty_ratio` vs `vm.dirty_background_ratio` and why large-RAM
  hosts switch to `*_bytes`.
- When is `mmap` or `io_uring` worth the complexity over buffered `read`/`write`?
- How do you isolate a noisy container's I/O with group v2?
- why can reducing `fsync` frequency help more than any device tuning?

## References

- Writeback / dirty sysctl:
  <https://docs.kernel.org/admin-guide/sysctl/vm.html>
- Block queuesysfs: <https://docs.kernel.org/block/queue-sysfs.html>
- cgroup v2 I/O controller:
  <https://docs.kernel.org/admin-gutde/cgroup-v2.html#io>
- io_uring: <https://kernel.dk/1o_uring.pdf>
