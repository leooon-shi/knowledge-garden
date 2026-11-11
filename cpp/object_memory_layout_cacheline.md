<! -- docs/cpp/object_memory_layout_cacheline.md -->
Yes. In C++, object size and layout can directly affect cache behavior, especially in high-throughput or multithreaded code.
### 1. objects are loaded through cache lines
The CPU usually transfers memory between cache levels in cache-line-sized blocks. On many modern systems, a cache line is 64 bytes, although this is platform-dependent.
Therefore, when a thread accesses one field of an object, the cUmay fetch the surrounding cache line as well.
cpp
struct Packet {
uint32_t protocol; uint32_t length; uint64_t timestamp; char payload[32];
static_assert(sizeof(Packet) = 48);
If a vector contains many "Packet" objects, accessing one packet often brings neighboring packet data into the cache too. Compact objects generally improve cache locality.
I
### 2. Padding changes the effective size
The compiler inserts padding to satisfy alignment requirements.
*** cpp
struct BadLayout {
char
flag;
uint64_t value;
int
count;
// 1 byte
// usually requires 8-byte alignment
};
struct BetterLayout {
uint64_t value;
int
count;
char flag:

* Coradding changes the ettective size
" BetterLayout" compiler.
may use less padding, although the exact size depends on the ABI and
Inspect the result instead of assuming:
***cpp
#include < cstddef> #include <iostream›
std: cout « sizeof (BadLayout) «< "\n'; std:: cout « offsetof (BadLayout, value) « "In';
The member order is part of the performance design, but do not reorder members if binary layout or ABI compatibility must be preserved.
### 3. Empty classes still need an address
A normal empty object usually has size one:
struct Empty (7;
static_assert(sizeof(Empty) >= 1);
Empty a;
Empty b;
assert (&a l= &b);
The byte does not represent meaningful data. It allows distinct objects, including adjacent array elements, to have distinct addresses.
However, empty base optimization can remove that overhead:
***cpp
struct Tag ();
I
struct object : Tag (
int value;


static_assert(sizeof(object) = sizeof(int));
C++20 also provides "[[no_unique_address]] for potentially overlapping empty members:
struct Tag Os
struct object {
[[no_unique_address]] Tag tag;
int value;
This is useful for policy classes, allocators, comparators, and marker types. It should not be used when the member must have a unique address.
### 4. Array-of-structures versus structure-of-arrays
For packet processing, the layout can strongly affect cache usage.
***cpp
struct Packet {
uint32
_t protocol;
uint32
_t length;
uint64_t timestamp; char payload[128];
};
I
std:: vector<Packet> packets;
If the algorithm only needs "protocol and length, each access may pull the unused timestamp and payload into the cache.
A structure-of-arrays layout avoids that:
***cpp
struct PacketFields {
std:: vector<uint32_t> protocols;
std:: vector<uint32t> lengths;


std:: vector<uint64_t> timestamps;
std:: vector<std: :array<char, 128>> payloads;
This is often beneficial when:
-   processing large batches;
-   scanning one or two fields across many objects;
-   using SIMD;
-   the object contains large cold fields;
-   packet classification accesses only metadata.
An array-of-structures layout is often better when the complete object is processed together.
##* 5. False sharing between threads
Two independent variables can be placed on the same cache line:
cpp
struct Counters
std:: atomic<uint64_t> producer_count; std:: atomic<uint64_t> consumer_count;
If different cores update these counters repeatedly, the cache line may constantly move between cores. This is called false sharing.
Padding or alignment can separate them:
•*cpp
#include <atomic>
struct alignas (64) Counter ( std:: :atomic<uint64_t> value(0};
struct Counters
Counter producer count;

en treads
Counter consumer_count;
Now each counter is intended to occupy a separate cache line. This is useful for:
-   ring-buffer producer and consumer indexes;
-   per-core statistics;
-   lock-free queues;
-   frequently updated state owned by different threads.
However, padding increases memory consumption. It should be applied only to genuinely contended fields.
### 6. object alignment
You can request stronger alignment:
** cpp
struct alignas(64) CacheAlignedstate {
uint64_t value;
};
But alignment does not automatically improve performance. It can also:
-   increase object size;
-   create more cache pressure;
-   waste memory:
-   reduce the number of objects fitting in a page.
For ordinary data, natural alignment is usually sufficient. Explicit alignment is most valuable for cache-line isolation, SIMD data, DMA requirements, or hardware-specific interfaces.
#** 7. Hot and cold data separation
A large class should not necessarily contain every field used by the system.
•*cpp
cout HotconsumerState

uint32_t consumer_id; uint32_t tunnel_id; uint64_t packet_count;
｝；
struct ColdConsumerConfig €
std:: string name;
std: string endpoint;
std:: vector<uint32_t> backup_routes;
The forwarding path can access only "HotConsumerstate, while configuration and diagnostic information remain elsewhere.
This is particularly useful for PVTB-style packet forwarding:
cpp
struct TunnelHotstate
uint32
_t gre_key;
uint32_t consumer_index; uint64_t packets_forwarded;
};
struct Tunnelcoldstate f std: :string domain; std:: string protocol;
std: vector<std:: string> metadata;
｝；
The fast path should avoid loading strings, vectors, rarely used flags, and diagnostic structures.
#*# 8. Virtual functions affect layout
A polymorphic class generally needs a virtual-table pointer:
*** cpp
struct Base
｛
Virtual ~Base) = default.
I

sayout
virtual void process) = 0;
spration
on a typical 64-bit implementation, the object commonly contains an additional pointer-sized field. The exact layout is implementation-dependent.
Virtual dispatch can also affect performance because it may:
-   require an indirect call;
-   reduce inlining opportunities;
-   make branch prediction more difficult;
-   cause different derived objects to access different code paths.
This does not mean virtual functions should always be avoided. It means the hot path should be measured. For fixed packet-processing types, alternatives can include:
-   templates;
-   "std: :variant";
-   function tables;
-   integer-based dispatch;
-   specialized code paths.
### 9. Useful layout inspection
Compile with warnings and inspect object layout:
•**bash
clang++ -std=c++20 -02 -Wall -Wextra -Wpadded layout. cpp
clang can dump record layouts:
•**bash
clang++ -Xclang - fdump-record-layouts -fsyntax-only layout. cpp
For generated code and cache behavior:
•* bash
nepf stat -e cache-references, cache-misses •/program

perf record -e cache-misses •/program perf report
You should compare realistic workloads rather than relying only on sizeof.
### Practical checklist
when designing a performance-sensitive class, inspect:
-   "sizeof and "alignof";
-   member offsets;
-   padding between members;
-   number of objects per cache line;
-   whether hot fields are mixed with cold fields;
-   whether different threads update fields on the same cache line;
-   whether a virtual pointer or indirection is necessary;
-   whether an array-of-structures or structure-of-arrays layout fits the access pattern;
-   cache misses and throughput in a benchmark.
The important distinction is:
*   Smaller objects improve density and locality, while carefully separated objects reduce
*   contention and false sharing.
Those goals can conflict, so the correct layout depends on whether the workload is primarily single-threaded scanning, random lookup, or multi-core concurrent updates.
