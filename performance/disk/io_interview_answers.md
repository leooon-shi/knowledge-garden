k!-- performance/io/io_interview_answers.md -->
# I/O - Interview Question Answers
Answers to the "Likely interview questions" in the files under performance/io/, grouped by source file.
G arm_check.cp
## disk.md
*  *Why is random I/O so much worse than sequential on HDDs but not on SSDs?** On an HDD, every random access needs a mechanical
*  *rotational latency** (waiting for the sector to spin under it), about 5-10 ms
**seek** (moving the head) plus
each. That limits a drive to roughly 100-200 random IOPS, while sequential I/o pays the cost once and then streams. SSDs and NVMe drives have no moving parts.
Any flash address takes about the same time to reach (tens of ps), so random I/0 is close to sequential. Flash has its own costs instead: erase-before-write, garbage collection, and write amplification.
**What problem does blk-mq solve, and how do software vs hardware queues work?** The legacy block layer had **one lock-protected request queue per device**. Every core contended on that lock, so it could not scale to SSD/NVMe IOPS. blk-ma (the default since 5.0; the old path has been removed) splits queuing into two levels:
-   **Software (staging) queues** are per-CPU, so cores submit I/0 without lock contention, and merging and scheduling can happen there.
-   **Hardware dispatch queues** map onto the device's real submission queues: one for SATA/AHCI, up to 64k for NVMe.
This is what lets Linux drive millions of IOPS.
**When to pick 'none' vs "mq-deadline" vs "bfa" vs "kyber"?**
-   none: no reordering. Use it for fast NMe, or hardware that schedules I/O itself; it has the lowest overhead.
-   "mq-deadline: bounds latency with deadlines while still merging and sorting.A good default for SATA SSDs and HDDS.
-   "bfq: proportional fairness and good interactivity. Suits desktops, mixed workloads, HDDs, and io.weight setups, but costs more CPU.
-   "kyber': lightweight, latency-target-based token throttling. Suits fast multi-queue devices that need some fairness.
**Why is
*%util misleading on NVMe/RAID, and what do you use instead?**
•%uti1*
is the share of time with *at least one** request in flight. That equals
saturation only on a device that serves one request at a time (a single HDD).
NVMe and RAID serve many requests in parallel, so a device can show 100% with one of 64+ queues busy and still have plenty of headroom. Use these instead: latency (r_await /w_await rising above baseline), queue depth ("aqu-sz growing), achieved IOPS/throughput compared with the device spec, and PSI (/proc/pressure/io) -
**TOPS vs throughput vs latency - which matters for which workload?**
-   **IOPS** matters for small random I/O: OLTP databases, key-value stores, metadata-heavy work.
-  **Throughput** (MB/s) matters for large sequential I/O: backups, streaming, analytics scans, log shipping.
-   **Latency»*matters for anything user-facing or synchronous (fsync-bound commits). Check the tail (p99), not only the average.
Little's law links them: "aqu-sz = IOPS x await".
**How do you design a representative
•fio test?**
Copy the real workload: access pattern (*--rw=randread/randwrite/randrw with the right mix), block size (--bs*),
concurrency ("--iodepth plus "--numjobs"), and a
proper engine ("libaio"
"io_uring). Use
• --direct=1* so you measure the device
and not the page cache, Run long enough (--time_based --runtime*) to reach steady state; for SSD writes, precondition the drive 50 GC and write amplification show up. Report IOPS, bandwidth, and latency percentiles (clat"), not only averages.
Avoid iodepth=1" unless the app really runs at queue depth 1.
## disk_io_optimization,md
**Walk through optimizing a write-heavy service from app → fs → block → kernel,**
1.    **App** (the biggest wins): buffer and batch small writes into large ones, group commits, cut fsync frequency (or use "fdatasync"), and use async I/0 (io_uring) so threads don't block.
2.    **Filesystem**;"noatime / relatime; choose the Journal mode deliberately(data-ordered by default;
"writeback" only if you accept weaker crash
guarantees); tune "commit-N°; pick XFS for highly parallel workloads; align to the erase block and RAID stripe.
3. **Block**: scheduler (none" for NMe, "mq-deadline" for SATA),
nr_requests"
(queue depth),
• rotatíona1=0 on SSDs.
4
**Kernel writeback**; set "vm.dirty_bytes/ dirty_background_bytes to modest fixed values so writes flush smoothly instead of in storms; use group "io.max" or "io.weight" to isolate neighbors.
Change one knob at a time and measure the application-level metric before and after.
** vm.dirty_ratio vs vm.dirty_background_ratio, and why large-RAM hosts switch to '_bytes'.**
- dirty_background_ratio: once dirty pages reach this share of RAM, the background flusher starts writing them out asynchronously. Applications don't
stal1.
- dirty_ratio: once dirty pages reach this share, **the writing processes

block** and flush synchronously, which shows up as app stalls.
Both are percentages of RAM, so they grow with memory. On a 256 GB host the default 20% is about 50GB of dirty data, which ends in a huge flush storm and multi-second stalls. Fixed •*_bytes" values (a few hundred MB) keep writeback latency predictable no matter how much RAM the host has. Setting the bytes variant overrides the ratio.
**When is
mmap or "io_uring worth the complexity over buffered read/write?**
-  **mmap** is worth it for random access into large files, or files shared read-mostly between processes: no syscall per access, no extra copy. The costs: page-fault latency is unpredictable, I/errors arrive as SIGBUS, and durability still needs "msync.
-  **io_uring** is worth it at high IOPS or with many concurrent I/Os, where per-syscall overhead and thread-per-I/o blocking dominate. Shared submission and completion rings allow batched, async submission for buffered I/o, direct I/0, and sockets.
For simple sequential I/0 at modest rates, buffered read/ write is simpler and
usually just as fast.
**How do you isolate a noisy container's I/0 with group v2?** Put hard caps in "io.max" per device (MAJ:MIN rbps- wbps- riops- wiops-'), for example echo 259: wbps-10485760 wiops-1000">.../io.max. If you'd rather share fairly only under contention, use "io.weight" (proportional shares). To protect a latency-sensitive group, use "io.latency", which throttles the others.
Trade-off: hard caps waste capacity when the device is idle, while weights and latency targets only apply under contention and are less predictable.
**Why can reducing 'fsync frequency help more than any device tuning?**
Each
fsync"/fdatasync forces a **device cache flush** and waits for it, which takes milliseconds per call. An app that syncs every write is bound by flush latency (the Redis "appendfsync always case: about 7-8 ms per write), and no scheduler or queue tuning removes that wait. Batching (group commit) or relaxing the policy (everysec") cuts the number of flushes by orders of magnitude.
## how_analyze_system_io.md
**Walk top → iostat → pidstat → strace/lsof to find an I/O hog.**
1.    top/ vmstat 1°: high wa, low us/sy, and processes in "D state point to I/O.
2.    iostat -x 1: find the busy device, whether it is read- or write-bound, and its Latency (r await /'w_await,
3.    "pidstat -d 1° (or "iotop',
aqu-sz°).
(kB_rd/s,
• kB wr/s,
"biotop): find which process issues the I/O
4.
iodelay) -
strace -f-T -tt -e trace-descp ‹pid›: see which syscalls it makes and how long each takes.
5. Isof-p ‹pid›: map the FD numbers to file paths.
Then fix the cause and measure again.
**Explain the "iostat -x" columns you rely on.**
-r/s,
*w/s: completed read/write requests per second (IOPS).
- "rkB/s,
*wkB/s°: throughput.
•rareq-sz / wareq-sz give the average request size
(small means random, large means sequential).
*  rrqm/s,
*  wrqm/s,
-%rrqm,
*%wrqm: requests merged by the block layer.
-   ***r_await /w_await **: average latency per request in ms, queue time plus service time. This is the main health signal.while await rises, the device is really saturated.
-  f/s, fawait: flush requests and their latency (fsync/FUA).
-  **%util**;time with at least one request in flight. It means saturation only on single-queue HDDs.
Skip the first sample; it covers the time since boot.
**Why is PSI better than "%util on modern hardware?**
PSI (/proc/pressure/1o*) measures how much time **tasks actually stall** waiting on
I/0.
" some" is the share of time at least one task was stalled;
•full' is the share
of time all runnable tasks were stalled. That is real work lost, independent of how many requests the device can run in parallel. "%utilonly says whether something was in flight, which says nothing on parallel NVMe/RAID. PSI also works per cgroup (io pressure*).
** rchar*/'wchar" vs "read_bytes /'write_bytes" in "/proc/pid/io,**
" rchar" / wchar" count bytes that went through the **syscall interface**, including reads served from the page cache, A cached read raises rchar while read bytes stays e.
read_bytes write_bytes count bytes that actually reached or came from
the **block layer** (real device I/O).
**When do you reach for eBPF ("biosnoop/biolatency") over 'strace ?** In production, or whenever overhead matters.
strace
uses ptrace and stops the
target on every syscall, which slows it badly, eBPF tools run in the kernel with 1ow overhead and see the **block layer**; per-I/Olatency (biosnoop) and latency histograms (biolatency") that expose tail latency averages hide. Use strace for short, targeted syscall-level questions, ideally outside production.
## linux_filesystem.md
**Describe the four FS objects and how an "fd maps down to an inode.**
- **superblock**: one mounted filesystem instance (block size, counts, root dentry,
"super operations').

**inode**: one file's metadata (type, mode, owner, size, timestamps, tank count, block pointers). It holds no name.
-  **dentry**: one path component mapped to an inode. Dentries are an in-memory cache that speeds up path lookup.
-  **file**: an open file description (offset, access mode, pointer to the dentry).
Mapping: the process **fd table** entry → struct file (open file description) →
**dentry** → **inode**. same offset; a second open) *
•dup/fork share the same struct file and therefore the creates a new description.
*  *Why does an inode not store the file name? What makes hard links possible?** Names live in
*  *directory entries** (dentries), which map a name to an inode
number. Because the name is separate from the inode, several directory entries can point to the same inode: those are **hard links**. The inode's link count tracks how many names exist, and the data is freed only when the count reaches e and no process holds the file open.
**Explain the three ext4 •data- modes and what each protects against.**
-   "data-journal': journals both metadata and data. Safest (no stale or torn data after a crash)but slowest, since data is written twice.
-   "data-ordered(default): journals metadata only, but forces file data to disk
**before** the metadata that references it commits. After a crash, a file never points at blocks containing someone else's old data.
- "data-writeback": journals metadata only, with no ordering. Fastest, but after a crash a file can show **stale data** from earlier owners of its blocks.
***€sync vs fdatasync; why fsync the directory after creating a file?**
"fsync" flushes the file's data **and all its metadata** (including non-essential fields like "mtime") and issues a device cache flush.
•fatasync flushes data plus
only the metadata needed to read the data back (such as size) and skips the rest, so it is cheaper, but it still includes a full device flush. The **name** of a new or renamed file lives in its directory, not in the file's inode, so fsyncing the file doesn't make the directory entry durable. You must also •fsync the
*  *containing directory**, or the file can disappear after a crash.
*  *When does "O_DIRECT help and when does it hurt? What alignment rules apply?** It helps when the application caches better than the kernel, for example a database with its own buffer pool (InnoDB). Bypassing the page cache avoids caching the same data twice and gives predictable behavior. It hurts workloads that benefited from the page cache and read-ahead (repeated or sequential reads), because every access now goes to the device. Alignment: the buffer address, file offset, and length must all be aligned to the logical block size (use "posix_memalign"); otherwise the call fails with •EINVAL. "O_DIRECT" does **not** imply durability; you still need fsync / O_DSYNC.
*  *How can a filesystem be "full" with free space, and how do you confirm it?** It can run out of **inodes**, typically from millions of tiny files, while blocks are still free. Check with df -i (IUse% at 100%). ext fixes the inode count at
*  mkfs* time; XFS allocates inodes dynamically and rarely hits this. Another cause: space held by **deleted-but-still-open** files, which "df counts but • du doesn't see. Find them with •1sof +L1'.
## flooding_log_events_practice.m
**What does a process in "D' state plus high "iowait" tell you?**
The process is blocked in the kernel on uninterruptible I/0, and the CPUs are idle waiting for that I/0 to finish. In short, the workload is bound by I/O, not CPU.
Next, find out which device and which process (iostat -x,
"pidstat -d'), and
remember that high iowait alone doesn't prove the device is saturated.
**why is synchronous per-line log "fsync" so damaging, and how does async logging fix it?** Every
"fsync forces a device flush and waits for it (about 58 ms per call in the
case study). The request thread blocks for that whole time, and under load the flushes queue behind each other and saturate the disk. Async logging hands records to a background thread or queue (QueueHandler"/ QueueListener), so request threads never wait on disk, Dropping per-message fsync lets the page cache batch writes. The only data at risk is the last few buffered lines if the process crashes, which is acceptable for most logs.
**How do you distinguish application-log writes from request-data writes?**
Follow the chain: "pidstat -d → strace -e trace=write, fsync (the payloads look like log lines: timestamps, INFO/DEBUG) → "1sof -p <pid› to map the FD (/var/log/app/app.1og*).
•filetop (bcc) also shows per-file write volume directly.
**When is *%util=100% meaningful (HDD/SATA) vs misleading (NVMe)?** On a single-queue HDD or SATA device, 100% means the device is busy all the time, i.e. saturated. On NVMe/RAID it only means at least one request was in flight, so check "w_await" against baseline,
*aqu-sz, achieved IOPS/throughput against the
spec, and PSI.
**How would you bound disk usage and I/O from logging going forward?** Log at INFO / WARN in production (not "DEBUG ), 10g asynchronously, don't fsync per message, rotate logs (logrotate / RotatingFileHandler") with size and retention caps, sample or rate-limit noisy events, and ship structured logs off-host to a log pipeline. A group "io max" cap can serve as a last-resort guard.
## slow_sq1_query_practice.md

*  *Why does "MyISAM depend on the OS page cache while InnoDB does not?** MyISAM caches **only indexes** in its own key buffer. Row data is read through the os **page cache**, so its performance depends on the kernel keeping the data file cached. InnoD has its own **buffer pool** (innodb_buffer_pool_size") that caches both indexes and data inside the process (often opened with "O_DIRECT"), so it doesn't rely on, and isn't hurt by, page-cache eviction or "drop_caches.
*  *What do 'umstat"s "bi", "bo", and "wa" columns mean, and how do they relate to "iostat ?**
"bi is blocks read in from block devices per second, "bo is blocks written out per second, and "wa" is CPU iowait%. They are a system-wide summary.
iostat -x"
breaks the same activity down per device (rkB/s / wkB/s match bi/bo*) and adds latency and queue depth, so you can see *which* device and *how* busy.
*  *What do the three values of 'drop_caches do? Why is dropping caches in a loop harmful?**
*  1 drops clean page cache,
*2° drops reclaimable slab (dentries and inodes),
drops both. Run "sync" first, since only clean pages are dropped. Doing it in a loop throws away the working set over and over, so every read goes back to disk. That is exactly what caused the high bi/ wa" and slow queries in this case. It is a diagnostic tool only; the kernel reclaims cache by itself when needed.
**How would you use eBPF tools ("filetop, "opensnoop,
biosnoop) to attribute I/0?**
-filetop shows per-file read/write volume with TID and command (which files are hot), "opensnoop" shows which paths are opened and by whom (it revealed the
temporary •*.txt* churn), and
biosnoop shows each block I/O with PID, device,
sector, size, and latency (which process causes which disk I/O and how slow it is).
Together they get you from "the disk is busy" to the exact process and file.
*  *What is index selectivity/cardinality, and how do you choose a prefix-index length?**
*  *Cardinality** is the number of distinct values. **Selectivity** is cardinality + total rows, ranging from 1/#rows to 1. Higher selectivity lets the index filter out more rows; a unique index has selectivity 1. To size a prefix index, compute the full-column selectivity
( COUNT (DISTINCT col)/COUNT(*)"), then increase N in
- COUNT(DISTINCT LEFT (col,))/COUNT (*) until the result gets close to the full value.
Pick the shortest N that gets close. Prefix indexes are mandatory for
•BLOB" / TEXT*/very long "VARCHAR" columns.
## slow_redis_reaction_practice.md
*  *Compare Redis RDB vs AOF; what does each "appendfsync" value trade off?**
*  *RDB** writes periodic point-in-time snapshots from a forked child. It is compact and restarts fast, and costs little at runtime, but you lose everything since the last snapshot. **AOF** appends every write command, which is more durable but produces larger files and needs rewrites.
"appendfsync":
"always: fsync on every write. You lose at most one command, but every write pays flush latency, which makes it the slowest.
everysec (default): fsync once per second from a background thread. You lose up to about 1s of data, with good performance.
- "no: the DS flushes when it chooses (typically ~30 s). Fastest and least durable.
Redis 7 adds multi-part AOF (an RDB base plus incremental AOF files), which changes how rewrites work but not this durability trade-off.
*  *Why is 'fdatasync" cheaper than "fsync", and why is it still slow?** It skips metadata that isn't needed to read the data back (such as "mtime"), saving a metadata write, It still forces the data to stable storage, including a **device cache flush**, and waits for it to finish, which costs milliseconds (about 7-8 ms here). Calling it on every write makes Redis's single-threaded command loop wait on
*  *How do you read syscall latency from "strace -T output?**
•-T adds "‹seconds›" at the end of each line; the time spent inside that syscall.
*-tt"adds wall-clock timestamps with us precision. Look for calls whose duration is far above their neighbors: *fdatasync(7) <0.005415> against sub-millisecond
"read" / write"
calls, The gaps between timestamps also show how often each call
happens (here an " fdatasync" about every 18 ms),
**How would you attribute disk writes
a specific FD and then to a client
connection?*
"strace shows which FD gets the "write"/ fdatasync" calls (FD 7). maps FDs to objects (FD 7-*/data/appendonly.aof", FD 8 - a TCP socket). From the strace sequence, each "GET" returning "good" on FD 8 is followed by a 'SADD' that is written to FD 7.
*1sof -1 (inside the right network namespace) resolves the FD 8
socket to *localhost:6379->localhost:32996*, and port 32996 belongs to the Python app (PID 9181).
**How do container network namespaces affect "Isof/ss" visibility, and how do you work around it?** Each container has its own network namespace. From the host's namespace, and ss" don't show the container's sockets or don't resolve its connections, so you can't see the peer. Enter the container's namespace with
nsenter --target ‹container-pid› --net -- 1sof -1 (or ss -tanp), getting the PID from "docker inspect --format "(f.State.Pid})". You can also run the tool inside the container ("docker exec") or attach a debug container that shares its namespace (kubectl debug').