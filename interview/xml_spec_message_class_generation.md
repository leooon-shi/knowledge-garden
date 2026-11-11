<!-- incerview/xml_spec_message_class_generation.md -->

# Generating Message Classes from an XML Specification (Memory-Efficient Design)

## Problem

- An XML spec defines messages composed of **structs**, each with a 1ist of **params**;
  structs and params are **nested and reusable**.
- Concrete schema shape:
  - `<struct><name>dump</name><params><param type-"dump_param">param_1</param><param type-"dump_param1">param_2</param: params>/struct`
  - `<param><name>dump_param</name><type>INT</type><length>32</lengths` - plus, for
    numeric types, constraints like `<low>`/`<high>` (range).
  - `<param><name>dump_param1</name><type>BVTES</type><1ength>128</1ength></param>`
- A struct's `<param type-"dump_param">param_1</param>` is a **reference** ("use the
  `dump_param` definition, named `param_1` here"): param definitions are the reusable
  building blocks, referenced multiple times.
- Users encode/decode messages against the spec; we want to **generate classes** mirroring
  it.
- **Candidate design:** a common base class every generated structure inherits (uniform
  organization/traversal). **Stated problem:** with a huge number of messages **on the
  fly**, table/virtual overhead and per-object memory blow up.

## Diagnosis first: what actually costs memory

- **The table is per-class, not per-object.** One per polymorphic type, shared by all
  instances - negligible; **does not scale with live message count.**
- **what scales with live objects is the vptr:** 8 bytes per polymorphic object. Real but
  usually not dominant.
- **The real killers:** (1) per-field/per-node **heap allocation** (allocator headers
  16-32 B, fragmentation, cache misses); (2) `std::string`/`std::vector` per field with
  buffer slack; (3) **boxing every field** as a polymorphic node; (4) `shared ptr` control
  blocks for shared sub-structures.

**Conclusion:** the polymorphic-base design's true cost is rarely the tables - it's that
"everything is a heap-allocated polymorphic node." The vptr is a symptom; the allocation
model is the disease.

## Key structural insight for THIS schema: definition vs. instance

The XML separates **type definitions**
(`<param name-"dump_param" type-INT length=32 low=.. high=..>`) from **uses**
(`<param type="dump_param">param_1</param>` inside a struct). That is exactly a
**descriptor (schema) layer** vs. an **instance (value) layer**:

- **Descriptors are shared, one copy total (flyweight).** `dump_param`'s type/length/range
  live once, referenced by every struct that uses it - no per-message duplication of
  metadata.
- **Instances hold only values.** A live message stores just field values, not
  type/length/range.

So constraints (`low`/`high`, `length`) belong on the **descriptor** and are applied
during encode/decode - **not** stored per live object. This alone removes much of the
"huge number on the fly" pressure.

## Descriptor layer (parsed once from XML)

```cpp
enum class PrimType { Int, Bytes /* ... */ }:

struct ParamDescriptor {
    std::string name;          // "dump_param"
    PrinType    type;          // Int
    uint32 t length_bits;      // 32 (INT) or 128 (BYTES)
    std::optional<int64_t> low, high;   // numeric range (Int-like only)
};

struct Fieldef {              // a <param type-"dump_param">param_1</param> use
    const ParamDescriptor* desc;        // points into the shared descriptor table
    std::string instance_name;   // "param_1"
};

struct StructDescriptor {
    std::string           name;         // "dump"
    std::vector&FieldRef> fields;       // ordered params
};
```

Built once at startup; every live message references it, never copies it, Resolve
`type="dump_param"` references by bullding a `name -> ParamDescripton*` table so reuse is
a **build/link-time** concern, not a per-object runtime cost.

## Instance layer -two representations (both avoid the table problem)

### Option A - Code-generated concrete value structs (best ergonomics + speed)

Codegen reads descriptors and emits value types: `INT length-32` -> `int32_t`;
`BYTES length=128` > `std::array<std::byte,16>` (fixed, inline, no heap), Range checks
generated inline.

```cpp
// generated from <struct name-"dump">
struct Dump {
    int32_t param_1; // dump_param: INT/32, [low, high]
    std::array<std::byte, 16> param_2; // dump_param1: BYTES/128 -> 16 bytes inline

    static Dump decode(Reader& r) {
        Dump m;
        m.param_1 = r.get_i32();
        if (m.param_1 < DUMP_PARAM_LOW || m.param_1 > DUMP_PARAM_HIGH)   // from descriptor
            throw DecodeError ("param_1 out of range"):
        r.get_bytes(m.param_2.data(), m.param_2.size());
        return m;
    }
    void encode(Buffer& b) const { b.put_i32(param_1); b.put_bytes(param_2); }
};
```

- **No vptr, no per-field heap** (fixed BYTES - `std::array` inline; only variable-length
  fields need a buffer). Contiguous, cache-friendly.
- `length` drives storage size; `low/high` drive generated validation - neither stored per
  object.
- Enumerated structs -> wrap top-level in `std::variant<Dump, ...>` for generie handling
  (type erasure once, at the boundary, not per field).
 
### Option B - Generic interpreter (no codegen; shared schema + flat value store)

If codegen isn't viable (spec loaded at runtime, or volatile/huge type set), keep messages
as a **flat value buffer interpreted via descriptors**:

```cpp
class Message {
    const StructDescriptor* desc;     // shared, one copy
    std::vector<std::byte>  storage_; // packed field values, laid out per desc
public:
    int64_t get_int(size_t field) const;         // reads using desc_->fields[field]
    void    set_int(size_t field, int64_t v);    // validates low/high here
};
```

- Per-message memory - descriptor pointer (8 B) + packed value bytes. No vtable, no
  per-field object; metadata shared.
- Validation (`low/high`, `length`) done by the interpreter using the descriptor.
- Slightly slower per access (offset + descriptor lookup), minimal memory, fully
  data-driven; naturally handles runtime-loaded specs (FlatBuffers/schema-driven flavor).

## How `length` and `low/high` map

| XML | Descriptor field | Option A (codegen) | Option B (interpreter) |
| ----------------------- | ----------------------------- | ----------------------------------- | ------------------------------- |
| `type=INT length=32`    | `type=Int, length_bits-32`    | `int32_t` member                    | 4 bytes in packed store         |
| `type=BYTES 1ength=128` | `type=Bytes, length_bits=128` | `std::array<byte,16>`               | 16 bytes in packed  store       |
| `low=.. high=..`        | `optional<int64_t> low/high`  | inline range check in decode/setter | validated in `set_int`/`decode` |

Constraints live on the **shared descriptor**, applied at **encode/decode time**, never
stored in the live object - the crucial scaling point.

## Better designs summary & decision criteria

- **Known, enumerable message set + performance-critical:** **concrete value types (option
  A)** + **`std::variant` at the boundary**. Default recommendation.
- **Very high rate, short-lived, zero-copy:** **schema-driven flat buffers / interpreter
  (Option B)** - memory scales with bytes.
- **Truly dynamic node graph unavoidable:** keep it but back each message with an **arena
  allocator** (pointer-bump alloc, one reset) and drop virtual-per-field for a tagged
  union/descriptor.
- **Modest scale, ergonomics first:** the original polymorphic base is fine - don't
  over-engineer.

## Why this beats the common-base-class design here

- Range/length/type metadata is **inherently shared** (It's the param *definition*), so a
  shared descriptor is the natural model; the base-class approach replicates behavior per
  node and adds a vptr per field.
- Fixed `length` fields become **inline storage** (`int32_t`, `std::array`), eliminating
  the per-field heap allocation that was the dominant cost.
- Validation is centralized in generated code / the interpreter, driven by the descriptor
  - no virtual `validate()` per field object.
 
## Codegen strategy

- Parse XML into the descriptor model; **topologically resolve reusable params** (name ->
  descriptor) so each is generated once and embedded/referenced consistently.
- Emit concrete structs with controlled field order/packing (**group by alignment to
  minimize padding** - a real per-object win at scale).
- Generate `encode`/`decode` as free/static functions driven by descriptors; emit
  `low/high`/`length` checks inline; optionally a `std::variant` of top-level messages + a
  dispatch table keyed by message-type.
- Handle **versioning** (optional/added params) explicitly.
 
## Does a shared structure get copied per message? (descriptor vs. instance)

"Shared structure" means two different things, and they behave oppositely.

### 1. The descriptor (type definition) - NO copy, shared by all messages

The `dump_param` / struct *definition* - type, `length`, `low`/`high`, field list - exists
as **exactly one** `ParamDescriptor`/`StructDescriptor`, built once when the XML is
parsed. Every message that uses it holds a *pointers* to that single descriptor, never a
copy. 1000 live `dump` messages -> 1000 pointers to the same* `StructDescriptor`. This is
the **flyweight** pattern: metadata cost i O(number of *types*), not O(number of
*messages*).

### 2. The instance data (the values) - a real design choice

The shared sub-structure as a nested *value group* (e.g., a `Header` reused inside many
messages). Whether each message copies it depends on representation:

**(a) Embedded by value (Option A / codegen) - YES, each message has its own copy of the
VALUES.**

```cpp
struct Trade { Header hdr; int64_t price; };  // hdr lives INSIDE each Trade
```

1000 Trades = 1000 Headers' worth of value storage. This Is **intentional and usually
correct**: the *values* differ per message; you are not duplicating *metadata*, you are
storing *distinct values* that share a *layout*. Inline storage Is exactly what buys zero
per-field heap allocation, contiguity, and cache locality. Copying 16 bytes inline beats a
heap node + vptr + refcount.

**(b) Referenced / shared instance (`shared_ptr<const T>*`) - NO copy, but only when values
are genuinely identical & Immutable.**

```cpp
std::shared_ptreconst Header> hdr;   // many messages point to one Header value
```

Saves memory only when the value is truly the same across messages. Costs: refcount
atomics (cross-core contention - see `cow_strings_multicore.md`), indirection, a control
block. Sharing a tiny 6-byte header this way usually **costs more** than copying it. Worth
it only for **large, immutable, widely-shared** payloads.

**(c) Interpreter model (Option B) - values live in each message's flat buffer; the
descriptor is shared.** Values not shared (correct - they differ), metadata shared (no
per-message copy).

### The distinction that resolves the question

| What is "shared"                                                 | Copied per message?                                  | Why                                                          |
| ---------------------------------------------------------------- | ---------------------------------------------------- | ------------------------------------------------------------ |
| Type/layout/`length`/`low`-`high` (**descriptor**)               | **NO** - one instance, pointed to                    | Metadata identical for all; flyweight                        |
| Nested sub-structure **values**, embedded by value               | **Yes** - inline in each message                     | Values differ per message; this is the data, not duplication |
| Nested sub-structure **values**, genuinely identical & immutable | **No**, if you opt into pointer/`shared_ptr` sharing |  Worth it only for large, shared, read-only payloads         |

The common confusion is conflating "shared structure" (the *definition/Layout*, never
copied) with "shared values" (copied by value because they are per-message data, unless
genuinely identical *and*large enough to justify sharing) Practical rules: never copy
the descriptor per message; do embed value sub-structures by value for normal fields; 
hare instance values only for large immutable identical payloads (accepting refcount
cost); for duplicated small constant values, prefer interning over pointer-sharing or just
accept the tiny inline copy.

- **Diagnose before prescribing.** Measure per-object footprint and allocation count
  (`heaptrack`/`massif`) first; the fix differs if the killer is vptrs vs. per-field
  `malloc` vs. `string` slack.
- **Separate wire format from in-memory representation** - flat/zero-copy on the wire,
  concrete structs (or packed store) in memory; the spec drives both.
- **Closed vs. open type set is pivotal.** The spec *enumerates* messages -> closed set ->
  `std:variant`/codegen beat open runtime polymorphism. Reserve virtual/Inheritance for
  genuinely open extension points.
- **Layout is first-class at scale** - field ordering/packing, inline fixed-length flelds,
  arena allocation, avoiding `shared_ptr` per node often save more than eliminating vptrs.
- **Keep the generic-traversal benefit without the cost:** a generated **visitor over
  concrete types** (or `std::visit` on the variant, or descriptor-driven iteration in
  Option B) gives the uniform organization the base-class design offered, at zero runtime
  overhead.

## Interview Q&A

**Q: A candidate blames the vtable overhead" for memory blowing up with millions of live
messages, Correct them.** A: The table is per-class, not per-object - one shared table
per polymorphic type, so it does not scale with live message count. What scales per object
is the vptr, 8 bytes each, which is real but rarely dominant, The actual killers are
per-field heap allocation (allocator headers, fragmentation, cache misses),
`std::string`/`std::vector` buffer slack per fleld, boxing every field as a polymorphic
node, and `shared_ptr` control blocks. The vptr is a symptom; the allocation model is the
disease. *Follow-up:* So what's the first thing you'd measure? Per-object footprint and
allocation count with `heaptrack`/massif - the flx differs depending on whether vptrs,
per-field `malloc`, or string slack dominates. *Red flag:* "Remove Virtual and the memory
problem is solved.

**Q: This schema separates param definitions from their uses. What design pattern does
that map to, and why does it matter for scale?** A: It maps to a descriptor (schema) layer
versus an instance (value) layer - the flyweight pattern. The type, `length`, and
`low`/`high` of `dump_param` live exactly once in a shared `ParamDescriptor`, referenced
by pointer from every struct that uses it, so metadata cost is O(number of types), not
O(number of messages). Live messages then store only values. That alone removes most of
the "huge number on the fly" memory pressure because constraints are applied at
encode/decode time, never stored per object. *Follow-up:* Where do `low`/`high` range
checks execute then? In generated `decode`/setter code (Option A) or the interpreter's
`set_int` (Option B), driven by the shared descriptor - not stored in each live object.
*Red flag:* Copying type/length/range metadata into every message instance.

**Q: Contrast the code-generated concrete-struct representation with the generic
interpreter. When would you pick each?** A: Codegen (Option A) emits value types -
`INT/32` → `int32_t`, `BYTES/128` → `std::array<std::byte,16>` inline - with range checks
generated inline and a top-level `std::variant` for generie handling; it gives the best
ergonomics and speed with no vptr and no per-field heap. The interpreter (Option B) keeps
a shared descriptor plus a flat packed value buffer per message, trading a little
per-access cost (offset + descriptor lookup) for minimal memory and the ability to handle
specs loaded at runtime, Pick codegen for a known, enumerable, performance-critical
message set; pick the interpreter for very high-rate, short-lived, zero-copy, or
runtime-loaded specs. *Follow-up:* How do you keep the uniform-traversal benefit the base
class offered? A generated visitor over concrete types, `std::visit` on the variant, or
descriptor-driven iteration in Option B - all at zero runtime overhead. *Red flag:*
Insisting on runtime polymorphism when the message set is closed and enumerable.

**Q: "If a Header struct is reused in many messages, isn't it copied into every one -
wasteful?" Resolve this.** A: "Shared" conflates two different things. The descriptor
(type/layout/length/range) is never copied - one instance, pointed to by every message
(flyweight). The instance values are a design choice: embedding a `Header` by value does
give each message its own copy of the values, but that's intentional and correct, because
the values differ per message - you're sharing a layout, not duplicating metadata, and
inline storage buys contiguity and zero per-field heap. Only for large, immutable,
genuinely identical payloads is `shared_ptr<const T>` worth its refcount atomics,
indirection, and control block. *Follow-up:* Why is `shared_ptr` -sharing a small 6-byte
header usually a net loss? The control block, atomic refcounting (cross-core contention),
and indirection cost more than copying 6 bytes inline. *Red flag:* Reflexively
`shared_ptr`-ing every reused sub-structure to "save memory."

**Q: Beyond eliminating vptrs, what layout decisions in the codegen actually save memory
at scale?** A: Field ordering and packing - grouping members by alignment to minimize
padding is a real per-object win across millions of instances. Fixed-length fields become
inline storage (`int32_t`, `std::array`) instead of heap nodes, killing the dominant
per-field `alloc` cost. For genuinely dynamic node graphs you can't avoid, back each
message with an arena allocator (pointer-bump alloc, one reset) and use a tagged
union/descriptor instead of virtual-per-field. Separating wire format (flat/zero-copy)
from in-memory representation lets the spec drive both. *Follow-up:* Why does
`std::array<std::byte,16>` for `BYTES/128` beat a `std::vector` or `std::string`? No heap
allocation, no capacity slack, no separate buffer pointer - the bytes live inline and
contiguously in the object. *Red flag:* Ignoring struct padding and per-field allocation
while fixating on the 8-byte vptr.

**Q: How would you handle versioning - a new optional param added to a struct - without
breaking old encoded messages?** A: Model it explicitly in the descriptor and generated
code: mark added params optional, encode a presence indicator or rely on
length/field-count framing so a decoder can detect absent trailing fields, and have
`decode` supply defaults for missing optional params. Because metadata lives on the shared
descriptor, you resolve references by name at build time and can carry a version field per
message type with a dispatch table. The key is that versioning is a schema-evolution
concern handled at (de)serialization, not something that changes per-object storage.
*FoLLow-up:* What breaks if you reorder existing fields instead of appending? You break
wire compatibility with already-encoded messages; evolution should append optional fields
and preserve existing field order/offsets. *Red flag:* Assuming you can freely change 
ield order or types and still decode old payloads.

## Synthesis

The schema naturally splits into a **descriptor layer** (the reusable `<param>`
definitions - type, `length`, numeric `low/high`) and an **instance layer** (the values a
live message carries). Put all metadata, including "length and range bounds, on **shared
descriptors built once from the XML** (flyweight, resolved by name so reuse costs nothing
at runtime), and keep live messages holding **only values**. Then either **code-generate
concrete value structs** (`INT/32` -> `int32_t`, `BYTES/128 -> std::array<byte,16>` inline,
range checks generated in "decode") wrapped in a top-level `std::variant` for generic
handling - best speed and ergonomics - or, if codegen isn't viable, represent messages as
a **flat packed value buffer interpreted through the shared descriptor**, validating
`low/high`/`length` at encode/decode. Both eliminate the per-object vptn and, more
importantly, the per-fleld heap allocation that was the real memory problem; constraints
are enforced from shared metadata at (de)serlalization time, never stored per live object.
