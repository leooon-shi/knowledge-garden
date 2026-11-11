K-- performance/memory/swap.md -->
# Swap, swappiness, and Compressed Memory (zram/zswap)
What swap is, how the kernel decides to use it, how to tune it, and the modern compressed-memory alternatives. The broader reclaim path (watermarks, kswapd, LRU) is in memory recycle.md.
#* What Swap Is
Swap is disk-backed (or file-backed) space the kernel uses to store **anonymous** pages (heap/stack, not file-backed) that it has reclaimed from RAM. It lets the system keep more anonymous memory "allocated" than physically fits in RAM, at the cost of major page faults (disk I/O) when a swapped page is touched again.
Only anonymous pages swap. Clean file-backed pages don't need swap - they can be dropped and re-read from their file, (See "buffer_cache.md".)
$ swapon --show
NAME
TYPE
SIZE
/swapfile file
4G
$ free -w
USED PRIO
128M
-2
Men:
Swap:
total
used
free shared buffers
32866224••
...
...
..٠
4194300 131072 ...
cache available
" proc/meninfo exposes 'SapTotal", "Swapre", and "Swapached" (pages thard are
both in RAM and already have a swap slot, so they can be dropped cheaply).
#* How the Kernel Decides to Swap: swappiness vm.swappiness'
anonymous pages**:
biases reclaim between **evicting file cache** and **swapping
$ cat /proc/sys/vm/swappiness
60
* Lower it (prefer dropping file cache over swapping anon):
$ sudo sysctl vm.swappiness-10
* (requires root)
-   Range is **g-200** on modern kernels (it was 0-100 historically; the extended range appeared with the addition of memory compression accounting) .
-   Default **60** (verified on this Ubuntu 24.04 / kernel 6.8 host).
-   Lower - prefer reclaiming page cache; higher = more willing to swap anon.
*  swappiness-0° does **not** fully disable swap; it only makes the kernel avoid swapping until it is nearly the only option. To truly disable, use swapoff -a.
It is a *ratio/bias*, not an on/off switch - a common misconception.
## NUMA Reclaim Interaction
On NUMA systems *vm.zone_reclaim_mode" affects whether the kernel reclaims (and potentially swaps) within a node before spilling allocations to a remote node.
Aggressive local reclaim can trigger swapping even when another node has free RAM.
See "memory_recycle.md.
## zram and zswap (Compressed Memory)
Instead of (or in front of) disk swap, the kernel can compress pages in RAM:
- **zram** - a compressed block device in RAM used *as* a swap device. Swapped pages are compressed and kept in memory (no disk). Great for RAM-constrained systems and the default swap on many modern desktop/mobile distros.
$ sudo modprobe zram $ zranctl
NAME
ALGORITHM DISKSIZE DATA COMPR TOTAL STREAMS MOUNTPOINT
/dev/zrame lzo-rle
8G
1G
ЗО0М 316М
•••
[SWAP]
- **zswap** - a compressed *cache in front of* a real swap device, Pages are compressed into a RAM pool first; only when that pool fills are they decompressed and written to the backing swap disk, Reduces swap 1/0 while still having a disk fallback.
$ cat /sys/module/zswap/parameters/enabled scat/sys/kernel/debug/zswap/stored_pages
Rule of thumb: **zram** - RAM-only compressed swap; **zswap** • compressed front-cache for on-disk swap. They are generally used one or the other, not stacked on the same path.
## group v2 Swap Control
Per-group swap is bounded by ** memory•swap•max ** (and "memory.swap.high" for throttling). Setting memory. swap. max=0° disables swap for that group while still allowing the rest of the system to swap. Kubernetes uses this to gate swap for Burstable pods - see memory k8s.md"

٠٠٠
$ cat /sys/fs/cgroup/<slice>/memory.swap.max
max
$ cat/sys/fs/cgroup/<slice>/memory.swap.current
## Troubleshooting Workflow
1. vmstat 1 - si (swap-in) and so" (swap-out) columns. Sustained non-zero, especially si, means the system is actively faulting pages back from swap + latency pain.
1.    "free -w* - how much swap is used; but used swap alone is not bad, *active swapping* is.
2.    "sar -W 1 - "pswpin/s / "pswpout/s over time.
3.    Per-process: •cat /proc/<pid›/status | grep VmSwap, or smem -s swap -r° to rank processes by swap usage.
4.    If swapping hurts: add RAM, lower "swappiness", fix the leak | ("memory_leak.md®), or consider zram/zswap.
## Common Pitfalls
-   **"Used swap" * problem. ** The kernel may have swapped idle pages long ago; | what matters is ongoing si/so.
-   ***swappiness-0* does not disable swap.**
-   **Disabling swap on a tight box** converts graceful slowdown into hard oOM kills (anon pages become unreclaimable - see "memory_recycle.md®).
-   **Stacking zram and zswap** on the same device is misconfiguration.
-   Swap on slow spinning disk can make a box appear hung under pressure.
## Likely Interview Questions
-   Which pages can be swapped, and which cannot?
-   Does "swappiness-0" disable swap? What does swappiness really control?
-   zram vs. zswap - explain the difference.
-   Is non-zero used swap a problem? what metric tells you swapping hurts?
-   How do you disable swap for a single group/container?
-   Mhy can disabling swap make 0ON *more* 1ikely？
## References
-  kernel ®Documentation/admin-guide/sysctl/vm.rst* - swappiness.
-  kernel Documentation/admin-guide/blockdev/zram.rst and mm/zswap.rst.
-   kernel ®Documentation/admin-guide/cgroup-v2,rst* - "memory.swap-max".
-   "man swapon, "man vmstat,
"man sar°,

