## 1. Scatter-gather I/O
<1-- docs/cpp/SA/system_performance_interview_overview_flow.md -->
Yes. Zero-copy belongs to a broader group of **systems-performance and data-movement techniques**. Important related concepts include the following.
## 1. Scatter-gather I/O
Instead of copying several pieces into one contiguous buffer, the application submits multiple buffers through an array of buffer descriptors, such as iovec".
Used by:
-   readv() and "writev()
-   sendmsg()* and recmsg() ;
-   Linux sockets;
-   high-performance HTTP and RPC libraries.
Example use:
-   buffer 1: protocol header;
-   buffer 2: existing payload;
-   buffer 3: trailer.
The kernel or NIC sends them as one logical packet without requiring the application to concatenate them first.
In C+t, this is commonly represented by non-owning views such as std:: span or a custom buffer descriptor.
## 2. DMA and registered memory
DMA allows a device to read or write memory without the CPU copying every byte.
Used by:
-   network cards;
-   storage controllers;
-   GPUS;
-   RDMA adapters;
-   video and audio devices.
Important issues include:
-   pinned memory;
-   buffer alignment;
-   IOMMU mappings;
-   cache coherency:
-   NUMA locality;
-  buffer lifetime until device completion.
DA is not automatically zero-copy. There can still be intermediate buffers or mapping overhead.
	- DPDK;
	- netmap;
	- AF _XDP;
	- RDMA verbs;
	- SPDK for storage;
	- Solarflare/OpenOnload technologies.
## 3.Kernel bypass
Kernel bypass allows applications to communicate with hardware while avoiding much of the normal kernel networking or storage path.
Popular examples:
Typical benefits:
I
Typical costs:
-   dedicated CPU cores;
-   polling;
-   complex memory management;
-   difficult integration with ordinary socket APIs;
-   more responsibility for packet processing and backpressure.
DPDK, for example, commonly uses preallocated packet pools and per-core queues rather than allocating a new object for every packet.
	- fewer system calls;	
	- fewer context switches;	
		- direct access to packet buffers;
	- better batching;	
	- predictable latency.	

mmap() maps a file or device region into a process address space rather than requiring explicit read() calls into application buffers.
Used by:
-   databases;
-   search engines;
-   operating systems;
-   shared-memory IPC;
-   memory-mapped indexes;
-   large immutable data structures.
Advantages:
-   convenient random access;
-   possible sharing between processes;
-   avoids an explicit user-space copy from a file buffer.
Caveats:
I
-   page faults still cost cPu time;
-   memory pressure can cause reclaim or I/o;
-   access patterns matter;
-   file changes and lifetime must be handled carefully;
-   mapping a huge file does not mean the entire file is resident in RAM.
## 5. Shared-memory IPC
Instead of serializing data through a pipe, socket, or messaging system, processes share a memory region and exchange only descriptors, offsets, or indexes.
Used by:
-   *shm_open) and mmap()*:
-   POSIX shared memory;
-   Boost. Interprocess;
-   Chromium-style shared-memory regions;
-   high-performance media pipelines;
-   shared-memory message queues.
The expensive payload is written once. Control messages contain metadata such as:

-   buffer offset;
-   length;
-   type;
-   ownership state;
-   sequence number.
The difficult part is synchronization and lifetime management. Shared memory does not remove the need for a protocol.
## 6.Buffer pools and object pools
A buffer pool reuses preallocated buffers rather than repeatedly allocating and freeing memory.
Used by:
-   Netty;
-   DPDK;
-   web servers;
-   database engines;
-   RPC frameworks;
-   packet-processing systems.
Benefits:
-   fewer allocator calls;
-   fewer page faults;
-   less fragmentation;
-   more predictable latency;
-   reduced garbage-collection pressure in managed languages.
For a C++ packet path, a buffer pool might provide fixed-size packet storage and return a buffer only after all asynchronous consumers have finished using it.
This is especially relevant to replicated forwarding. A single packet buffer can be shared if consumers only read it and ownership is tracked correctly. Otherwise, separate copies may be required.
7. Slab, arena, and monotonic allocation

These techniques optimize allocation patterns.
### Slab allocation
objects of the same type and size are allocated from fixed-size caches.
Common in:
-   Linux kernel subsystems;
-   networking;
-   connection tracking;
-   protocol object management.
### Arena allocation
Many objects are allocated from one region and released together.
Useful for:
-   request processing;
-   parsing;
-   temporary state-machine data;
-   query execution.
### Monotonic allocation
Memory is allocated sequentially and individual deallocation is avoided. The whole resource is released at once.
C++ support includes:
- *std: ipmr: monotonic_buffer_resource;
-   custom arenas;
-   request-local allocators.
These techniques are not zero-copy, but they reduce allocation overhead and improve locality.
## 8. Copy-on-write
Copy-on-write shares data until one participant modifies it. A copy is made only at the point of mutation.

Used historically or commonly in:
-   operating-system process creation;
-   immutable strings;
-   persistent data structures;
-  configuration snapshots;
-   filesystem snapshots;
-   container/image layers.
For example, a read-only configuration snapshot can be shared by many worker threads. A new version is created only when configuration changes.
Caveat: copy-on-write introduces hidden copying and synchronization on writes, so it is most useful when reads greatly outnumber modifications.
#* 9. Immutable data and ownership transfer
Many high-performance systems avoid copying by making buffers immutable after publication.
The producer transfers ownership to the consumer, or multiple consumers share read-only ownership.
Common techniques:
-   move ownership using RAII;
-   reference-counted buffers;
-   intrusive reference counting;
-   immutable message objects;
-   descriptor queues;
-   producer/consumer ownership states.
In Ct+, moving a std: vector usually moves its allocation metadata rather than copying its elements, but this is not the same as zero-copy I/o. It only avoids copying during object transfer.
• std:: span also avoids copying the descriptor's viewed bytes, but it does not own the data and does not extend its lifetime.
#* 10. RCUand immutable snapshots

	- the Linux kernel;
- routing tables;	
	- configuration management;
	- service-discovery data;
	- read-heavy caches;
	- concurrent lookup tables.
Read-copy-update, or RCU, allows readers to access a mostly immutable structure without taking a lock. Writers create or modify a new version and publish it.
Used in:
A C++ service can use an immutable snapshot for tunnel configuration:
-   readers perform lock-free or low-lock lookups;
-   an updater builds a new table;
-   the new table is atomically published;
-  the old table is reclaimed after readers finish.
The important distinction is that RUavoids blocking readers, but writers may still copy or rebuild data.
## 11. Batch processing
Batching processes several operations together instead of handling each item separately.
Used by:
-   DPDK;
-   io_uring ;
-   GPU APIS;
-   databases;
-   Kafka producers;
-   network drivers;
-   vectorized storage engines.
Batching reduces:
-   system calls;
*   queue operations;
-   lock acquisitions;
-   interrupt overhead;

- per-message metadata processing.
For packet forwarding, processing a batch can improve cache and branch behavior. However, excessive batching increases latency, so batch size and timeout require measurement.
## 12. Polling and busy polling
Instead of sleeping and being woken by an interrupt for every event, a worker repeatedly checks a queue or device.
Used by:
-   DPDK;
-   high-frequency trading systems;
-   RDMA applications;
-   some storage engines;
-   high-performance network servers.
Advantages:
I
-   Lower wake-up Latency:
-   fewer context switches;
-   more predictable behavior.
Disadvantages:
-   consumes a CPU core;
-   can waste CPU under low traffic;
-   requires careful power and scheduling management.
## 13. Asynchronous I/O and completion queues
Asynchronous I/0 lets an application submit operations and receive completions later.
Examples:
-   Linux "io_uring ;
*   Windows IOCP;
-   POSIX AIO;Boost Asio.

-   Libuv;
-   RDMA completion queues.
petion queues
A key design principle is to keep the buffer alive until the completion event arrives.
Reusing or freeing it too early causes corruption or use-after-free. io_uring additionally supports features such as:
-   submission and completion rings;
-   batched submissions;
-   registered buffers;
-   fixed files;
-   polling modes:
-   reduced system-call overhead.
## 14. Persistent and memory-efficient data structures
Some systems reduce data movement by storing data in formats that can be accessed directly.
Examples:
-   Apache Arrow;
-   memory-mapped indexes;
-   B-trees;
-   log-structured storage;
-   STables;
-   immutable segment files;
-   columnar storage.
Apache Arrow is particularly important because it defines a columnar in-memory format that can be shared between systems and languages with little or no conversion.
This avoids repeated transformations such as:
database row format -> Language objects -> serialized message - another language's
objects
Instead, consumers can operate on a common representation.


## 15. Serialization avoidance and zero-copy deserialization
Serialization can dominate service CUusage. Systems often reduce it by:
-   using binary formats;
-   reusing serialized buffers;
-   parsing in place;
-   using length-prefixed messages;
-   using offset-based structures;
-   sharing an Arrow or FlatBuffers representation.
Examples:
-   FlatBuffers;
-   Cap'n Proto;
-   Apache Arrow;
-   memory-mapped binary indexes.
FlatBuffers and Cap'n Proto allow access to fields directly inside a serialized buffer, although alignment, compatibility, bounds checking, and buffer lifetime remain important.
This is different from ordinary protobuf decoding, which generally creates a separate in-memory object representation.
	- packet parsing;
	- checksum calculation;
	- compression;
	- encryption;
	- JSON and protocol parsing;
	- database scans;
	- search engines.
## 16.SIMD and vectorized processing
SIMD processes multiple values with one instruction or instruction group.
Used by:
Popular libraries and technologies include:
-   AVX2 and AVX-512;
-   ARM NEON;
-   Intel IPP;
-   Google Highway;


-   simdjson;
-   xsimd.
SIMD does not reduce copying directly. It reduces Cuinstructions required to inspect or transform data.
For a packet path, SIMD may help with:
-   scanning headers;
-   classifying packets;
-   checking multiple fields;
-   checksum operations;
-   filtering batches of packets.
## 17. Compression and encoding optimizations
Compression can reduce network and storage traffic, but it consumes CPU. High-performance systems often combine:
-   SIMD-optimized codecs;
-   streaming compression;
-   dictionary compression;
-   compression by batch;
-   hot/cold data separation;
-   selective compression.
Examples:
-   LZ4;
-   Snappy;
-   Zstandard;
-   Brotli;
-   hardware-assisted compression.
Compression is beneficial when saved I/O or bandwidth is more expensive than the CPU cost.
It can be harmful for already-small packets or latency-sensitive paths.
## 18. Cache and NUMA-aware design
Performance often depends more on memory locality than on algorithmic complexity.

Techniques include:
-   per-thread data;
-   per-core queues;
-   CPU affinity;
-   NUMA-local allocation;
-   avoiding false sharing;
-   cache-line alignment;
-   structure-of-arrays layouts;
-   reducing pointer chasing.
Examples:
-   one packet queue per worker;
-   one statistics counter per thread;
-   separate producer and consumer indexes onto different cache lines;
-  allocating packet buffers on the NuMA node of the NIC.
This is especially important for lock-free structures. Removing a mutex does not help if every thread continuously modifies the same cache line.
## 19. Control-plane/data-plane separation
Many high-throughput systems separate slow configuration work from the hot data path.
Examples:
-   routing-table updates;
-   tunnel lifecycle management;
-   service discovery;
-   policy changes;
-   connection management.
The data plane uses an immutable or optimized snapshot. The control plane prepares and publishes a new version.
For a GrE forwarding service, this can mean:
-   control plane maintains tunnel and consumer-group configuration;
*   data-plane workers perform read-only lookups;
-   updates replace a snapshot rather than locking every packet operation.


You can organize these topics as follows:
### Data movement
4##14
-   zero-copy;
-   DMA;
-   scatter-gather I/0;
-   shared memory:
-   memory mapping;
-   copy-on-write.
#*# Allocation and lifetime
-   buffer pools;
-   slab allocators;
-   arenas;
-   monotonic allocators;
-   ownership transfer;
-   reference counting;
-   deferred reclamation.
#*# I/O and scheduling
-   asynchronous I/0;
-   completion queues;
-   batching;
-   polling;
-   kernel bypass;
-   registered buffers.
### CPU efficiency
-   SIMD;
-   vectorized processing;
-   branch reduction;
-   cache locality;
-   NUMA awareness;
-   false-sharing avoidance.
### Concurrency
-   lock-free queues;
-   RCU;immutable snanshots:

-   per-thread state;
-   SPSC and MPMC ring buffers;
-   hazard pointers and epoch reclamation.
### Data representation
-   binary serialization;
-   zero-copy deserialization;
-   FlatBuffers;
-   Cap'n Proto;
-   Apache Arrow;
-   columnar storage.
The most important interview distinction is:
*   Zero-copy reduces data movement. Buffer pooling reduces allocation. Batching reduces
*   per-operation overhead. SIMD reduces CPU instructions. Lock-free algorithms reduce
*   blocking. These solve different bottlenecks and are often combined, but none of them
*   automatically guarantees better performance.
