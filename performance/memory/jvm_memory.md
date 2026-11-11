k!-- performance/memory/jvm_memory.md -->
# JVM Memory
ow the JVM lays out and manages memory, why a container's RSS exceeds the Jav eap, and how to measure and bound each region. Container limits and Qos ar covered in "memory_k8s.md".
## The JVM Memory Model
promon mistake 15 to eute ad (ea heap se to +2 914 men tv.
• common mistake is to equat only one component.
### Heap
The garbage-collected region holding Java objects.
- -Xms" sets the initial heap,
• -Xmx" the maximum.
-   Modern collectors (G1- default since JDK 9; ZGC and Shenandoah for 1ow-pause) divide the heap into regions/generations. The classic generational model is young generation (Eden+ two Survivor spaces) and old (tenured) generation.
-   Objects are allocated in Eden; minor GCS copy survivors between Survivor spaces and eventually promote long-lived objects to old gen; major/ful1 GCs collect old
#*# Off-heap (native) memory
Everything outside the Java heap that still counts toward process RSS:
-   *Metaspace** - class metadata. Replaced the old Permen in JDK 8. Grows in native memory; bound with •-XX:MaxMetaspaceSize. Classloader leaks (common in app servers that redeploy) show up here.
-   **Thread stacks** - one native stack per thread, default ~512 KB-1 MB (-ss*). Thousands of threads add up fast.
-   **Direct byte buffers** - "ByteBuffer.allocateDirect" / NIO; bounded by
-  XX:MaxDirectMemorySize. Netty and NIO servers lean heavily on these.
-   **Code cache** - JIT-compiled native code (*-XX:ReservedCodeCacheSize®).
-   **GC structures, symbol tables, JVM internal** - remembered sets, card tables, etc.
-   **Native allocations by libraries** (e.g. JNI, zlib, native crypto).
## Native Memory Tracking (NMT)
NMT is the built-in tool to account for *all* JVM memory, not just the heap.
* Enable at launch
-XX:NativeMemoryTracking-summary
* or detail
# Query a running JVM
$ jand <pid› VM.native_memory summary
Total: reserved=1842MB, conmitted=423MB
Java Heap (reserved-1024MB, committed-256MB)
Class (reserved=... Metaspace ...)
Thread (reserved=... stacks ...)
Code (reserved-240MB, conmitted-10MB)
GC (reserved=... )
base ne then es yeary. to find what 15 grows .native, memory
baseline then ",
#* Container Awareness
Before DK 8u191/JDK 10, the JVM read the "host's* total memory and CPU, ignoring the group limit - so in a container it would size the heap and GC threads for the whole node and get OOM-killed,
Modern VMs (JDK 10*, backported to 8u191+) are **container-aware**;
- *-XX:UseContainerSupport (on by default) makes the JVM read the cgroup memory
- or the confeiner 1 pretered over a faggo i in a For el
size the heap as a fraction
•-XX:MaxRAMPercentage-75.0°,
- The JuM also respects the group PU quota when sizing Ge/ ForkJoinPool" threads, though pinning with •-XX:ActiveProcessorCount" is sometimes needed. hi ofeap regions, or the kerne Do k11a the container even theret the sava
heap is under •-Xmx. (See "memory_kBs.md" for 0oMk11led vs. working set.)
## Too1s
-   ***jmd <pid› GC.heap_info ** - 11ve heap breakdown.
-   ***jand <pid› VM.native_memory ** - NMT (above).
-   **jstat -gcutil <pid› 1s°** - live GC activity and generation occupancy.
-   ** jmap -histo® / heap dump + Eclipse MAT** - object-level leak analysis.
-   **C logs** - -Xlog:gc* (JDK 9+) / -XX:+PrintGDetails (JDK 8).
-   **async-profiler /JFR** - allocation profiling with low overhead.

#* Troubleshooting Workflow
1.    Container OOMKilled but heap looks fine? → off-heap growth. Enable NMT.
2.    "OutofNemoryError: Java heap space* → heap leak; take a heap dump, analyze inMAT, look for dominator trees / retained size.
3.    "OutofMemoryError: Metaspace → classloader leak; check loaded class count over time.
4.    "OutOfMemoryError: Direct buffer memory" → NIO/Netty buffer leak; bound
MaxDirectMemorySize and audit buffer release.
5. Steady RSS climb with flat heap → native/JI leak; NMT baseline diff, or | "jemalloc profiling.
## Conmon Pitfalls
-   Equating ®-Xmx" with total memory - ignores metaspace, stacks, direct buffers, code cache.
-   Setting a fixed *-Xmx" in a container instead of "MaxRAMPercentage".
-   Forgetting that thread count multiplies stack memory.
-   Assuming the JVM is container-aware on an old JDK (< 8u191).
## Likely Interview Questions
-   Why does a container's RSS exceed *-Xmx"? Enumerate the off-heap regions.
-   How do you diagnose a native (off-heap) memory leak in the JVM?
-  What makes a JVM container-aware, and what breaks without it?
-   Metaspace vs. the old Permen - what changed in JDK 8?
-   Why prefer "MaxRAMPercentage" over •-Xmx" in Kubernetes?
## References
-  https://rdzimmer.medium.com/memory-neasurements-complexities-and-considerat.ions-3e4afcde888
-  JDK NMT docs: "jand"/ /"VM.native_memory".
-   JEP 379 (Shenandoah), JEP 377 (ZGC), G1 GC tuning guide.

