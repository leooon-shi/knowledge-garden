<!-- performance/io/how_analyze_system_io.md -->

# How to Analyze System I/O

A practical workflow and tool reference for diagnosing disk I/O problems on
Linux, from system-wide symptoms down to the exact file and syscall responsible.

## Workflow

1. **Confirm it's I/O**; `top`/`vmstat 1` show high `wa` (iowait), low
   user/system CPU, and processes stuck in `D` (uninterruptible sleep) state.
2. **Find the device**: `iostat -x 1`- which device is busy, read- or
   write-bound, and what the latency is.
3. **Judge saturation correctly**: on NVMe/RAID use `r_await`/`w_await`,
   `aqu-sz`, achieved IOPS vs spec, and PSt - not `%util`.
4. **Attribute to a process**: `pidstat -d 1`, `lotop`, `/proc/<pid>/io>`,
   `biotop`.
5. **Find files & syscalls**: `fletop`, `opensnoop`, `strace -e trace-file`,
   `lsof`, `biosnoop`.
6. **Explain & fix**, then re-measure.

## iostat -x

The primary per-device tool. Column layout is from `sysstat` fostat on Ubuntu 24.04;
the numbers are Illustrative (the study host's disks were idle):

```console
$ iostat -x 1 2
Device    r/s  rkB/s  rrqm/s %rrqm r_await rareq-sz  w/s  wkB/s wrqm/s %wrqm w_await wareq-sz  d/s dkB/s ... f/s f_await aqu-sz %util
nvme0n1  120.0 4800.0    5.0   4.0    0.35   40.0   80.0 3200.0  10.0   11.0   0.80    40.0  0.0   0.0 ...  2.0   0.90   0.11  18.0
```

How to read the key columns:

- **`г/s`, `w/s`** - read / write **requests** completed per second (this is
  IOPS, split by direction).
- **`rkB/s`, `wkB/s`** - KiB read / written per second (throughput). Divide by
  `r/s` to get average request size (or read `rareq-sz` /`wareq-sz` directly).
- **`rrqm/s`*, `wrqm/s`, `%rrqm`, `%wrqm`** - requests **merged** per second and
  the % merged. High merge% means the block layer is coalescing adjacent I/O
  (good for HDD sequential).
- **`r_await`, `w_await`** - **average latency** in ms per read/write request,
  *including* queue time + service time. This is the most important health
  signal. Rising `await` vs your baseline = the device is struggling.
- **`rareq-sz`, `wareq-sz`** - average request size in KiB; tells you random
  (Small) vs sequential (large).
- **`aqu-sz`** - average queue length (outstanding requests). `aqu-sz ≈ IOPS ×
  await`. Growing `aqu-sz` with rising `await` = real saturation.
- ***`f/s`, `f_await`** - flush requests/s and their latency (fsync/FUA) -
  newer sysstat column, very relevant to the Redis AOF case.
- **`%util`** - % of time at least one request was in flight. **Only** a
  saturation signal for single-queue HDDs; misleading on NVMe/RAID, which serve
  many requests in parallel (100% can still have spare capacity).

Always run `iostat -x 1` (or `-dxz`) to see live deltas; the first sample is
since boot and should be ignored.

## pidstat -d

Attribute I/O to processes:

``` console
$ pidstat -d 1
Linux ... (host)
UID  PID    kB_rd/s   kB_wr/s kB_ccwr/s iodelay  Command
0    9685      0.00  12500.00      0.00     145  redis-server
```

Columns: `kB_rd/s`/`kB_wr/s` = KiB read/written to the storage Layer per second;
`kB_ccwr/s = KiB whose pending writeback was cancelled (e.g. file trucated/
deleted before flush); `iodelay` = clock ticks the task was blocked on I/O
(block-layer + swap-in delay) - a direct per-process I/O-walt signal.

## iotop

Interactive, top-like view of per-process/thread I/O (`sudo lotop`), Shows
`DISK_READ`, `DISK_WRITE`, `SWAPIN`, and `IO>` (% time waiting on I/O), Use
`-o` to show only processes doing I/O, `-a` for accumulated totals.

## /proc/<pid>/io

Per-process cumulative counters:

```console
$ cat /proc/9685/io
rchar: 102400          # bytes read via read()-family (may hit cache)
wchar: 83000000        # bytes written via write()-family
syscr: 240             # read syscalls
syscw: 51600           # write syscalls
read_bytes: 0          # bytes actually fetched from the block layer
write bytes: 90000000  # bytes actually sent to the block layer
Cancelled_write_bytes: 0
```

Key distinction: `rchar`/`wchar` count bytes through the syscall interface
(a cached read inflates `rchar` but `read_bytes` stays 0). `read_bytes`/
`write_bytes` are the real device-level I/O.

## eBPF / bcc tools (precise attribution)

Part of the `bcc` package (usually under `/usr/share/bcc/tools`):

- **`biolatency`** - histogram ofblock I/O latency, Spots tall latency
  invisible in averages.
  
  ```console
  $ biolatency
       usecs:      : count     distribution
         64 -> 127 : 1200      |******               |
        128 -> 255 : 4500      |*********************|
      1024 -> 2047 : 37        |                     |
  ```

- **`biosnoop`** - per-I/O trace: timestamp, comm, PID, device, R/W, sector,
  bytes, latency (ms). Great for correlating a spike to a process.
- **`biotop`** - top-like, per-process block I/O (IOPS, Kbytes, avg latenfy).
- **`filetop`** - top-like, per-file reads/writes with TID and file name.
- **`opensnoop`** - traces open() calls (which paths, success/fallure).
 
## strace and lsof

- **`strace -f -e -trace=file -p <pid>`** - trace file-related syscalls (`open`,
  `stat`, `unlink`, …). Use `-e trace=desc` for `read`/`write`/`fsync`, add
  `-T -tt` to get per-call duration and timestamps (how the Redis case found
  the slow `fatasync`). Caveat: `strace` has high overhead; prefer eBPF for
  production.
- **`lsof -p <pid>`** - list a process's open files, mapping FD numbers to
  paths/sockets (how the Redis case identified `/data/appendonly.aof` as FD 7).
  `lsof -i` resolves TCP sockets to connections; `lsof <path>` finds who holds a
  file.
 
## Pressure Stall Information (PSI)

``` console
$ cat /proc/pressure/io
some avg10=0.00 avg60=0.00 avg300=0.00 total=92929638
full avg10=0.00 avg60=0.00 avg300=0.00 total=86125332
```

How to read it: `some` = % of time **at least one** task was stalled waiting on
I/O; `full` = % of time **all** runnable tasks were stalled (no useful work
possible). `avg10/60/300` are the running averages over 10/60/300 s; `total` is
cumulative microseconds. PSI is the modern, parallel-friendly saturation signal
and is far better than "%til on multi-queue devices. It also works per-cgroup
(`/sys/fs/cgroup/<grp>/io.pressure`).

## Pitfalls

- Trusting `%util` as saturation on NVMe/RAID.
- Reading the first `iostat`/`pidstat` sample (cumulative since boot).
- Confusing cached reads (`rchar`) with real device reads (`read_bytes`).
- Leaving `strace` attached in production (it serializes syscalls and slows the
  target).

# Likely interview questions

- Walk top → jostat→ pidstat→ strace/lsof to find an I/O hog.
- Explain every column of `iostat-x` you rely on, especially `r_awalt`,
  `aqu-sz`, `%util`.
- Why is PSI better than `%util` on modern hardware?
- Difference between `rchar`/`wchar` and `read_bytes`/`write_bytes` in
  `/proc/pid/io`.
- When do you reach for eBPF (`biosnoop`/`biolatency`) over `strace`?

## References

- `iostat(1)` / sysstat: <https://man7.org/linux/man-pages/mand/iostat.1.html>
- `pidstat(1)`: <https://man7.org/linux/man-pages/man1/pidstat..html
- bcc tools: <https://github.com/lovisor/bcc>
- PSI: <https://docs.kernel.org/account.1ng/psi.html>
- `/proc/<pid>/io`: <https://man7.org/linux/man-pages/man5/proc.5.html>
