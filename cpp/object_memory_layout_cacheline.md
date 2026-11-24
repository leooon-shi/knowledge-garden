<!-- docs/cpp/object_memory_layout_cacheline.md -->

Yes. In C++, object size and layout can directly affect cache behavior, especially in
high-throughput or multithreaded code.

### 1. objects are loaded through cache lines

The CPU usually transfers memory between cache levels in cache-line-sized blocks. On many
modern systems, a cache line is 64 bytes, although this is platform-dependent.

Therefore, when a thread accesses one field of an object, the CPU may fetch the
surrounding cache line as well.

```cpp
struct Packet {
    uint32_t protocol;
    uint32_t length;
    uint64_t timestamp;
    char payload[32];
};

static_assert(sizeof(Packet) = 48);
```

If a vector contains many `Packet` objects, accessing one packet often brings neighboring
packet data into the cache too. Compact objects generally improve cache locality.

### 2. Padding changes the effective size

The compiler inserts padding to satisfy alignment requirements.

```cpp
struct BadLayout {
    char flag; // 1 byte
    uint64_t value; // usually requires 8-byte alignment
    int count;
};
struct BetterLayout {
    uint64_t value;
    int count;
    char flag:
}
```

`BetterLayout` compiler. may use less padding, although the exact size depends on the ABI and
compiler.

Inspect the result instead of assuming:

```cpp
#include <cstddef>
#include <iostream>

std::cout << sizeof(BadLayout) << '\n';
std::cout << offsetof(BadLayout, value) << '\n';
```

The member order is part of the performance design, but do not reorder members if binary
layout or ABI compatibility must be preserved.

A caveat on `offsetof`: it is only guaranteed to be well-defined for *standard-layout*
types. A standard-layout class has, roughly, no virtual functions, no virtual bases, a
single access control for all non-static data members, and no base class with non-static
data members (or only one class in the hierarchy has them). `BadLayout` and `BetterLayout`
above qualify, so `offsetof` is fine there. But the moment a class gains a virtual function
or a non-empty base, it is no longer standard-layout, and `offsetof` becomes conditionally
supported - some implementations still produce the expected number, others warn or give
unspecified results. For those polymorphic types, reach for the compiler's record-layout
dump (section 9) rather than `offsetof`:

```cpp
#include <type_traits>

static_assert(std::is_standard_layout_v<BadLayout>,
              "offsetof is only well-defined for standard-layout types");
```

`std::is_standard_layout_v` is a cheap compile-time guard to keep `offsetof` honest.

### 3. Empty classes still need an address

A normal empty object usually has size one:

```cpp
struct Empty {};

static_assert(sizeof(Empty) >= 1);

Empty a;
Empty b;

assert (&a != &b);
```

The byte does not represent meaningful data. It allows distinct objects, including
adjacent array elements, to have distinct addresses.

However, empty base optimization can remove that overhead:

```cpp
struct Tag {};

struct Object : Tag {
    int value;
};

static_assert(sizeof(Object) == sizeof(int));
```

C++20 also provides `[[no_unique_address]]` for potentially overlapping empty members:

```cpp
struct Tag {};

struct Object {
    [[no_unique_address]] Tag tag;
    int value;
};
```

This is useful for policy classes, allocators, comparators, and marker types. It should
not be used when the member must have a unique address.

There is an important subtlety when multiple `[[no_unique_address]]` members share the
same type. The attribute lets a member *potentially* overlap other members, but two
objects of the same type must still have distinct addresses. So two adjacent
`[[no_unique_address]]` members of the *same* empty type cannot both collapse to the same
offset - at least one must be placed at a distinct address, reintroducing a byte of size:

```cpp
struct Empty {};

struct TwoSame {
    [[no_unique_address]] Empty a;
    [[no_unique_address]] Empty b;   // same type as 'a` -> cannot fully overlap it
    int value;
};

// 'a' and 'b' cannot share one address, so the attribute does not save the full byte here.
static_assert(sizeof(TwoSame) > sizeof(int) || sizeof(TwoSame) == sizeof(int));
```

when the empty members have *different* types, they may all overlap with the following
member and collapse away:

```cpp
struct TagA {};
struct TagB {};

struct Twodifferent {
    [[no_unique_address]] TagA a;
    [[no_unique_address]] TagB b;
    int value;
};

static_assert(sizeof(TwoDifferent) == sizeof(int));
```

This is the same rule that governs empty base optimization: a class cannot have two base
subobjects of the same type at the same address either. It matters most for templates that
compose several policy members - if two of them happen to resolve to the same empty type,
the expected size saving quietly disappears.

### 4. Array-of-structures versus structure-of-arrays

For packet processing, the layout can strongly affect cache usage.

```cpp
struct Packet {
    uint32_t protocol;
    uint32_t length;
    uint64_t timestamp;
    char payload[128];
};

std::vector<Packet> packets;
```

If the algorithm only needs `protocol` and `length`, each access may pull the unused
timestamp and payload into the cache.

A structure-of-arrays layout avoids that:

```cpp
struct PacketFields {
    std::vector<uint32_t> protocols;
    std::vector<uint32_t> lengths;
    std::vector<uint64_t> timestamps;
    std::vector<std::array<char, 128>> payloads;
};
```

This is often beneficial when:

- processing large batches;
- scanning one or two fields across many objects;
- using SIMD;
- the object contains large cold fields;
- packet classification accesses only metadata.

An array-of-structures layout is often better when the complete object is processed
together.

### 5. False sharing between threads

Two independent variables can be placed on the same cache line:

```cpp
struct Counters {
    std::atomic<uint64_t> producer_count;
    std::atomic<uint64_t> consumer_count;
};
```

If different cores update these counters repeatedly, the cache line may constantly move
between cores. This is called false sharing. Section 12 explains the coherence mechanism
this makes this expensive; the short version is that each write forces the writing core to
take exclusive ownership of the line and invalidate every other core's copy.

Padding or alignment can separate them:

```cpp
#include <atomic>

struct alignas (64) Counter {
    std:::atomic<uint64_t> value{0};
};

struct Counters {
    Counter producer_count;
    Counter consumer_count;
};
```

Now each counter is intended to occupy a separate cache line. This is useful for:

- ring-buffer producer and consumer indexes;
- per-core statistics;
- lock-free queues;
- frequently updated state owned by different threads.

However, padding increases memory consumption. It should be applied only to genuinely
contended fields.

The literal `64` above is an assumption. The cache-line size is platform-dependent: 64
bytes on most x86-64 and many ARM cores, but 128 bytes on Apple Silicon and some POWER
chips, and some systems prefetch cache-line *pairs*, making the effective interference
granularity 128 bytes even where a line is 64. C++17 provides two constants in `<new>` to
express intent portably:

```cpp
#include <new>
#include <atomic>

#ifdef __cpp_lib_hardware_interference_size
constexpr std::size_t kdestructive = std::hardware_destructive_interference_size;
constexpr std::size_t kconstructive = std::hardware_constructive_interference_size;
#else
// Fallback for toolchains that do not define the feature.
constexpr std::size_t kDestructive = 64;
constexpr std::size_t kconstructive = 64;
#endif

struct alignas(kDestructive) Counter {
    std::satomic<uint64_t> value{0};
};
```

The two constants express opposite goals, and conflating them is a common mistake:

- `hardware_destructive_interference_size` is the *minimum* offset you should place
  between two objects to **avoid** false sharing - use it to pad apart fields updated by
  different threads (the "Counter" case above).
- `hardware_constructive_interference_size` is the *maximum* size of a region that is
  likely to share a cache line - use it to pack together data you want fetched **together**
  (for example, a key and its most-frequently-read companion fields).

A practical caveat: because these are `constexpr` and must be stable across translation
units that might be linked together, implementations bake in a fixed, conservative value
at compile time (GCC and clangtypically report 64, though this can differ by target).
That value is a compile-time guess, not a runtime query of the actual CPU. If you need the
true line size of the machine you are running on, query it at runtime instead - for
example `sysconf(_SC_LEVEL1_DEACHE_LINESIZE)` on Linux, or
`std::thread: hardware_concurrency()`: -adjacent platform APIs. Some toolchains even emit a
warning if you use the constants as an ABI boundary for exactly this reason. For
single-process, single-build code, the constants are the right default; for
layout-on-the-wire or shared-memory ABIs, pin an explicit number and document it.


### 6. object alignment

You can request stronger alignment:

```cpp
struct alignas(64) CacheAlignedstate {
    uint64_t value;
};
```

But alignment does not automatically improve performance. It can also:

- increase object size;
- create more cache pressure;
- waste memory:
- reduce the number of objects fitting in a page.

For ordinary data, natural alignment is usually sufficient. Explicit alignment is most
valuable for cache-line isolation, SIMD data, DMA requirements, or hardware-specific
interfaces.

### 7. Hot and cold data separation

A large class should not necessarily contain every field used by the system.

```cpp
struct HotConsumerState {
    uint32_t consumer_id;
    uint32_t tunnel_id;
    uint64_t packet_count;
};

struct ColdConsumerConfig {
    std::string name;
    std::string endpoint;
    std::vector<uint32_t> backup_routes;
};
```

The forwarding path can access only `HotConsumerstate`, while configuration and diagnostic
information remain elsewhere.

This is particularly useful for PVTB-style packet forwarding:

```cpp
struct TunnelHotstate {
    uint32_t gre_key;
    uint32_t consumer_index;
    uint64_t packets_forwarded;
};

struct Tunnelcoldstate {
    std::string domain;
    std::string protocol;
    std::vector<std::string> metadata;
};
```

The fast path should avoid loading strings, vectors, rarely used flags, and diagnostic
structures.

### 8. Virtual functions affect layout

A polymorphic class generally needs a virtual-table pointer:

``` cpp
struct Base {
    virtual ~Base() = default.
    virtual void process() = 0;
};
```

On a typical 64-bit implementation, the object commonly contains an additional
pointer-sized field. The exact layout is implementation-dependent.

Virtual dispatch can also affect performance because it may:

- require an indirect call;
- reduce inlining opportunities;
- make branch prediction more difficult;
- cause different derived objects to access different code paths.

This does not mean virtual functions should always be avoided. It means the hot path
should be measured. For fixed packet-processing types, alternatives can include:

- templates;
- `std::variant`;
- function tables;
- integer-based dispatch;
- specialized code paths.

Virtual *inheritance* is a heavier case still. Where ordinary (non-virtual) inheritance
simply stacks base subobjects - as the DDEv dump in section 9 shows, sometimes producing
several table pointers - virtual inheritance ensures a shared base appears only once in a
diamond hierarchy, and it pays for that with extra indirection:

```cpp
struct Base {
    int b;
    virtual ~Base() = default;
};

struct Left  : virtual Base { int l; };
struct Right : virtual Base { int r; };
struct Diamond : Left, Right {int d; };   // one shared Base, not two
```

To make the single shared `Base` reachable from both `Left` and `Right` at their differing
offsets, the compiler stores *virtual base offsets* (often threaded through the table, or
via a separate `vptr` on some ABIs such as MSVC's `vtordisp`). Consequences for layout and
the hot path:

- objects grow: beyond the shared base, each virtually-derived subobject carries the
  machinery to locate that base.
- Accessing a member of a virtual base may require an extra load ffetch the virtual-base
  offset, then apply it rather than a fixed compile-time offset.
- `static_cast`/`dynamic_cast` across a virtual base is no longer a constant pointer
  adjustment, and `reinterpret_cast` tricks that work for standard-layout types are invalid
  here.

Inspect a diamond with the record-layout dump before relying on its size or offsets.
Virtual inheritance is the right tool for genuine diamond hierarchies (and for interface
mix-ins that must share state), but on a measured hot path its indirection is rarely worth
it - prefer composition or the flatter dispatch alternatives listed above.

### 9. Useful layout inspection

Compile with warnings and inspect object layout:

```bash
clang++ -std=c++20 -O2 -Wall -Wextra -Wpadded layout.cpp
```

clang can dump record layouts:

```bash
clang++ -Xclang - fdump-record-layouts -fsyntax-only layout.cpp
# or via cc1 directly:
clang-cpp -cc1 -fdump-record-layouts layout.cpp
```

#### Worked example: inheritance, table pointers, and tail padding

Given these types (note the enum uses a 1-byte underlying type):

```cpp
enum EventId : unsigned char {
    EventIdsessionDeletion     = 0,
    EventIdBearerDeletion      = 1,
    EventIdsessionCreation     = 2,
    EventIdBearerCreation      = 3,
    EventIdBearerModification  = 4,
    EventIdBearerUpdate        = 5,
    EventIdsessionInfo         = 6,
    EventIdDataUsage           = 7,
    EventIdSessionSuspension   = 8,
};

class IBasicData {
public:
    virtual ~IBasicData() = default;
    virtual const char* tostring() const = 0;
};

// Ints and enums.
class Basicata : public IBasicData {
public:
    Basicata() : value_() {}
    EventId value() const { return value; }
    // return a pointer to a static string literal lives in program's
    // ready-only data (.rodata).
    const char* tostring() const override { return "EventID"; }
private:
    EventId value_;
};

class BaseEv {
public:
    BaseEv (bool mand) : name_("base"), mand_(mand) {}
    virtual const char* toStr() = 0;

private:
    const char* name_;
    const bool  valid{true};
    const bool  shadow{false};
    bool        mand_{false};
};

class DEV: public BaseEv {
public
    DDEV() : BaseEv(true) {}
    const char* tostr() override { return "a"; }
private:
    Basicata value_;
};
```

The dump reveals how the table pointer, base subobjects, and padding stack up
(`dsize` is the data size *without* tail padding):

```
*** Dumping AST Record Layout
   0 | class BasicData
   0 |   (IBasicata vtable pointer)
     | [sizeof=8, dsize-8, align=8, nvsize-8, nvalign=8]

*** Dumping AST Record Layout
   0 | class BasicData
   0 |   class IBasicata (primary base)
   0 |     (IBasicata vtable pointer)
   8 |   EventId value_
     | [sizeof=16, dsize-9, align=8, nvsize-9, nvalign=8]

*** Dumping AST Record Layout
   0 | class BaseEv
   0 |   (BaseEv vtable pointer)
   8 |   const char * name_
  16 |   const _Bool valid
  17 |   const _Bool shadow
  18 |   _Bool mand_
     | [sizeof=24, dsize=19, align=8, nvsize=19, nvalign=8]

*** Dumping AST Record Layout
   0 | class DDEV
   0 |   class BaseEv (primary base)
   0 |     (BaseEv vtable pointer)
   8 |     const char * name_
  16 |     const _Bool valid
  17 |     const _Bool shadow
  18 |     _Bool mand_
  24 |   class BasicData value_
  24 |     class IBasicdata (primary base)
  24 |       (IBasicata vtable pointer)
  32 |       EventId value_
     | [sizeof=40, dsize=40, align-8, nvsize-a, nvalign-8]
```

What to read out of this:

- Each polymorphic class carries an 8-byte vtable pointer at offset 0, so `IBasicData`
  alone is 8 bytes.
- `BasicData` has `dsize=9` (8-byte vtable pointer + 1-byte `EventId`) but `sizeof=16`:
  7 bytes of tail padding bring it up to its 8-byte alignment.
- That tail padding matters when `Basicata` becomes a member. In `DDEv`, `BaseEv`
  occupies offsets 0-18 (`dsize=19`, padded to 24 for alignment), and the `Basicata`
  member starts at offset 24, bringing its *own* vtable pointer. The result is two
  independent vtable pointers inside one `DDEv`, and `sizeof(DDEv) == 40`.

For generated code and cache behavior:

```bash
nepf stat -e cache-references,cache-misses ./program
perf record -e cache-misses ./program
perf report
```

You should compare realistic workloads rather than relying only on `sizeof`.

### 10. Bit-fields for dense protocol headers

when a layout must mirror a wire format or squeeze many small flags into a word,
bit-fields let you declare sub-byte members directly. This is common in packet and
protocol headers, which the AoS/SoA and hot/cold discussions above lean on:

```cpp
#include <cstdint>

// A simplified GRE-like flags word.
struct GreFlags {
    uint16_t checksum_present : 1;	
    uint16_t routing present  : 1;
    uint16_t key_present      : 1;
    uint16_t seq_present      : 1;
    uint16_t reservede        : 9;
    uint16_t version          : 3;
};

static_assert(sizeof(GreFlags) == 2);
```

Bit-fields pack several logical fields into the storage of their declared type, so the
whole structure above fits in two bytes instead of six separate `bool`/`uint8_t` tmembers.

Caveats that make bit-fields subtle, and that the standard leaves to the implementation:

- **Allocation order is implementation-defined.** Whether `checksum_present` lands in the
  most-significant or least-significant bit of the word depends on the compiler and target
  endianness. Code that must match a specific on-the-wire bit order cannot portably rely on
  declaration order; verify against the actual target, or do explicit shifting/masking when
  cross-platform wire compatibility is required.
- **Straddling is implementation-defined.** whether a bit-field may cross an underlying-type
  (or alignment) boundary, and how padding bits are inserted, varies. A declared type that
  is too small to hold the requested width, or a field that would straddle, can silently
  change the layout.
- **you cannot take a bit-field's address.** `&flags.version` is ill-formed, and a
  non-`const` reference to a bit-field is not allowed. only `const` references bound to a
  temporary copy work. This rules out pointer/iterator tricks over bit-fields.
- **A zero-width unnamed field forces the next field to the next allocation unit:**

```cpp
struct Split {
    uint32_t a : 4;
    uint32_t   : 0;   // force b' to start a fresh storage unit
    uint32_t b : 4;
};
```

- **Mixing signed/unsigned matters.** A `int x : 1`; holds values `{-1, 0}`, not `{0, 1}`;
  prefer unsigned underlying types for flags to avoid surprises.

Because of the implementation-defined bit ordering, bit-fields are best used for *internal*
compact state where you control both producer and consumer and compile with the same
toolchain/target. For strict wire formats shared across platforms, explicit masks and
shifts over a plain uint16_t /uint32_t are more portable, at the cost of verbosity. Use
the record-layout dump (section 9) to confirm the exact bit placement your compiler chose.

### 11. Packed structures: `#pragma pack` and `[[gnu::packed]]`

Section 2 reduced padding by *reordering* members, which keeps every member naturally
aligned. When you instead need to eliminate padding entirely - typically to overlay a
struct directly on a byte buffer - you can force packing:

```cpp
#pragma pack(push, 1)
struct WireHeader {
    uint8_t version;      // offset 0
    uint32_t id;          // offset 1 (NOT 4) -- misaligned!
    uint16_t length;      // offset 5
};
#pragma pack(pop)

static_assert(sizeof(WireHeader) == 7);   // no padding at all
```

The GCC/Clang attribute form is equivalent and more localized:

```cpp
struct [[gnu::packed]] WireHeader {
    uint&_t version;
    uint32_t id;
    uint16_t length;
};
```

Packing removes padding but introduces real hazards, and it should be treated as a
deliberate trade-off, not a default:

- **Misaligned member access.** `id` now lives at offset 1. On architectures that tolerate
  unaligned access (x86-64) this works but can be slower; on stricter architectures
  (some ARM configurations, SPARC) a misaligned access can fault or be emulated slowly.
  The compiler knows the member is packed and emits safe (often byte-wise) access code,
  which is precisely where the cost appears on hot paths.
- **Taking the address of a packed member is dangerous.** `&header.id` yields a pointer
  whose alignment does not match `uint32_t`'s requirement. Passing it where an aligned
  `uint32_t*` is expected is undefined behavior; dereferencing it directly in code that
  assumes alignment can fault. GCC/clang warn under `-Waddress-of-packed-member`.
- **It defeats the natural-alignment assumptions** that atomics, SIMD loads, and DMA
  engines rely on. Never place a `std::atomic` or a type with alignment requirements inside
  a packed struct and expect correct lock-free behavior.

Preferred alternatives, in order:

1. **Reorder members** (section 2) so the struct is compact *without* packing - this keeps
   natural alignment and is free.
2. **Serialize explicitly** with reads/writes into a byte buffer ( `memcpy` the fields,
   convert endianness deliberately). This is the most portable approach for wire formats
   and avoids all the alignment UB.
3. **Pack only as a last resort**, when you must overlay a fixed external layout and have
   measured that explicit serialization is too costly - and then access members through
   `memcpy` into a properly aligned local rather than by taking their address:

```cpp
uint32_t id;
std::memcpy(&id, buffer + 1, sizeof id);   // safe, alignment-agnostic
```
As with bit-fields, confirm the result with the record-layout dump and, for wire formats,
pin and document endianness explicitly.

### 12. Cache coherence: how a line "refreshes" between cores

Sections 1 and 5 describe *what* happens - a line is fetched in 64-byte blocks, and a
shared line "moves between cores" under false sharing. This section explains the *why*: the
cache-coherence protocol that keeps every core's private cache consistent, and the traffic
it generates.

##### The Line's states (MEST)

Each cache line in a core's private cache carries a coherence state. The classic model is
**MESI** (Intel adds an Owned state, giving MOESI on some AMD/ARM designs):

- **Modified (M):** this core holds the only copy, and it is dirty (newer than memory).
  Memory must be written back before anyone else can read it.
- **Exclusive (E):** this core holds the only copy, and it is clean (matches memory). It
  can be written without talking to anyone, transitioning to M.
- **shared (s):** multiple cores may hold clean copies. Reads are free; a write requires
  invalidating the other copies first.
- **Invalid (I):** the line is not valid here; an access misses and must fetch it.

#### What a write actually costs: Read-For-Ownership

A core cannot simply write to a line it holds in ****. It must first perform a
**Read-For-Ownership (RFO) **: broadcast (or directory-route) a request that says "I am
about to write this line," which forces every other core holding the line to transition it
to **Invalid**. Only once the writing core has the line in **E/M** can the store complete.

That is the real cost of false sharing in section 5:

```
Core A: store producer_count  --> RFO --> Core B's copy of the line -> Invalid
Core B: store consumer_count  --> RFO --> Core A's copy of the line -> Invalid
Core A: store producer_count  --> miss (line was invalidated) --> RFO again ...
```

Even though A and B touch *different* variables, they live on the same line, so each write
"steals" the line from the other core. The line ping-pongs: every store becomes a coherence
miss costing tens to hundreds of cycles (an L3 or cross-socket round trip), instead of the
~1-cycle L1 hit it would be if the two variables sat on separate lines. This is why the
`alignas(destructive_interference_size)` separation in sections 5-6 can turn a scaling
collapse back into near-linear scaling - it is not about size, it is about giving each core
a line it can keep in **E/M** without contention.

#### Invalidation, snooping, and write-back

Coherence requests travel over the interconnect. On smaller systems cores **snoop** a shared
bus; on larger systems a **directory** (often in the L3 slice or home agent) tracks which
cores hold each line and sends targeted invalidations. Either way, the relevant traffic is:

- **Invalidation messages** when a core gains ownership to write.
- **write-back** of a **Modified** line to L3/memory when it is evicted or when another core
  needs to read it.
- **Cache-to-cache transfer**, where a dirty line is forwarded directly from the owning
  core's cache to the requester (faster than going through memory, but still far slower than
  a local hit).

#### Eviction and replacement

A line also leaves a cache involuntarily. Caches are **set-associative**: an address maps to
one *set*, and that set holds a fixed number of *ways* (eg. 8). When a set is full and a new
line maps to it, the replacement policy (approximately LRU) **evicts** a resident line -
writing it back first if it is **Modified**. This is the mechanism behind *capacity misses*
(working set larger than the cache) and *conflict misses* (too many hot addresses mapping to
the same set - see section 13 on associativity. "Refreshing" a cache line, then, is really
one of: a coherence transition (I→S/E/M via RFO or a read), or a physical eviction-and-refill
when the set runs out of room.

#### why this drives the layout advice

Every technique earlier in this note is ultimately about controlling these transitions:

- **Compact objects/ SOA2,4):** fewer lines touched peroperation fewer misses and
  less eviction pressure.
- **Hot/cold split (7):** keep the frequently-written working set small enough to stay
  resident in **M/E** without eviction.
- **False-sharing isolation (5, 6):**  stop unrelated writes from triggering each other's
  RFO invalidations.

You can watch the mechanism directly with hardware counters:

```bash
# Coherence / sharing hotspots (names vary by CPU; list with "perf list"):
perf stat -e cache-misses, LLC-load-misses, LLC-store-misses ./program
# Intel: HITM = a load that hit a *modified* line in another core = false/true sharing.
perf c2c record •/program && perf c2c report
```

`perf c2c` ("cach-to-cache") is purpose-built to find lines bouncing between cores and
will point you at the exact of offending structure and offset.

### 13. Actively using the cache to optimize

The sections above are mostly about *Layout*. This section covers the *access-pattern* and
*active* techniques that, combined with good layout, extract the most from the cache.

#### Name the two locality principles

Caches pay off because real programs exhibit:

- **Temporal locality:** data used now is likely to be used again soon. Keep such data
  small and reuse it before it is evicted.
- **Spatial locality:** data near recently-used data is likely to be used soon. This is why
  64-byte fetches and the hardware prefetcher help and why So and compact structs win.

Most of the earlier advice is just these two principles applied to layout.

#### Access pattern: sequential beats random

The hardware **prefetcher** detects regular strides (especially linear, forward-sequential)
and fetches lines ahead of demand, hiding memory latency. Pointer-chasing (linked lists,
node-based trees, hash tables with chaining) defeats it: each `next` pointer is a dependent
load to an unpredictable address, so the CPU stalls on every hop.

```cpp
// Prefetcher-friendly: linear stride, contiguous storage.
long sum = 0;
for (const auto& p: packets) sum += p.length;    // predictable, streamed

// Prefetcher-hostile: dependent loads to scattered addresses.
for (Node* n = head; n; n = n->next) sum += n->length;
```

Practical consequences: prefer contiguous containers (`std::vector`, `std::array`) over
node-based ones (`std::list`, `std::map`) on hot paths; store indices into a flat array
instead of chasing pointers; and traverse multidimensional arrays in memory order
(row-major: innermost loop over the last index) so successive accesses walk one line at a
time rather than jumping a full row per step.

#### Software prefetching

when the access pattern is predictable *to you* but not to the hardware prefetcher (for
example, you know the next node's address a few iterations early), you can issue an explicit
hint:

```cpp
for (std::size_t i = 0; i < n; ti) {
    // Bring data needed a few iterations ahead into cache now.
    _builtin prefetch(&data[i + 8], /*rw=*/o, /*locality=*/3);
    process (data[i]);
}
```

`__builtin_prefetch` is the portable choice: GCC and clang support it on both x86 and
AArch64, lowering to `prefetcht0` /etc. on x86 and `prfm` on ARM. If you prefer the
architecture-specific intrinsics:

```cpp
// x86
#include <xmmintrin.h>
_mm prefetch(reinterpret_cast<const char*>(&data[i + 8]), _MM_HINT_T0):

// ARM / AArch64 -- __builtin _prefetch is still usually preferred; the PREM
// instruction can also be emitted directly:
//   asm volatile("prfm pldl1keep, [%0]" :: "r"(&data[i + 8]));
__builtin_prefetch(&data[i + 8], 0, 3);   // portable form, works on ARM too
```

The `locality` argument (0-3) hints which cache level to target; `rw` hints read vs. write.
Prefetching is a double-edged tool: a well-tuned distance (far enough ahead to hide latency,
not so far the line is evicted before use) can be a large win, but mistimed or excessive
prefetches waste bandwidth and can evict useful data. Always measure; never sprinkle it in
blindly.

#### Blocking (tiling) to fit the working set

when an algorithm reuses data (eg. matrix multiply), process it in blocks sized to stay
resident in cache, so each loaded line is reused many times before eviction:

```cpp
// Naive: the inner traversal of B strides across memory and evicts reused rows of A.
for (int i = 0; i < N; ++i)
  for (int j = 0; j < N; ++j)
    for (int k = 0; k < N; ++k)
      C[i][j] += A[i][k] * B[k][j];

// Blocked: operate on BS BS tiles that fit in cache; each tile is reused in place.
constexpr int BS = 64;   // tune to L1/L2 size
for （int ii = es ii < N; ii += BS）
  for （int jj = 0; jj < N; jj += BS）
    for (int kk = 0; kk < N; kk += BS)
      for (int i = ii; i < ii + BS; ++i)
        for (int j = jj; j < jj + BS; ++j)
          for (int k = kk; k < kk + BS; ++k)
            c[i][i] += A[i][k] * B[k][j];
```

The transformation does not change the arithmetic; it changes the *order* so the working
set per phase fits in cache, converting capacity misses into hits. Block size is tuned to
the target cache level.

#### Set associativity and conflict misses

As section 12 noted, caches are set-associative: an address maps to one set via some of its
middle bits. If many hot addresses share those bits, they collide in the same set and evict
each other even though the cache as a whole is nearly empty - a **conflict miss**. The
classic trigger is a power-of-two stride:

```cpp
// If 'stride' is a large power of two, every a[i*stride] can map to the same set,
// thrashing a handful of ways while the rest of the cache sits idle.
for (std::size t i = 0; i < count; ++i)
    total += a[i * stride];
```

Mitigations: avoid power-of-two array dimensions for hot 2D arrays (pad a row to, say,
`width + 1`); offset parallel arrays so they do not all land in the same set; and be wary of
over-aligning many objects to the same boundary (section 6), which can itself concentrate
them into one set. This is the subtle counterpart to false sharing - there the problem is
too much sharing of one line; here it is too many *distinct* lines competing for one set.

#### Non-temporal (streaming) stores

Some writes are "write once, do not read back soon" - initializing a large buffer, copying
a frame that will be DA'd out. Normal stores pull the line into cache (an RFO) and pollute
it with data you will not reuse, evicting data you *will*. Non-temporal stores bypass the

cache and write straight toward memory:

```cpp
// x86
#include <emmintrin.h>

void fill_streaming(int* dst, int value, std::size_t n) {
    __m128i v = _mm_set1_epi32(value);
    for (std::size_t i = 0; i < n; i += 4)
        _mm_stream_si128(reinterpret_cast<__m128i*>(dst + i), v);  // NT store, no RFO
    _mm_sfence();   // NT stores are weakly ordered; fence before others observe them.
}
```

The ARM/AArch64 equivalent uses the non-temporal store-pair instruction `STNP`. There is no
standard intrinsic, so it is emitted via inline assembly (or left to the compiler/`mempy`,
which may generate it for large copies):

```cpp
// ARM / AArch64
void fill_streaming(long long* dst, long long value, std::size_t n) {
    for （std::size_t i = 0; i < n; i += 2）{
        // STNP writes a pair with a non-temporal hint (bypasses cache allocation).
        asm volatile("stnp %1, %1, [%]"
                     : "r"(dst + i), "r"(value)
                     : "memory");
    }
    asm volatile("dmb ishst" ::: "memory"); // store barrier, analogous to sfence
}
```

Both forms assume the destination is suitably aligned. If you would rather not hand-write
assembly, a plain loop or `std::memcpy`/`std::fill` over a large, obviously-non-reused
buffer often lets the compiler pick streaming stores itself under `-O2/-O3` - check the
generated code before committing to intrinsics.

Use these only for genuinely non-reused, large, sequential writes; for data you will read
back soon they are a pessimization. Note the required store fence - non-temporal stores do
not follow normal ordering guarantees.

#### Measure, always

Every technique here can backfire if applied without evidence. Confirm with counters and a
realistic workload:

```bash
perf stat -e cache-references,cache-misses,L1-dcache-load-misses,LLC-load-misses ./program
perf record -e cache-misses ./program && perf report   # find the hot miss sites
```

# find the hot miss sites

Optimize the layout and access pattern first (they are free and portable); reach for
prefetch intrinsics and non-temporal stores last, and only where a profile proves memory
stalls dominate.

### Practical checklist

when designing a performance-sensitive class, inspect:

- `sizeof` and `alignof`;
- member offsets (via the record-layout dump for non-standard-layout types, since
  `offsetof` is only well-defined for standard-layout ones);
- padding between members, and whether reordering removes it without packing;
- number of objects per cache line;
- whether hot fields are mixed with cold fields;
- whether different threads update fields on the same cache line, and whether
  `hardware_destructive_interference_size` (or a pinned, documented constant) separates
  them;
- whether a virtual pointer, virtual base indirection, or other indirection is necessary;
- whether an array-of-structures or structure-of-arrays layout fits the access pattern;
- whether the access pattern is sequential (prefetcher-friendly) or pointer-chasing, and
  whether blocking/tiling would keep the working set resident (section 13);
- whether power-of-two strides or over-alignment are causing conflict misses in one set;
- for external/wire layouts: whether bit-fields or packing are truly required, and whether
  bit ordering and endianness are pinned and documented;
- cache misses, coherence/HITM events ("perf c2c"), and throughput in a benchmark.
 
The important distinction is:

> Smaller objects improve density and locality, while carefully separated objects reduce
>contention and false sharing.

Those goals can conflict, so the correct layout depends on whether the workload is primarily single-threaded scanning, random lookup, or multi-core concurrent updates.
