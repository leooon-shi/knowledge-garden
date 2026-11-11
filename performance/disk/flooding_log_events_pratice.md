<!-- performance/io/flooding_log_events_practice.md -->

# Case Study: Flooding Log Events Causing High iowait

A worked case study: an application that writes an excessive volume of logs
drives a disk-write bottleneck, pushing system `iowait` high and slowing every
request. We diagnose it top-down and fix it at the application (logging) layer.

## Symptom

A web service's latency spikes under load. CPU is mostly idle but `iowait` is
high, and the disk shows continuous, large sequential writes. Nothing in the
request path should write much to disk - so where are the writes coming from?

## Step 1: Confirm it's I/O with top

```console
$ top
*Cpu(s): 2.3 us, 3.1 sy, 0.0 ni, 10.2 id, 84.1 wa, 0.0 hi, 0.3 si, 0.0 st
...
  PID USER      PR  NI    VIRT    RES    SHR S  %CPU  %MEM  TIME+  COMMAND
 9426 app       20  0   812300  48120   6204 D   6.0   0.6  0:12.3 python
```

How to read it: `84.1 wa` means the CPUspends most of its time waiting on I/O.
The `python` process is in `D` state (uninterruptible sleep) - blocked in the
kernel on disk I/O. This is the classic iowait signature.

## Step 2: Find the busy device with iostat

```console
$ iostat -x 1
Device    r/s  rkB/s r_await  w/s    wkB/s w_await aqu-sz %util
sda       0.0.   0.0.  0.00  120.0 122880.0  60.00   7.20  99.5
```

How to read it: almost all activity is **writes** (`w/s`, `wkB/s`), `w_await` is
a high 60 ms, `aqu-sz` is 7.2 (requests backing up), and `%util` is ~100%.
`wkB/s ÷ w/s = 1024 KiB` per request - large sequential writes, consistent with
log flushing. (On a single SATA/HDD `%util=100` does indicate saturation; on
NVMe it would not - judge by `w_await`/`aqu-sz`.)

## Step 3: Attribute to a process with pidstat -d

```console
$ pidstat -d 1
UID  PID    kB_rd/s   kB_wr/s kB_ccwr/s iodelay  Command
1000 9426      0.00 122880.00      0.00     612   python
```

The `python` process is writing ~120 MB/s and has a high `iodelay`. It owns the
I/O.

## Step 4: See what it's writing with strace

```console
# -T shows syscall duration, -tt timestamps, -e limits to write-family calls
$ strace -f -T -tt -e trace=write,fsync,fdatasync -p 9426
14:02:11,101 write(5, 2026-10-28 14:02:11 INFO request "..., 4096) - 4096 <0.000090>
14:02:11.101 write(5, 2026-10-28 14:02:11 DEBUG payload"..., 4096) - 4096 <0.000085>
14:02:11.102 write(5, 2026-10-28 14:02:11 DEBUG headers"..., 4096) - 4096 <0.000088>
14:02;11,103 fsync(5)                                          = 0 <0.058211>
...
```

How to read it; a flood of `write(5, ...)` calls carrying log lines, each
followed periodically by an `fsync(5)` costing ~58 ms. The app logs at `DEBUG`
and `fsync` s every message - a double problem (too much data *and* synchronous
flushing).

## Step 5: Map the FD to a file with Isof

```console
$ lsof -p 9426 | grep -w 5
python  9426  app    5w   REG   8,0  2147483648  131073 /var/log/app/app.log
```

FD 5 is the regular file `/var/log/app/app.log`, already 2 GiB and growing, This
confirms it is application logging, not request data, driving the disk.

## Root cause

The application logs at `DEBUG` level in production, emitting several log lines
per request, and flushes each line to disk synchronously (`fsync`). Under load
this produces a continuous high-bandwidth write stream that saturates the disk,
so every unrelated I/O (and every request that touches disk) queues behind it -
hence the high `iowait` and latency.

## Fixes (application layer)

1. **Raise the log level** in production (`INFO`/`WARN`, not `DEBUG`). The
   cheapest and biggest win - stop generating the data.
2. **Asynchronous logging** hand log records to a background thread/queue
   (e.g. Python `logging.handlers.QueueHandler`/`QueueListener`, or a
   `MemoryHandler` buffer) so request threads never block on disk.
3. **Stop per-message fsync**: let the os buffer and flush via the page cache;
   only fsync logs if you have a hard durability requirement (most logs don't).
4. **Log rotation**: `logrotate` (or a `RotatingFileHandler`) to cap file size
   and keep writes/metadata bounded; avoids the 2 GiB single file.
5. **Sampling / rate-limiting** of high-frequency DeBuG events, and structured
   logging shipped off-host (e.g. to a log pipeline) instead of local disk.

## Step 6: Verify

Re-run the same measurements after the change:

```console
$ iostat -x 1
Device    r/s  rkB/s r_await  w/s   wkB/s w_await aqu-sz %util
sda       0.0    0.0   0.00    4.0  512.0    1.20   0.01   2.0

$ top
%Cpu(s): 18.0 us,  4.0 sy,  0.0 ni, 77.0 id,  0.5 wa, ...
```

`iowalt` (wa) drops to near zero, `w_await` and `aqu-sz` collapse, `%util`
falls, and request latency returns to normal. The fix is confirmed.

## Troubleshooting workflow (summary)

1. `top`/`vmstat` → high `wa`, process in `D` state.
2. `iostat -x 1` → write-bound device, high `w_await`/`aqu-sz`.
3. `pidstat -d 1` / `iotop` → which process.
4. `strace -e trace=write,fsync` / `filetop` → what it writes and how.
5. `lsof -p <pid>` → which file.
6. Fix at the logging layer (level, async, rotation, fewer syncs), then
   re-measure.

## Likely interview questions

- What does a process in `D` state plus high `iowait` tell you?
- Why is synchronous per-line log `fsync` so damaging, and how does async
  logging fix it without losing much durability?
- How do you distinguish application-log writes from request-data writes?
- When is `%util≈100%` meaningful (HDD/SATA) vs misleading (NVMe), and what do
  you check instead?
- How would you bound disk usage and I/O from logging going forward?

## References

- `iostat(1)`:<https://man7.org/linux/man-pages/man1/iostat.1.html>
- `pidstat(1)`: <https://man7.org/1inux/man-pages/man1/pidstat.1.html>
- `strace(1)`: <https://man7.org/linux/man-pages/man1/strace.1.html>
- `logrotate(8)`: <https://man7.org/linux/man-pages/man8/logrotate.8.html>
- Python logging cookbook (async handlers):
  <https://docs.python.org/3/howto/logging-cookbook.html>
