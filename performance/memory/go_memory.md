<-- performance/memory/go_memory.md -->
# Go Runtime Memory and Garbage Collection
How the Go runtime manages memory, how its garbage collector works, the knobs that control it ( GOGC,
"GOMEMLIMIT"), and how to profile heap usage with "pprof.
General leak detection is in "memory_leak.md"; container 1imits in
*memory_k8s.md.
## Go Memory Model at a Glance
The Go runtime manages heap memory itself on top of the 05 (via "mmap"), so you rarely call anything like "malloc/ free' directly:
-  **Stacks** are per-goroutine, start tiny (~2 KB/8 KB) and grow/shrink automatically. Cheap goroutines are possible because of this.
-   **Heap** holds escaping allocations. The compiler's **escape analysis** decides whether a value lives on the stack (freed automatically when the frame returns) or escapes to the heap (GC-managed). "go build -gcflags-'-m" shows escape decisions.slab/tcmalloc design, mininizing lock contention.
** The Garbage Collector
Go uses a **concurrent, tri-color mark-and-sweep** collector with a write barrier, designed for **low latency** rather than maximum throughput:
-   Mark and sweep run concurrently with the application; stop-the-world (STW) pauses are brief (sub-millisecond, bounded) and happen only at phase transitions.
-   The GC is **not generational and not compacting** - it does not move objects, so there is no compaction pause, but it also does not defragment.
-   A background "scavenger" returns unused pages to the 05 over time (RSS may lag behind freed heap).
## GC Tuning Knobs
### GOGC
- Beyond he we set aet e preis ole on e at ros collect
when the heap doubles).
S GOGC-208 ./app
$ GOGC-59
-/app
$ GOGC-off -/app
Also settable at runtime with debug. Setcercent (n)*.
### GOMEMLIMIT (Go 1.19+)
A **soft** memory limit for the whole runtime (heap + runtime overhead). The GC becomes more aggressive as usage approaches the limit, trading CPU to avoid
but it greatly reduces 0OM kills in containers.
...
$ GOMEMLIMIT-512MiB /app
*or runtime: debug-Set emoryLimit512 28)
limit and leave GOGC at default (or "GOGC-off" and rely solely on the 11mit for
Sized 6c for the host, not the container" problem. (See "memory_kas-md.)
## Profiling with pprof
Go ships first-class heap profiling via runtime/pprof and net/http/pprof®.
** go import _ "net/http/pprof"
// exposes /debug/pprof/ on your HTTP server
* Live heap profile (in-use space)
so tool prof http://localhost:6969/debug/pprof/heap
(pprof) top
* top allocators by in-use memory
(prof) list FuncName
(prof) web
# call graph (needs graphviz)
*Allocation profile (cumulative allocs, good for churn/GC pressure) go tool prof-alloc_space http://localhost:6968/debug/pprof/heap
Key prof heap sample types:
*anuse space / anuse_objects** - currently 11ve memory (Leak hunting).
- **alloc_space / alloc_objects** - total allocated over time (GC-pressure / allocation-churn hunting).
Runtime stats without pprof:

$ GODEBUG-gctrace=1 /app
* prints a line per GC cycle: heap sizes, pause, CPU
gc 42 @12.3s 0%: 0.05+1.2+0.03 ms clock, ... 100->180->90 MB, .••
Reads as: before-peak-after heap, pause times, and GC CPU fraction.
## Troubleshooting Workflow
1.    RSS climbing? Confirm it is live heap, not just un-scavenged pages (GODEBUG-gctrace-1', or "runtime.ReadMemStats").
2.    Grab an **inuse_space** heap profile with pprof; "top"/ list" to find the retaining code - common causes: growing maps/slices never trimmed, goroutine leaks holding references, caches without eviction.
3.    For high GC CPU, grab **alloc_space*» and reduce allocation churn (reuse buffers, "sync.Pool', preallocate slices).
4.    In containers, verify "GOMEMLIMIT /' GOGC are set to the group limit.
5.    Goroutine leaks: "go tool pprof .../debug/pprof/goroutine - a steadily rising goroutine count often *is* the memory leak.Common Pitfalls
-  **Not setting "GOMEMLIMIT" in a container**→ GC sizes for the host; 00MK111ed.
-   Treating high RSS as a leak when it is un-scavenged memory the runtime will | return lazily.
-  **Goroutineleaks**masquerading as memory leaks (each leaked goroutine retains its stack and referenced objects).
-   Using "GOGC-off in a long-running service.
-   Confusing "inuse_space" (Live) with "alloc_space" (cumulative) profiles.
# Likely Interview Questions
-   Describe Go's GC: concurrent? generational? compacting?
-   What do "GOGC" and "GOMEMLIMIT control, and how do they interact?
-   How do you configure a Go service to not get 00Mkilled in Kubernetes?
-   inuse_space vs, alloc_space in a pprof heap profile?
-   What is escape analysis and why does it matter for memory?
-  How can a goroutine leak present as a memory leak?
## References
-   Go docs: "A Guide to the Go Garbage Collector" (GOGC", "GOMEMLIMIT').
-   "runtime", "runtime/pprof, 'net/http/pprof package docs.
-   "go tool pprof documentation; "GODEBUG-gctrace-1°,
