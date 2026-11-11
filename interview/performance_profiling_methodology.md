<!-- interview/performance_profiling_methodology.md -->

# Performance Profiling Methodology: perf, Flame Graphs, Off-CPU Analysis, eBPF

The how-to-investigate doc that ties together the diagnostic commands scattered across
`std_mutex_real_cost.md`, `cpp_multithreading_throughput.md`, `close_wait_surge.md`,
`zero_copy_smal1_packets.md`, and `numa_hugepages_memory_locality.md`. Note: the study
host has `perf_event_paranoid=4` (Ubuntu's extra-restrictive setting), so perf needs
`sudo` or `sysctl kernel.perf_event kernel.perf_event_paranoid=1` there.

## 1. Methodology first

- **Define the problem precisely:** which metric (p99 latency, throughput, CPU per
  request), how much, since when, under what load. "It's slow" is not a problem statement.
- **USE method** (Brendan Gregg) for resources - for each of CPU, memory, disk, network,
  locks: **U**tilization, **S**aturation (queueing), **E**rrors.
- **RED method** for services-**R**ate, **E**rrors, **D**uration per endpoint.
- **On-CPU vs off-CPU:** wall-clock time = time running + time walting (locks, I/O,
  scheduler, page faults). A CPU profile only explains the first part. if latency is high
  and CPU is low, profile off-CPU.
- **Top-down microarchitecture analysis (TMA):** when on-CPU time dominates, classify
  pipeline slots as Retiring / Bad speculation / Frontend bound / Backend bound (memory vs
  core before optimizing.
- **Change one thing, measure, compare distributions** (not means; use `hyperfine`, many
  runs, CPU frequency pinned, noise controlled - `taskset`, disabled turbo, isolated
  cores).

## 2. First 60 seconds on a Linux host

```
uptime                     # load averages (run queue + uninterruptible D state)
dmesg -T | tail            # OOM kills, NIC errors, hung tasks
vastat 1                   # r (run queue), si/so (swapping), us/sy/wa/st
mpstat -P ALL 1            # per-CPU imbalance, softirq (%soft), steal
pidstat 1 / pidstat -t 1   # per process / thread CPU
iostat -xz 1               # disk util, await, queue size
free -m ; sar -n DEV 1     # memory; network throughput
sar -n TCP,ETCP 1          # TCP active/passive opens, retransmits
top / htop                 # quick overview
```

Plus `ss -s`, `nstat -az` (TCP counters), `/proc/pressure/{cpu,memory,io}` (**PSI** -
direct measure of stall time, used by systemd-oomd and Kubernetes).

## 3. perf

- **Counters:**
  `perf stat -e cycles,instructions,cache-misses,branch-misses,dTLB-load-misses -p <pid> -- sleep 10`.
  **IPC** (instructions/cycle) < ~1 on a modern core usually means memory stalls; > 2-3 is
  compute bound. `perf stat -topdown` (or `-M TopdownL1`) for TMA levels.
- **Sampling profile:** `perf record -F 99 -g -p <pid> -- sleep 30` then
  `perf report --no-children` / `perf report --children`. 99 Hz avoids lockstep with
  timers.
- **Call stacks need unwinding info:** frame pointers (`-fno-omit-frame-pointer`,
  `-mno-omit-leaf-frame-pointer` - now default in Fedora 38+/Ubuntu 24.04 packages), or
  `--call-graph dwarf` (copies stack chunks; heavy, large files), or `--call-graph lbr`
  (Intel, short stacks). Missing frames = broken flame graphs.
- **Symbols:** JITs need perf maps (`/tmp/perf-<pid>.map`: Java
  ` -XX:+PreserveFramePointer` + perf-map-agent, Node `--perf-basic-prof`, Python 3.12
  `-x perf`). Stripped binaries need debuginfo/debuginfod.
- **Other modes:** `perf top` (live), `perf trace` (strace-like, far lower overhead),
  `perf sched latency` (scheduler delays), `perf lock contention` (kernel and futex lock
  contention, BPF-based on newer kernels), `perf c2c` (cache-line contention/false
  sharing), `perf mem` (load latency sampling), `perf annotate` (hot instructions).
- **Containers:** perf needs `CAP_PERFMON`/`CAP_SYS_ADMIN`; symbol resolution needs access
  to the container's filesystem (`--symfs`, or run perf in the container's mount
  namespace).

## 4. Flame graphs

- **CPU flame graph;** x-axis = proportion of samples (sorted alphabetically, **not
  time**), y-axis = stack depth, width - time on CPU including children. Look for **wide
  plateaus** at the top (functions burning CPU themselves) and wide towers you didn't
  expect.
  ```
  perf record -F 99 -a -g -- sleep 36
  perf script | stackcollapse-perf.pl | flamegraph.pl > cpu.svg     # Brendan Gregg's FlameGraph scripts
  ```
  Alternatives: `perf script report flamegraph`, Firefox Profiler / speedscope (load
  `perf script` output), `inferno`(Rust), prof (`go tool pprof -http`),
- **Icicle graph** - inverted; **differential flame graph** - red/blue for before/after
  (regression hunting),
- **Off-CPU flame graph:** stacks where threads *blocked*, weighted by blocked time:
  ```
  offcputime-bpfcc -df -p <pid> 30 > out.stacks   # bcc tool
  flamegraph.pl --color=io --countname=us < out.stacks > offcpu.svg
  ```
  Shows lock waits (`futex_walt`), I/O (`io_schedule`), sleeps, page faults. **Wakeup**
  and **off-wake** graphs add who woke the thread.
- **Continuous profiling:** Parca, Pyroscope/Grafana, Google-Wide Profiling-style fleet
  profilers, and eBPF-based agents (OpenTelemetry ePF profiler) - always-on low-rate
  sampling to compare versions and find fleet-wide costs.

## 5. eBPF tooling

eBPF runs verified programs in the kernel at hook points (probes, uprobes, tracepoints,
USDT, perf events, sockets, DP, cgroups) and aggregates in-kernel (maps), so tracing
high-frequency events is cheap.

- **bcc tools** (`*-bpfcc` on Ubuntu): `execsnoop` (short-lived processes), `opensnoop`,
  `biolatency`/`biosnoop` (disk), `teplife`, `tepretrans`, `tepconnect`/`tepaccept`,
  `runqlat` (scheduler run-queue latency), `offeputime`, `profile`, `funccount`,
  `funclatency`, `trace`, `cachestat`, `memleak`.
- **bpftrace one-liners:**
  ```
  bpftrace -e 'tracepoint:syscalls:sys_enter_openat { @[comm] = count(); }'
  bpftrace -e 'kprobe:tcp_sendmsg { @bytes = hist(args); }'                       # send size distribution
  bpftrace -e 'tracepoint:syscalls:sys_enter_futex /comm == "myapp"/ { @start[tid] = nsecs; }
               tracepoint:syscalls:sys_exit_futex /@start[tid]/ { @us = hist((nsecs - @start[tid]) / 1000); delete(@start[tid]); }'
  bpftrace -e 'uprobe:/usr/lib/x86_64-linux-gnu/11bc.so.6:malloc { @[ustack(5)] = count(); }'
  bpftrace -e 'profile:hz:99 /pid == 1234/ { @[ustack] = count(); }'
  ```
- **USDT probes:** static tracepoints compiled into user code (`DTRACE_PROBE` from
  `sys/sdt.h`; also in PostgresQL, MySQL, JVM, Python, Node). Near-zero cost when
  disabled.
- **Overhead awareness:** uprobes on hot functions (malloc) cost ~1 us per hit - measure
  overhead; kprobes/tracepoints are cheaper; prefer tracepoints (stable) over kprobes
  (kernel-version specific).
- **production eBPF beyond tracing:** Cilium (networking/policy), Katran (L4 LB, XDP),
  Falco/Tetragon (security), sched_ext (BPF CPU schedulers, kernel 6.12).

## 6. Other tools by question

| Question | Tool |
| ------------------------------------ | ------------------------------------------------------------------------------------------------------------------------ |
| Which syscalls, how often, how long? | `strace -c -f -p` (high overhead - ptrace), `perf trace -s` (low)                                                        |
| Who is allocating, leaking?          | heaptrack, `bpftrace`/`memleak-bpfcc`, jemalloc/tcmalloc heap profiles (`MALLOC_CONF=prof:true`, pprof), Valgrind massif |
| Lock contention (userspace)          | `perf lock contention -b`, mutrace, off-CPU graph on `-futex`                                                            |
| Cache-line contention                | `perf c2c record/report`                                                                                                 |
| Microarchitecture deep dive          | Intel VTune, AMD uProf, `toplev` (pmu-tools), `llvm-mca` for a hot loop                                                  |
| Instrumented timelines               | Tracy, Perfetto, `-finstrument-functions`, `uftrace`                                                                     |
| Benchmarks                           | Google Benchmark (`DoNotoptimize`, `ClobberMemory`), `hyperfine` for conmands                                            |

## Likely interview questions

- *"p99 Latency doubled after a deploy; CPU is flat. How do you investigate?"* - Off-CPU
  analysis (locks, I/O, run-queue latency `runqlat`), PSI, compare flame graphs between
  versions.
- *"How do you read a flame graph?"* - Width = samples, x is not time, look for wide tops.
- *"Why are my perf stacks broken?"* - No frame pointers; use FP bullds or DWARF/LBR
  unwinding.
- *"IPC is 0.4 - what does it suggest?"* - Memory-bound / stalls; check cache and TLB
   misses, top-down.
- *"How is eBPF tracing safer/cheaper than strace?"* - Verifled in-kerned programs,
  in-kernel aggregation, no ptrace stops.
- *"What's the USE method?"*

## Synthesis

Profile with a method, not a tool; state the metric, apply **USE** to resources and
**RED** to services, and split time into **on-CPU** (perf sampling, CPU flame graphs, IPC
and top-down counters) and **off-CPU** (offcputime flame graphs, `runqlat`, PSI, lock and
I/O tracing). `perf` covers counters, sampling, scheduler, lock and cache-line contention,
but it needs frame pointers or DWARF/LBR unwinding and symbols to produce usable stacks.
**eBPF** (bcc, bpftrace, UST) makes high-frequency kernel and user events observable in
production with in-kernel aggregation and low overhead. Compare distributions before and
after each change, and use continuous profiling to catch regressions across a fleet.
