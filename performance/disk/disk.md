<!-- performance/io/disk.md -->

# Disks and the Block Layer

A senior-level reference on storage devices (HDD/SSD/NVMe), the Linux block
layer (bio, request, blk-mq, I/O schedulers, the metrics that matter, why
`%util` lies on modern devices, and how to benchmark with `fio`.

## 1. Device types

| Property        | HDD                    | SATA/SAS SSD         | NVMe SSD (PCIe)        |
|-----------------|------------------------|----------------------|------------------------|
| Media           | Rotating platters      | NAND flash           | NAND flash             |
| Random latency  | ~5-10 ms (seek+rotate) | ~50-150 µs           | ~10-100 µs             |
| IOPS (random)   | ~100-200               | ~10k-100k            | ~100k-1M+              |
| Interface/queue | 1 queue                | 1 queue (AHCI, 32)   | up to 64k queues × 64k |
| Access pattern  | Sequential >> random   | Random ~ sequential  | Random ~ sequential    |

Key consequences:

- **HDD**: random I/O is catastrophic (seek + rotational latency). Sequential
  access and request merging/ordering matter enormously.
- **SSD/NMe**: no seek penalty; random is nearly as fast as sequential. But
  flash has **erase-before-write** (program/erase at block granularity),
  garbage collection, and **write amplification**. `fstrim`/`discard` tells the
  SSD which blocks are free. Endurance is finite (DWPD/TBW).
- **NVMe** replaces the single-queue AHCI model with many deep hardware queues,
  matching multi-core parallelism. This is why the single-`%util` model breaks
  (below).

## 2. The block layer

Path of an I/O: `VFS/page cache → block layer → device driver → device`.

- **bio** (`struct bio`): the basic unit of block I/O. Describes one transfer -
  target device, sector, and a list of memory segments (pages). A bio can be
  split or merged.
- **request** (`struct request`): one or more merged bios queued to a device.
  Adjacent bios are merged to form larger sequential requests (hugely important
  for HDDs).
- **request_queue**: the per-device queue the driver pulls from.

### blk-mq (multi-queue block layer)

The legacy single-queue block layer had one lock-protected request queue per
device - a scaling bottleneck for SSD/NVMe. **blk-mq** (default since kernel
5.0; the old path was removed) introduces:

- **Software queues**: per-CPU, so submission is lock-free across cores.
- **Hardware queues**: map to the device's actual queues (one for SATA, many
  for NVMe).

blk-mq is what lets Linux drive millions of IOPS on NVMe.

## 3. I/O schedulers

With blk-mq the available schedulers are:

| Scheduler | Behaviour | Best for
|---|---|---|
| `none` | No reordering; FIFO passthrough to hardware queues. | Fast NVMe/SSD, or hardware that schedules itself |
| `mq-deadline` | Deadline-based; bounds latency, still merges/sorts. | General purpose, SATA SSD/HDD; good default for latency guarantees |
| `bfq` | Budget Fair Queueing; proportional fairness, good interactivity. | Desktops, ixed workloads, HOD;	higher CPU cost |
| `kyber` | Lightweight, latency-target based, token throttling. | Fast multi-queue SSD/NVMe needing some fairness |

Inspect/set per device:

``` console
$ cat/sys/block/nvme0n1/queue/scheduler
[none] mg-deadline kyber bfg
* brackets = active
$ echo mq-deadline | sudo tee /sys/block/sda/queue/scheduler
```

Rule of thumb; `none` for fast MMe (the device/driver already parallelizes),
`mq-deadline for SATA SSD/HDD where request ordering helps and you want latency
bounds, `bfa" where fairness/interactivity matters.

## 4. Core metrics (USE method)

- **Utilization**: fraction of time the device had at least one in-flight
  request (`%util`).
- **Saturation**: queue depth / backlog - work waiting because the device is
  busy (`aqu-sz`, PSI `pressure/io`).
- **IOPS**: operations per second (`r/s` + `w/s`). Random-dominated.
- **Throughput / bandwidth**; bytes per second (`rkB/s` + `wkB/s`), Sequential-
  dominated.
- **Latency**: service time per request (`r_await`, `w_await`).

Little's law ties them together: `aqu-sz ≈ (IOPS) × (await in seconds)`

## 5. Why `%util` is misleading for NVMe/RAID

`%util` measures the fraction of wall-clock time during which **at least one**
request was outstanding. It was meaningful for a single-spindle HDD that can
service exactly one request at a time - 100% busy meant saturated.

Modern NVMe (and RAID arrays) service **many requests in parallel**, A device
can show `%util = 100%` while only 1 of its 64+ queues is busy - i.e. it is
nowhere near saturated and could do 50x more TOPS. So on NVMe/RAID:

- **Do not** treat `%util = 100%` as "disk is the bottleneck."
- Use **latency** (r_await*/ wawait rising vs baseline), **queue depth**
  (`aqu-sz` growing), achieved **IOPS/throughput vs the device spec**, and PSI
  (`/proc/pressure/io`) instead.

## 6. Benchmarking with fio

`fio` is the standard tool. Example 4K random-read IOPS test against a file
(keep test files small on shared hosts and delete them afterward):

```console
$ fio --name-randread --filename=/tmp/flotest --size=256M \
      --rw=randread --bs=4k --ioengine=libaio --iodepth=32 \
      --direct=1 --runtime=30 --time_based --group_reporting
...
  read: IOPS=142k, BN=556M1B/s (583MB/s)(16.3GiB/30001msec)
    slat (nsec): min=1200, max=... aVg=...
    clat (usec): min=30, max=..., avg=223.7, stdev=...
     lat (usec): avg=224.0
  ...
$ rm -f/tmp/fiotest
```

How to read it: `IOPS` and `BW` are the headline results; `clat` is completion
latency (device-side), `slat` is submission latency (kernel-side), `lat` is
their sum. Important knobs:

- `--direct=1` bypasses the page cache ('O_DIRECT) so you measure the device, not RAM.
- `--rw`: `read`/`write`/`randread`/`randwrite`/`randrw`.
- `--bs`: block size - small (4k) for IOPS, large (1M) for throughput.
- `--iodepth` + `--ioengine-libaio` (or `io_uring`): queue depth drives
  parallelism; a single NVMe needs depth > 1 to reach peak IOPS.
- `--numjobs` : parallel workers.

## Troubleshooting workflow

1. Identify the busy device and whether it is read- or write-bound: `iostat -x`.
2. Decide if it is truly saturated: on Ne/RAID look at latency + `aqu-sz` +
   achieved IOPS vs spec, **not** `%util`.
3. Characterize the pattern (random vs sequential, block size) from `rareq-s`,
   `rKB/s` ÷ `r/s`.
4. Attribute to a process (`pidstat -d`, `iotop`, `biotop`).
5. Reproduce / size with `fio` using the real pattern.

## Tuning knobs and trade-offs

- **Scheduler**: `none` (lowest overhead, no fairness) ↔ `bfa` (fairness,
  higher CPU).
- **`nr_requests`** (`/sys/block/*/queue/nr_requests`): queue depth; deeper ₌
  more merging/throughput but higher latency.
- **read_ahead kb***; larger helps sequential, wastes bandwidth on random.
- **rotational"** flag: tells the kernel HDD vs SSD; affects defaults.
- **`discard`/`fstrim`**; reclaim flash blocks; inline `discard` mount can add
  latency, periodic `fstrim.timer` is usually better.
- **Write cache** (`hdparm -w`, `nvme`): volatile cache improves throughput but
  risks data on power loss unless FLUSH/FUA is honored.

## Pitfalls

- Reading `%util` as saturation on NVMe/RAID.
- Benchmarking without `--direct=1` (you measure RAM).
- Using `iodepth=1` and concluding the SSD is sLow.
- Ignoring write amplification / GC when steady-state write performance drops.

## Likely interview questions

- Why is random I/O so much worse than sequential on HDDs but not on SSDS?
- What problem does blk-mg solve, and how do software vs hardware queues work?
- When would you pick `none` vs `mq-deadline` vs `bfa` vs `kyber`?
- Why is `%util` misleading on Me/RAID, and what do you use instead?
- IOPS vs throughput vs latency - which matters for which workload?
- How do you design a representative `fio` test?

## References

- Block layer / blk-mg:<https://docs.kernel.org/block/index.html>
- Block systs (scheduler, nr requests, read_ahead_kb):
  <https://docs.kernel.org/block/queue-sysfs.html>
- `fio`: <https://fio.readthedocs.io/>
- Brendan Gregg, USE method: <https://ww.brendangregg.com/usemethod.html>
