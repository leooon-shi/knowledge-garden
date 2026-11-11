# CPU - Interview Question Answers
Answers to the "Likely Interview Questions" in "cpu.md".
## 1. Difference between •%iowait" and a process in 'D' state
They describe the same underlying situation from two different vantage points, and neither implies the other.
- ***%iowait ** is a *CPU-time* accounting bucket: the fraction of time a CPU was
**idle** *while at Least one I/O was outstanding* on that CPU's run queue. It is idle time - the CPU had nothing runnable to do and was waiting for I/o to complete. It is a per-CU statistic in /proc/stat*.
- **"D' state** (uninterruptible sleep) is a *per-process* state: a task is blocked inside the kernel on something that cannot be interrupted by a signal, almost always hardware I/O. It raises the load average.
A box can show high iowait with processes in D' (the usual I/0-bound case), but you can also have "D'-state processes with *Low* iowait if the CPUs are busy running other runnable work (iowait only counts *idle* time). Conversely, iowait can be high with few D' processes if the only runnable work happens to be I/0-bound.
a 2. Does high "iowait" always mean the disk is the bottleneck? Why not?
No. "iowait"
is only "idle time with an outstanding I/O." If the only runnable
work on the box is I/0-bound, the CPU has nothing else to do and Lowait rises - even though the disk may be nowhere near saturated. High iowait means "CUs are idle waiting on I/O," not "the storage device is saturated. " Always confirm the device is actually busy with iostat -x* (look at r_await/w_await, and achieved IOPS/throughput vs the device spec),
•dstat, and •pidstat -d. On
NVMe/RAID especially, Judge saturation by latency and queue depth, not by iowait or "%util'.
#* 3. Voluntary vs. non-voluntary context switches, and what each indicates
-   **Voluntary (*cswch/s' in 'pidstat -w")** - the task gave up the CPU itself because it blocked on a resource it could not get: I/O, a lock/mutex, a condition variable, or memory. High voluntary switches point to **resource waits** (the app is frequently blocking).
-   **Non-voluntary (nvcswch/s*) ** - the scheduler preempted the task against its will: its time slice expired or a higher-priority task became runnable. High non-voluntary switches point to **CPU contention** - more runnable tasks than CPUs, fighting over the run queue.
System-wide context switches are visible with vmstat" ( cs" column); per-process with pidstat -w.
** 4. What are "HZ" and "USER_HZ", and why are /proc/<pid›/stat times in 1/100 s?
-   ***HZ*** (CONFIG_HZ*) is the kernel's internal timer-tick rate - how often the timer interrupt fires. Common values are 100, 250, or 1000. With "CONFIG_HZ-250* one tick is 1/250 s. This is a compile-time kernel property.
-   ***USER HZ*** is the unit the kernel *exports* CPU times in through the /proc stat interfaces, and it is almost always **100** on Linux (so times are in 1/108 s). The kernel normalizes its internal "HZ" accounting to USER HZ' before presenting it to user space.
That decoupling is why /proc/<pid>/stat (and /proc/stat) report times in 1/100 s regardless of the kernel's actual internal •HZ". Code should read the value via
*sysconf(SC_CLK_TCK) rather than hard-coding 100.
## 5. How is a zombie created, and how can it harm the system?
A **zombie** is a process that has exited but whose parent has not yet *reaped* it with
*wait) / waitpid() (or a SIGCHLD" handler). The kernel keeps a minimal entry around holding the child's PID and exit status so the parent can collect it.
Harm: zombies hold onto PIDs. Normally they are reaped almost immediately, or
"init /'systemd" adopts and reaps them when the parent dies. But a long-lived buggy parent that never reaps its children will **accumulate zombies**, and enough of them can **exhaust the PID space**, blocking new process creation system-wide.
Zombies consume no CPU or real memory beyond the process table entry - the danger is PID exhaustion, not resource usage.
## 6. Walk through diagnosing a sudden CPU spike from 'top" down to a function
1. •uptime"
*top" - check the load average trend (1/5/15 min) and whether the
1.   min value approaches or exceeds the CPU count. Press '1" for the per-CPu split.
2.    **Classify the pressure*» from the CPU line; is it us/ ni (user), 'sy" (kernel/system),
*wa" (iowait), or
hi/ si (1ra/softirg)?
3. If **user/system bound**:
•pidstat 1 to find the offending process and split
its " %usr
vs %system,
4. Drill into the hot process at *function granularity** with perf top -g-p ‹pid› (live) or "perf record -g" + "perf report (offline).
*[.]* symbols are user space,
*[k] are kernel,
5. Once you have a hot function, inspect it further (perf annotate, source review, gdb*).
For the other classes: iowait → •dstat*/ vmstat" + pidstat -d* + "strace /perf: context-switch bound → vmstat" ( cs) + pidstat -w*; irq/softirq →
•/proc/softirqs,
"mpstat -P ALL 1°, check NIC/IRQ affinity.

#* 7. What does "steal time mean on a cloud v?
**Steal (st)** is the time the VM (guest) was *ready to run* but the hypervisor scheduled something else on the physical CPU instead - i.e. the guest wanted the
CPU but did not get it. It indicates **host CPU overcommit / noisy neighbors** not a problem inside the guest. Sustained high steal means the physical host is oversubscribed; the fix is on the host/placement side (fewer co-tenants, a dedicated/larger instance), not in the guest workload.
#* 8. How does "top"'s per-process "%PU' relate to "pidstat"s "%usr / %system'?
"top"s per-process "%CPU" is the **total** CPU the task consumed over the interval - the sum of its user-space and kernel-space time (and it can exceed 100% for a multithreaded process on multiple cores, since it sums across cores).
•pidstat **breaks that total down**: •%usr" is the user-mode portion, %system" is the kernel-mode (syscall) portion, "%guest" is time running a guest VM, and
* %CPU* is their sum - which lines up with "top's "%CPU. Use "top" to spot the hot process, then "pidstat" to see whether the time is in user code or in the kernel (which tells you whether to profile the app or investigate syscalls/kernel threads).
## Bonus distinctions worth stating
-   ***%CPU > 100% is normal** for multithreaded processes on multi-core systems - it is summed across cores.
-   **pidstat* %wait* is run-queue latency»* (ready but not scheduled → CPU contention), **not** I/O wait - do not confuse it with •top's "wa column.
-  ***I (idle) kernel-thread state does not raise load average**, unlike D'.

