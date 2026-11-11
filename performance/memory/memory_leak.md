KI-- performance/memory/memory_leak.md -->
# Detecting and Diagnosing Memory Leaks
What a memory leak is in Linux terms, how to detect one, and the tool chain to localize it - from coarse system signals (free/"vmstat") down to allocation stacks ("memleak, "valgrind", "heaptrack', ASan).
## What Counts as a Leak
A leak is memory that is allocated and never freed while st111 being unreachable (or never reused). On Linux this shows up as steadily growing RS** (or growing VRSS/anon pages) over time with no corresponding workload growth and no plateau.
Not every growth is a leak:
-   Page cache growth is not a leak (reclaimable - see "buffer_cache.md").
-   A heap that grows to a steady plateau is normal (allocator caching, see "brk" behavior in memory mapping-md).
-   A true leak grows *without bound* until oOM.
*# Step 1: Detect at the System Level
• Watch total used/free and swap trend
buff cache si so bi bo in cs us sy id wa st
0 6337240 652176 24282092 0 0 5 20 1089 2 1 6 99 0 •
Falling "free + rising "so
free- snapshots or artora data hnes at a leak. Confirmo with
Attribute to a process:
# RSS growth per process
$ pidstat -r1
ID PID minflt/s maiflt/s VS
04242
230.0
# or a quick ranking
$ ps -eo pid, comm, rss --sort=-rss | head
RSS %MEM Command
6 812340 400120 1.2 mya:
A process whose "RSS" climbs monotonically across samples is the suspect.
## Step 2: Localize with Allocation Tracing
### memleak (bcc / eBPF) - production-friendly
"memleak attaches to malloc/ free" (and kernel allocators) via eBPF and reports
*  outstanding** allocations with their stacks - no recompilation, low overhead, works on a running process.
*  Trace PID PID print top outstanding allocations every 5 s $ memleak-bpfcc -p 4242 5
Attaching to pid 4242...
[13:40:01] Top 10 stacks with outstanding allocations:
40960 bytes in 10 allocations from stack my parse_record+0x3f [myapp]
handle_request+0x1a2 [myapp]
The stack with ever-growing outstanding bytes is the leak site. Requires
*-p‹pid› and the binary's symbols (or debuginfo) for readable frames.
# valgrind memcheck - precise, heavy
Best in dev/test; runs the program on a synthetic CPU, so it is slow (20-50x) but catches leaks, invalid frees, and use-after-free precisely.
$ valgrind --leak-check-fu11 --show-leak-kinds-all /myapp
-1234== LEAK SUMMARY:
==1234==
definitely lost: 40,960 bytes in 10 blocks
=1234==
indirectly lost: bytes in e blocks
==1234==
at ex...: malloc (vg_replace_malloc.c)
==1234=3
••.
by ex...: my_parse_record (parser,c:88)
"definitely lost" blocks are leaks; the stack points at the allocation.
* heaptrack - fast heap profiler with GUI
Lower overhead than valgrind; records every allocation and gives flame graphs and a timeline of heap growth, Ideal for "where is my heap growing" analysis. $ heaptrack /myapp
$ heaptrack_gui heaptrack.myapp.1234.zst # or heaptrack_print
### AddressSanitizer (ASan) - compile-time
Instrument at build time (*-fsanitize-address) with
*ASAN OPTIONS-detect leaks-1° (LeakSanitizer)
Fast enough for CI test suites and

pinpoints the allocation stack of leaked blocks at process exit.
gcc -fsanitize-address -g myapp.c -o myapp
$ -/myapp
-1234--ERROR: LeakSanitizer: detected memory leaks
Direct leak of 40960 byte(s) in 10 objects) allocated from:
# malloc
#1 my_parse record parser.c: 88
### Language-specific
-   JVM: heap dump+Eclipse MAT, NNT for native leaks - see "jum_memory.md.
-   Go: "pprof heap profiles - see "go memory md.
#* Tool Selection Guide
I Tool | Overhead | Needs rebuild | Live process | Best for |
-  ----- ا
-  -_-____|--_-____-|--___--
"memleak
(bec) | Low | No | Yes | Production C/C++, running services | valgrind | Very high | No | No (relaunch) | Dev/test, precise correctness |
"heaptrack" | Low-med | No | No (relaunch) | Heap-growth profiling, GUI l
I ASan/LSan | Medium | Yes | No | CI, unit/integration tests |
## Troubleshooting Workflow
1.    Confirm it's a real leak: monotonic RSS growth, not cache, not a plateau(vnstat"(' pidstat -r").
2.    Attribute to a process (ps --sort=-rss, "pidstat -r°).
3.    Pick a tracer: live service → memleak; reproducible in test → valgrind/ASan;
4. need timeline → heaptrack.
5.    Read the allocation stack; fix the missing free / retained reference.
6.    Re-run under the same tool to confirm the leak is gone.
#* Common Pitfalls
-   Mistaking page-cache growth or allocator caching for a leak.
-   Running valgrind in production (too slow) or on a process that must stay up.
-   Missing symbols/debuginfo → unreadable stacks from "memleak.
-   Forgetting "ASAN_OPTIONS-detect_leaks-1" (on some platforms LSan is off by default).
-  A plateauing heap is not* a leak -watch long enough to distinguish growth from steady state.
# Likely Interview Questions
-  How do you tell a memory leak apart from healthy cache/allocator growth?
-   Which tool would you use to find a leak in a running production service,
-   valgrind vs. ASan vs. memleak - trade-offs?
-   What does "definitely lost" vs "still reachable" mean in valgrind?
-   How would you find a leak in a VM or Go service?
why?
## References
-   bcc 'memleak tool documentation.
-   "man valgrind; Valgrind memcheck manual.
-   heaptrack project docs; LLVM AddressSanitizer / LeakSanitizer docs.
- 
