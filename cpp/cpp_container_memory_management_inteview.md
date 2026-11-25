<!-- cpp/cpp_container_memory_management_interview.md -->

# How C++ standard containers manage their elements – interview Q&A

Focus: **where element storage lives (stack, heap, or inline array), how many allocations
happen, and how the control block relates to the element block.** This complements
`cpp_data_structure_interview.md`, which covers the data structures themselves; here the
lens is strictly *memory management of the elements*.

## 1. The core mental model

### Q1. When you declare a container as a local variable, what is on the stack and what is on the heap?

Think of every container as **two separate pieces**:

1. **The control block** – the container object itself: a handful of bookkeeping fields
   (pointers, size, capacity, a root pointer, etc.). It lives **wherever the object is
   declared**: the stack for a local, inside the enclosing object for a member, static
   storage for a global, the heap if the container itself was `new`-ed.
2. **The element storage** – for most containers a **separate heap allocation** (sometimes
   several), reached through a pointer in the control block.

So for `std::vector<int> v;` declared locally: the three control fields are on the stack;
the `int` elements are in one heap block.

### 02. What does `sizeof(container)` actually measure?

Only the **control block**, never the managed data. `sizeof(std::vector<int>)` is ~24
bytes on a typical 64-bit library whether the vector holds or or1,000,000 elements. The
exception is `std::array` whose control block *is* the elements.

### Q3. Which single container stores its elements inline, with no heap allocation?

`std::array<T, N>`. The elements are members of the object, so a local `std::array` puts
everything on the stack (or in whatever storage the object occupies). This is also why a
huge `std::array` local can overflow the stack.

## 2. `std::array` – inline, no allocation

### Q4. Where do the elements of `std::array<T, N>` live?

Inside the object itself. No dynamic allocation ever occurs. A local is fully on the
stack; a member is inside the enclosing object; a global is in static storage.

### Q5. Why can `std::array<int, 100000>` as a local crash the program while `std::vector<int>` of the same size does not?

The `array` version tries to place 400 KB directly on the stack, which commonly exceeds
the stack limit (often 1–8 MB, but frames are small by convention). The `vector` places
only ~24 bytes on the stack and the 400 KB on the heap.

### Q6. What is the cost you accept for `std::array`'s zero-allocation guarantee?

The size is fixed at compile time and cannot grow, and large instances consume whatever
storage region the object lives in (frequently the stack). You trade flexibility for the
absence of heap traffic.

## 3. `std::vector` – one contiguous heap block

### Q7. How does `std::vector` manage its elements?

A single **contiguous heap block**. The control block holds three pointers/sizes (begin,
end-of-size, end-of-capacity). Elements are constructed in place within that block.

### Q8. What happens to memory when the vector outgrows its capacity?

1. Allocate a new, larger block (geometric growth, commonly 1.5× or 2×).
2. Move (or copy) existing elements into it.
3. Construct the new element.
4. Destroy the old elements.
5. Free the old block.

This is why `push_back` is *amortized* O(1) but individual calls can be O(n), and why
reallocation invalidates all iterators, pointers, and references.

### Q9. Why does moving a vector touch no heap, but copying must allocate?

Move transfers the three control fields and nulls the source – the existing heap block is
reused in place. Copy must allocate a fresh block and copy-construct every element into
it.

### Q10. Does `clear()` free the heap block?

No. `clear()` destroys the elements but keeps the capacity. To release memory you need
`shrink_to_fit()` (non-binding) or the swap-with-empty idiom (`std::vector<T>().swap(v)`).

### Q11. Why is `std::vector<bool>` special in its storage?

It is typically a **packed bit representation** (one bit per element), so it does not
store `bool` objects at all. `operator[]` returns a proxy, not a `bool&`. It saves memory
but breaks the "contiguous array of T" model.

## 4. `std::string` – the inline "mini-heap" (SSO)

> **Terminology note:** "mini-heap" here means an **inline buffer / inline storage inside
> the string object** – a small private pool of bytes carried by the object. It is *not* a
> "min-heap" (the priority-queue data structure), and it is not the process heap; the
> whole point of SSO is to **avoid** a heap allocation for short strings.

### Q12. How does `std::string` manage its characters?

Two modes:

- **Short string optimization (SSO):** short strings live **inside the string object
  itself** in a small inline buffer – no heap allocation. This is the "mini-heap inside
  the object" (inline buffer / inline storage inside the string object).
- **Long strings:** one contiguous **heap** block, like a vector of chars.
 
### Q13. Is SSO required by the standard?

No. It is a universal implementation technique but not mandated. The threshold is
implementation-defined - commonly around 15 bytes (libstdc++) or 22 bytes (libc++) before
switching to the heap.

### Q14. Why is `sizeof(std::string)` larger than you might expect (often 24–32 bytes)?

Because the object must be large enough to hold the SSO buffer plus the size/capacity
bookkeeping. The library spends object size to buy the common case of zero allocation for
short strings.

### Q15. What happens to the SSO buffer when a string grows past the threshold?

The implementation allocates a heap block, copies the characters out of the inline buffer,
and switches to heap mode. This is a one-way transition for that content (it won't move
back inline on shrink unless the buffer is reset).

## 5. `std::deque` – two levels of heap allocation

### Q16. How does `std::deque` manage its elements?

Segmented storage with **two levels of heap allocation**:

1. A **map** – a heap-allocated array of pointers.
2. Multiple **fixed-size chunks** – each a separate heap block holding several elements;
   the map points at them.

Elements are therefore never one contiguous block.

### Q17. Why can `deque` grow at both ends cheaply without moving existing elements?

New chunks are allocated and their pointers added to the front or back of the map.
Existing chunks and the elements in them are not moved, so references to existing elements
survive push-front/push-back (unlike vector reallocation).

### Q18. Why is deque random access slower than vector random access?

Indexing requires locating the chunk via the map, then the offset within the chunk – an
extra indirection and worse spatial locality than a single contiguous array.

## 6. `std::list` and `std::forward_list` – one heap node per element

### Q19. How does `std::list` manage its elements?

**One independently heap-allocated node per element.** A `std::list` node holds the value
plus two pointers (prev, next); `std::forward_list` holds the value plus one pointer
(next).

### Q20. What is the memory and performance cost of this model?

- Per-node allocation/deallocation overhead.
- Extra bytes per element for the link pointer(s).
- Nodes scattered across the heap → pointer chasing, cache and TLB misses.

This is why `std::list` is often slower than `std::vector` even where its insertion
complexity is better.

### Q21. Why can `splice()` move elements between lists without copying values?

It relinks nodes – only the prev/next pointers are adjusted. The rode objects (and the
values inside them) stay put in memory, so no element copy or move occurs and no
allocation is needed.

## 7. `std::map` / `std::set` – tree nodes on the heap

### Q22. How do `std::map` and `std::set` manage their elements?

**One heap-allocated node per element**, arranged as a balanced (red-black) tree. Each
node stores the value plus parent/left/right pointers and a color bit. The control block
holds a root pointer and size.

### Q23. Why do map elements keep stable addresses across insertions and erasures of *other* elements?

Each element is its own node; inserting or erasing a different element only relinks
pointers, it never moves existing nodes. So pointers/references to a surviving element
remain valid (a key difference from `vector`).

### Q24. Why are map keys treated as immutable?

The key determines the node's position in the ordered tree. Mutating it in place would
violate the tree's ordering invariant, so the stored key is `const`; you must erase and
re-insert to change a key.

## 8. `std::unordered_map` / `std::unordered_set` – bucket array plus nodes

### Q25.How does an unordered container manage its elements?

**Two heap concerns:**

1. A **bucket array** – a heap-allocated array; each slot heads a chain.
2. **One heap-allocated node per element**, linked into the bucket's chain.

### Q26. How many heap allocations populate an unordered map with N elements (ignoring rehash churn)?

Roughly **N node allocations plus one bucket-array allocation** (the bucket array is
reallocated on rehash, but that's amortized). Contrast with `std::vector`, which needs a
small number of block reallocations total.

### Q27. What happens to memory on rehash?

A larger bucket array is allocated, every element's bucket index is recomputed, and nodes
are relinked into the new buckets. Node objects themselves are not reallocated (in the
standard node-based design), so references to elements survive a rehash even though
iterators are invalidated.

## 9. Container adaptors – delegated storage

### Q28. Where do `std::stack`, `std::queue`，and `std::priority_queue` store elements？

They don't manage storage directly – they **wrap an underlying container** and inherit its
memory model. Defaults: `stack` and `queue` use `std::deque`; `priority_queue` uses
`std::vector`. So a default `std::stack` has deque's two-level heap model.

## 10. Non-owning views – no element storage at all

### Q29. How do `std::string_view` and `std::span` manage elements?

They **don't own any**. Each is just a pointer plus a length pointing at storage owned by
something else. Zero allocation, zero ownership.

### Q30. What is the central hazard that follows from non-ownership?

**Dangling.** If the underlying storage is destroyed, resized, or reallocated (e.g., the
source `std::string` goes out of scope, or the source `std::vector` reallocates on
`push_back`), the view points at freed or moved memory. The view has no way to know and no
way to extend the lifetime.

## 11. Changing where elements live – custom allocators and `std::pmr`

### Q31. Can you keep a `std::vector`'s elements off the global heap?

Yes. The container's **control block still lives where the object is declared**, but the
**element storage comes from the allocator's memory resource** rather than the global
heap. With a `std::pmr::monotonic_buffer_resource` backed by a stack buffer, even a
`std::vector`'s elements can be drawn from the stack for the duration of a scope:

```cpp
std::array<std::byte, 4096> buf;
std::pmr::monotonic_buffer_resource res{buf.data(), buf.size()};
std::pmr::vector<int> v{&res};   // element storage comes from 'buf' on the stack
v.reserve(100);                  // no global-heap allocation
```

### Q32. What does the allocator *not* change?

It does not move the control block, and it does not change the container's structural
model (vector is still contiguous, list is still per-element nodes). It only changes
**which memory resource the element/node allocations draw from**.

### Q33. Why use a monotonic buffer resource for node-based containers?

A `std::list` or `std::map` normally does one heap allocation per element. A monotonic
buffer resource serves all those node allocations from one pre-reserved block, collapsing
N allocations into effectively one and eliminating per-node free overhead (it frees
everything at once when the resource dies). The risk: memory is not reclaimed per element,
so it suits short-lived, append-heavy scopes.

## 12. Summary table

| Container                    | Control block (local → stack) | Element storage                           | # heap allocations for N elements  |
| ---------------------------- | ----------------------------- | ----------------------------------------- | ---------------------------------- |
| `std::array<T,N>`            | entire object incl. elements  | inline, none                              | 0                                  |
| `std::vector<T>`             | 3 fields                      | one contiguous heap block.                | O(log N) reallocations (amortized) |
| `std::string`                | ~24-32 B incl. SSO buffer     | inline (short) or one heap block (long)   | 0 if short, else O(log N)          |
| `std::deque<T>`              | map ptr + bookkeeping         | heap map array + heap chunks              | map + ~N/chunk_size chunks         |
| `std::list` / `forward_list` | head/tail (+ size)            | one heap node per element                 | N                                  |
| `std::map` / `set`           | root + size                   | one heap tree node per element            | N                                  |
| `std::unordered_map` / `set` | bucket ptr + size             | heap bucket array + heap node per element | N + bucket reallocs                |
| adaptors                     | underlying container's        | delegated                                 | depends on underlying              |
| `string_view` / `span`       | ptr + length                  | none (non-owning)                         | 0                                  |

## 13. Rapid-fire closing questions

1. Name the only standard container that never allocates for its elements.
2. For `std::vector`, explain the difference between destroying elements and releasing
    the block, and which API does each.
3. At roughly what length does a typical `std::string` leave its inline buffer?
4. How many allocation *levels* does `std::deque` use, and what does each hold?
5. Why does `std::list::splice` need no allocation and no element moves?
6. Why do `std::map` element addresses survive insertion of other elements, while
    `std::vector` element addresses may not?
7. In an `unordered_map`, which two things are separately heap-allocated?
8. A default `std::stack` inherits whose memory model?
9. What is the one-line lifetime rule for `std::string_view`?
10. How would you make a `std::vector`'s elements live on the stack, and what does that
    *not* change about the vector?
