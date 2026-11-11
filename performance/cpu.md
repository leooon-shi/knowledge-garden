<!-- performance/cpu.md-->

# CPU Performance Analysis and Optimization

A field guide to understanding, measuring, and troubleshooting CU usage on Linux.
Covers CPU usage accounting, context switches, run-queue states, and the core
tooling (`top`, `ps`, `pidstat`, `vmstat`, `perf`).

## What CPU Usage Actually Measures

Linux is a multitasking 05. Each CPU's time is sliced into very short intervals,
and the scheduler hands those slices to runnable tasks in turn, creating the 
illusion that many tasks run simultaneously.

To account for CPU time, the kernel uses a fixed tick rate called `HZ`
(`CONFIG_HZ`, commonly 100, 250, or 1000). For example `CONFIG_HZ-250` means the
timer interrupt fires 250 times per second, so one tick is 1/250 s. the value
exported to user space through the stat interfaces is normalized to `USER_HZ`,
which is almost always 100 on Linux, This is why `/proc/<pid>/stat` flelds are in
units of 1/100 s regardless of the kernel's internal `HZ`.

CPU usage is "everything except idle time" as a fraction of total CPu time over
an interval:

```
CPU usage = 1 - idle_time / total_cpu_time
```

Because every tool reports an average over an interval, the interval length
matters: a 1 s sample and a 60s sample of the same workload can look very
different. Always know (and state) your sampling window.

### How the kernel exposes it: /proc/stat

`proc/stat` is the system-wide source of CPU accounting. Per-process accounting
lives in `/proc/<pid>/stat`.

```
$ cat /proc/stat | grep ^cpu
#      user      nice    system    idle          iowait  irq softirg    steal  guest  guest_nice
cpu    38129891  165291  28388631  11356375028   333652    0  516516    0      0       0   # sum of all CPUs
cpu0   3850132   19605   2820035   1421131761     37595    0  286672    0      0       0
cpu1   505233    18461   3730871   1418671150     44871    0  136284    0      0       0
...
```

Each column is cumulative CPU time (in USER_HZ ticks) since boot:

- **user (us)** – time in user mode. Excludes `nice` below but *includes* `guest`.
- **nice (ni)** – user-mode time for processes with a positive nice value.
  Nice ranges from -20 (highest priority) to 19 (lowest); larger = lower priority.
- **system (sy)** – kernel-mode time.
- **idle (id)** – idle time. Does *not* include `iowait`.
- **iowait (wa)** – time idle *while an I/O was outstanding. See pitfalls below.
- **irq (hi)** – time servicing hardware interrupts.
- **softirg (si)** – time servicing soft interrupts.
- **steal (st)** – time this VM was ready to run but the hypervisor ran something
  else (noisy-neighbor / overcommitted host).
- **guest** – time spent running a guest vM (already counted inside `user`).
- **guest_nice (gnice)** – time running a low-priority (niced) guest VM.

## top vs. ps

`top` shows system-wide CPU and memory plus per-process usage; `ps` shows
a one-shot snapshot of per-process usage.

`top` notes:
1. Refreshes every 3 s by default.
2. The headline CPU line is the average across all CPUs. Press `1` to expand to
   per-CPU lines,
3. Each process's `%CPU` is the sum of its user-space and kernel-space time:
   - user-space CPU,
   - kernel-space CPU consumed via system calls,
   - (plus scheduling detalls); to split user vs. kernel per process, use `pidstat`. 

`ps` is better for scripting and stable snapshots; `top` is better for 1ive
observation and sorting.

## pidstat: per-process CPU breakdown

`pidstat` is the go-to tool for splitting a single process's CPU time into its
components.

```
# One sample per second, 5 samples total
$ pidstat 1 5
15:56:62   UID     PID  %user %system %guest  %wait   %CPU CPU Command
15:56:03     0   15006   0.00    0.99   0.00   0.00   0.99   1  dockerd
...
Average:   UID    PID   %usr %system %guest  %walt   %CPU CPU Command
Average:     0  15906   0.00    0.99   0.00   0.00   0.99   -  dockerd
```

Columns:
- **%usr** – user-mode CPU usage.
- **%system** – kernel-mode CPU usage.
- **%guest** – CPU spent running a guest VM.
- **%wait** – time the task spent *waiting in the run queue* (ready but not
  scheduled). This is run-queue latency, not I/O wait – a high `%wait` means CPU
  contention. (Note: older docs sometimes mislabel this as "wait for CPU"; it is
  specifically runqueue wait, reported when `pidstat` is built against a kernel
  exposing `delayacct`.)
- **%CPU** - total CPU usage of the task.

Once a hot process is identified, dri11 into it at function granularity with
`perf top -g -p <pid>`.

## Context Switches

Two scopes:
- `vmstat` – system-wide context switches.
- `pidstat -w` – per-process context switches.

```
# One sample every 5 s
$ pidstat -w 5
Linux 4.15.0 (ubuntu) 09/23/18 _x86_64_ (2 CPU)

08:18:26 UID PID cswch/s nvcswch/s Command
08:18:31  0   1   0.20    0.00    systemd
08:18:31  0   8   5.40    0.00   rcu_sched
...
```

- **cswch/s** – voluntary context switches per second. A *voluntary* switch
  happens when a task blocks because it cannot get a resource it needs (e.g.
  waiting on I/O, a lock, or memory). High `cswch/s` points to resource waits.
- **ncswch/s** – non-voluntary context switches per second. A *non-voluntary*
  switch happens when the scheduler preempts a task (its time slice expired, or a
  higher-priority task became runnable). High `nvcswch/s` points to CPU
  contention – too many runnable tasks fighting for too few CPUS.

## Process States (the `S` column in top/ps)

- **R** – Running or Runnable: on a CPU run queue, running or ready to run.
- **D** – Uninterruptible Sleep (Disk Sleep): blocked in the kernel, typically on
  hardware I/O, and cannot be interrupted by signals. Raises load average.
- **Z** – Zombie: the process finished but its parent has not reaped it (via
  `wait()` / `waitpid()`), so its PID and exit status still linger.
- **S** – Interruptible Sleep: waiting on an event; wakes to R when it fires.
- **T** – Idle: used for kernel threads that are sleeping with no load. Unlike
  `D`, the `I` state does *not* raise load average. (This distinction is why a box
  can have many sleeping kernel threads without inflated load.)
- **T / t** – Stopped / Traced: paused by `SIGSTOP` (resume with `SIGCONT` / `fg`)
  or under a debugger/tracer.
- **X** – Dead: a transient teardown state you will not normally see in `top`/`ps`.

### Process groups and sessions

```
$ ps aux 1 grep /app
root 4009 0.0 0.0  4376  1008 pts/0 Ss+ 05:51 0:00 /app
root 4287 0.6 6.4 37280 33660 pts/0 D+  05:54 0:00 /app
root 4288 0.6 0.4 37280 33668 pts/0 D+  05:54 0:00 /app
```

In the `STAT` column: `s` marks a **session leader**, and `+` marks a process in
the **foreground process group**.

- A **process group** is a set of related processes (e.g. a parent and its
  children).
- A **session** is one or more process groups that share a controlling terminal
  (`tty`). Commands you launch in a terminal, plus their children, form process
  groups; backgrounded commands form the background group, foreground commands the
  foreground group.

## Zombie and Uninterruptible (D-state) Processes

When `iowait` rises, processes often sit in uninterruptible (`D`) state waiting
for hardware to respond, The D* state exists to keep process data consistent
with hardware state; brief "D' states are normal and can be ignored.

**Zombies** are common in multi-process apps. Normally a parent calls `walt()` /
`waitpid()` to reap a child, or registers a `SIGCHLD` handler to reap
asynchronously. If the parent never reaps (or the child exits before the parent
is ready), the child becomes a zombie, Zombies usually vanish quackey once
reaped, or are adopted and reaped by `init`/`systemd` when the parent exits, But a
long-lived buggy parent that never reaps w111 accumulate zombies - and because
zombies hold PIDs, enough of them can exhaust the PID space and block new process
creation.

```
# Press "1" to show per-CPU usage; observe, then Ctrl+C
$ top
top - 05:56:23 up 17 days, 16:45, 2 users, load average: 2.00, 1.68, 1.39
Tasks: 247 total, 1 running, 79 sleeping, e stopped, 115 zomble
%Cpu0 : 0.0 us, 0.7 sy, 0.0 ni, 38.9 id, 60.5 wa, 0.0 hi, 0.0 si, 0.0 st
%Cpu1 : 0.0 us, 0.7 sy, 0.0 ni,  4.7 id, 94.6 wa, 0.0 hi, 0.0 si, 0.0 st
...
PID  USER PR    NI  VIRT   RES  SHR S %CPU %MEM   TIME+ COMMAND
4340 root 20    0  44676  4048 3432 R  0.3  0.0 0:00.05  top
4345 root 20    0  37280 33624  860 D  0.3  0.0 0:00.01  app
4344 root 20    0  37280 33624  860 D. 0.3  0.4 0:00.01  app
   1 root 20    0 160072  9416 6752 S  0.0. 0.1 0:38.59  systemd
```

How to read this:
1. **Load average** 2.B0 / 1.68 / 1.39 is rising (1 min > 5 min > 15 min) and the
   1 min value already equals the CPU count - a likely bottleneck.
2. **Tasks** shows many zombies, still climbing - children are exiting without
   being reaped.
3. **user/system CPU** are both low.
4. **iowait** is 60.5% and 94.6% - abnormally high.
5. Top `%CPU` process is only 0.3% - so the pressure is not CPU-bound; two `app`
   processes are in `D` state (1ikely I/O), but `top` alone cannot prove they
   cause the iowait.

## I/0 Wait Investigation WorkFlow

Confirm whether high `iowait` is truly disk I/0, then find the culprit.

```
# System-wide: one sample per second, 10 samples
$ dstat 1 10
--total-cpu-usage-- -dsk/total- -net/total- ---paging-- ---system--
usr sys idl wai stl|read   writ|  rec send| in    out | int   csw
0     0  96   4   0|1219k 408k |  0     0 |  0     0  |  42   885
0     2  96  98   0|  34M    0 |198B 790B |  0     0  |  42   138
0     0  96 100   0|  34M    0 | 66B 342B |  0     0  |  42   135
...
```

Every time `wai` (iowait) rises, disk `read` is large → the lowait tracks disk
reads. Next, use `top` to find the `D` -state process, then confirm with
`pidstat`:

```
# -d shows I/O stats, -p selects a PID; 1 s interval, 3 samples
$ pidstat -d -p 4344 1 3
06:38:50 UID   PID  KB_rd/s kB_wr/s KB_ccwr/s iodelay Command
06:38:51   0  4344.    0.0      0.0       0.0       0   app
...
```

- **kB_rd/s** – KB read per second.
- **kB_wr/s** – KB written per second.
- **kB_ccwr/s** – KB of writes cancelled per second.
- **iodelay** – block I/0 delay in clock ticks.

```
* All processes, 1s interval
$ pidstat -d 1 28
06:48:47 UID  PID  KB_rd/s  KB_wr/s KB_ccwr/s  iodelay Command
06:48:47 0   6080 32768.00     0.00     0.00       170 app
06:48:47 0   6881 32768.00     0.00     0.00       184 app
...
```

The `app` processes read ~32 MB/s each – they are the source.

Trace their syscalls with `strace`:

```
$ strace -p 6082
strace: attach: ptrace(PTRACE_SEIZE, 6082): Operation not permitted
# (fails if the target has already become a zombie, or you lack CAP_SYS PTRACE
```

If `strace` fails because the target is a zombie, fall back to `perf record -g` /
`perf report`, and walk the parent chain with `pstree`:

```
# -a show command-line args, -p show PIDS, -s show the ancestry of this PID
$ pstree -aps 3084
systemd, 1
  |_dockerd,15006 -H fd://
      |_ docker-containe,15024 --conflg /var/rum/docker/containerd/containerd.toml
           |_docker-containe, 3991 -namespace moby -workdir...
               |_app, 4009
                  |_(app, 3084)
```

A revealing syscall:

```
open(disk, O_READONLY | O_DIRECT | IO_LARGEFILE, 0755)
```

`O_DIRECT` means the process bypasses the page cache, so every read hits the disk
directly - which explains the high iowait.

## perf: finding hot functions

Add `-g` to `perf top` and `perf record` to capture call graphs so you can follow
the call chain.

```
# -g enable call-graph, -p target PID
$ perf top -g -p 21515
$ perf top
Samples: 833 of event 'cpu-elock`', Event count (approx.): 97742399
Overhead Shared Object Symbol
   7.28% perf          [.] 6x00000000001f78a4
   4.72% [kernel]      [k] vsnprintf
   4.32% [kernel]      [k] module get_kallsym
   3.65% [kernel]      [k] _raw_spin_unlock_irgrestore
...
```

The header line shows the sample count (`Samples`), the event type (`event`), and
the total event count. `[.]` marks user-space symbols, `[k]` kernel symbols.

For offline analysis use `perf record -g` then `perf report`. Once you have a hot
function, you can inspect it further(e.g. with `gid`, source review, or
`perf annotate`).

## Load Testing

`ab` (ApacheBench) is a simple HTTP load generator, useful for driving a web
server such as Nginx while you observe it.

```
# 10 concurrent requests, 100 requests total
$ ab -c 10-n 100 http://292.168.0.10:10000/
This is ApacheBench, Version 2.3 <$Revision: 1706008 $>
...
Requests per second:  11.63 [#/sec] (mean)
Time per request:    859.942 [ms] (mean)
```

## Summary: Reading CPU Usage

CPU usage is the most direct and commonly used system performance signal. The key
components are user (`%user`), nice (`%nice`), system (`%system`), iowait
(`%iowait`), hardware interrupts (`%irq`), and soft interrupts (`%softirq`):

1. **High user + nice** → user-space processes dominate; profile the application.
2. **High system** → kernel-mode dominates; investigate syscalls and kernel
   threads.
3. **High iowait** → long waits on I/O; investigate the storage path.
4. **High irq/softirg** → interrupt handlers dominate; investigate drivers and the
   kernel's interrupt service routines.

## Troubleshooting Workflow (USE-style)

1. `uptime` / `top` – check load average and per-CPU split.
2. Classify the pressure: user, system, iowait, or irq/softirq?
3. If **user/system**: `pidstat 1` to find the process, `perf top -g -p <pid>` for
   the hot function.
4. If **iowait**: `dstat`/`vmstat` to confirm disk activity, `pidstat -d` to find
   the process, `strace` / `perf` to find the syscall.
5. If **context-switch bound**: `vmstat 1` (watch `cs`), `pidstat -w` to attribute
   voluntary vs. non-voluntary switches.
6. If **irq/softirg**: `cat /proc/softirqs`, `mpstat -P ALL 1`, check NIC/IRQ
   affinity.

## Common Pitfalls

- **iowait * disk bottleneck.** If the only runnable work is I/O-bound, iowait can
  be high even though the disk is nowhere near saturated. Always confirm actual
  disk throughput/latency (`iostat -x`, `dstat`).
- ***`%CPU > 100%` is normal** for multithreaded processes on multi-core systems
  (it is summed across cores).
- **Steal time** on VMs indicates host overcommit, not a guest problem.
- **Averages hide spikes,** A 1s and 60 s window can tell opposite stories.
- **`pidstat %wait` is run-queue latency**, not I/O wait - do not confuse with
  the `wa` column in `top`.

## Likely Interview Questions

- What is the difference between `%iowait` and a process in `D` state?
- Does high `iowait` always mean the disk is the bottleneck? why not?
- Explain voluntary vs. non-voluntary context swtiches and waht each indicates.
- Waht are `HZ` and `USER_HZ`, and why are `/proc/<pid>/stat` times in 1/100 s?
- How is a zombie created and how can it harm the system?
- Walk through diagnosing a sudden CPU spike from `top` down to a function.
- What does `steal` time mean on a cloud VM?
- How does `top`'s per-process `%CPU` relate to `pidstat`'s `%usr`/`%system`?

## References

- Linux `proc(5)` man page – `/proc/stat`, `/proc/<pid>/stat`.
- `man pidstat`, `man vmstat`，`man top`, `man perf`.
- Brendan Gregg, *Systems Performance*（USE method, `perf`）.
