
<!-- docs/cpp/cpp_zero_copy_interview.md -->

Answers: [`cpp_zero_copy_interview_answers.md`](cpp_zero_copy_interview_answers.md) (same
numbering; `Q4" is at `#q4`.)

Zero-copy should mainly be classified under **systems performance and I/O optimization**,
not under C++ concurrency or lock-free programming.

A useful classification is:

## 1. Memory and data movement

Zero-copy reduces unnecessary memory copying between buffers or execution domains.
Typical topics:

- `memcpy` cost;	
- memory bandwidth;	
- cache pollution;	
- buffer ownership;	
- buffer lifetime;	
- scatter/gather I/O;	
- `iovec`:	
- `std::span` and non-owning views;
- move semantics versus actual physical copying.

## 2. Operating-system and networking I/O

For network applications, zero-copy belongs strongly to os and networking internals:

- user space to kernel space copying;
- kernel socket buffers;
- `sendfile`;
- `splice`;
- `MSG_ZEROCOPY`;
- `io_uring`;
- `mmap`;
- DMA;
- NIC and device buffers;
- page pinning and registered memory.

The exact mechanism depends on the data path. For example `splice()` can avoid copying
between certain file descriptors through a kernel pipe, but it does not generally mean
that arbitrary user memory or GPU VRAM can be directly sent to a network device.

## 3. Cache and memory-performance design

Zero-copy also connects to cache behavior:

- fewer CPU load/store operations;
- less cache pollution;
- reduced memory-bandwidth pressure;
- better throughput;
- possible cache-line contention;
- buffer alignment;
- NUMA locality.
 
However, zero-copy does not always mean faster. Sharing a buffer may introduce
synchronization, buffer-lifetime management, cache contention, or expensive page pinning.

## 4. Concurrency and ownership

Zero-copy is not inherently a concurrency technique, but it often creates
concurrency-related problems:

- Who owns the buffer?
- When can the producer reuse it?
- When has the network device finished reading it?
- Is reference counting required?
- Can multiple consumers access it concurrently?
- Is the buffer immutable after publication?
- How is reclamation performed safely?

For example, a producer cannot reuse a buffer immediately after submitting it to an
asynchronous network operation unless the API guarantees that the operation has completed.

## 5. GPU and accelerator data movement

If the source is GPU memory, classify it additionally under:

- GPU-CPU data transfer;
- pinned host memory;
- unified memory;
- CUDA-aware networking;
- GPUDirect RDMA;
- device-to-device transfers;
- DMA engines.

A normal Linux zero-copy API does not automatically provide GPU-to-NIC zero-copy. That
usually requires hardware, driver, and framework support such as GUDirect RDMA.

## Recommended interview taxonomy

You could organize your C++ interview topics like this:

C+ performance |--- object layout and cache locality |--- Memory allocation |-- Data
movement and zero-copy |-- SIMD and vectorization |-- I/O and networking |-- NUMA and DMA
|__ Benchmarking

C++ concurrency |-- Mutexes and condition variables |-- Atomics and memory ordering |--
Lock-free data structures |-- Thread pools |-- Queues and ring buffers |-- Memory
reclamation

Zero-copy belongs primarily in:

> **C++ performance → memory/data movement → OS and networking I/O**

It should be connected to concurrency when discussing asynchronous I/O, buffer ownership,
lifetime, and reclamation.

Examples of zero-copy interview questions:

1. What problem does zero-copy solve?
2. Does move semantics guarantee zero-copy?
3. What is the difference between avoiding a user-space copy and avoiding all copies?
4. How do `sendfile()`, `splice()` and `MSG_ZEROCOPY` differ?
5. What are the buffer-lifetime requirements for asynchronous zero-copy I/OP
6. Why can zero-copy increase memory-management complexity?
7. How can zero-copy affect CPU caches and memory bandwidth?
8. What is the role of DA in zero-copy networking?
9.  Why does zero-copy from GPU memory require special hardware or driver support?
10. When can a normal copy outperform a zero-copy design?
11. How would you design a reusable buffer pool for zero-copy packet forwarding?
12. How would multiple consumers safelv share a zero-copy buffer?
13. How would you detect when a NIC or asynchronous I/O operation has finished using a
    buffer?
14. What are the effects of NuMA locality on zero-copy networking?
15. How would you benchmark a zero-copy path fairly against a copying path?

For your PVTB use case, zero-copy would be especially relevant to the **packet
forwarding/data plane**, while GRE-key extraction and tunnel selection may still require
reading packet headers. A practical design might avoid copying the entire packet while
parsing only the necessary header and forwarding the original buffer or buffer fragments.
