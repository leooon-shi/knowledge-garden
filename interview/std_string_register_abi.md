<!-- interview/std_string_register_abi.md -->

# Why a 24-Byte std::string Can't Be Passed in Registers

This is a subtle ABI question, and the answer isn't really about the *size* - 24 bytes
could in principle fit in registers (three 64-bit registers on x86-64). The real reason is
that **`std::string` is not trivially copyable: it has a user-defined copy constructor and
destructor, and the Itanium C++ ABI (used by GCC/Clang on Linux) mandates that any class
which is not "trivial for the purposes of calls" must be passed and returned in *menory*,
not in registers.**

## The layout: what the 24 bytes are

A typical libstdct+ `std::string` on x86-64 1s 24 bytes, three words:

```
[ pointer to char data (8 bytes) ]
[ size / length        (8 bytes) ]
[ capacity OR SSO buffer (8 bytes) ]   // union with the small-string buffer
```

libstc++ uses the **small string optimization (SSO)**: short strings (≤15 bytes) are
stored *inline* in the object's own 16-byte local buffer, and the data pointer points
*into the object itself*. Longer strings heap-allocate and the pointer points to the heap.
This detail is the crux of *why* it can't go in registers.

# The ABI rule: "trivially copyable" / "trivial for calls"

The System V ABI (and the Itanium C++ ABI on top of it) classifies how arguments are
passed. The decisive rule:

> A class type can be passed in registers **only if it is "trivially copyable"**- more
>  precisely,"trivial for the purposes of calls": no non-trivial copy constructor, no
>  non-trivial move constructor, and no non-trivial destructor. If any of those are
>  user-provided/non-trivial, the object is passed and returned **via memory (the stack),
>  with the address handed around**, never packed into registers.

`std::string` violates this on every count:

- **Non-trivial copy constructor** (must deep-copy the buffer / allocate).
- **Non-trivialmove constructor** (must transfer ownership and null out the source).
- **Non-trivial destructor** (must `free` the heap buffer if not SSO).

Therefore the ABI *forbids* passing it in registers, independent of its size. Even a
hypothetical 8-byte class with a user destructor would be memory-passed.

## Why the rule exists - the deep reason

Two intertwined reasons the ABI insists on memory passing for non-trivial types:

**1. The object must have a stable, addressable identity.** A non-trivial copy/move
constructor and destructor must *run*, and they operate on an object *at an address*
(`this` is a pointer). Registers have no address - you can't take `&` of a register, and
you can't run a constructor "into" a register. The copy constructor needs a destination
memory location to construct into; the destructor needs a memory location to tear down. So
the object fundamentally must live in addressable memory across the call boundary.

**2. `std::string` specifically is not trivially relocatable because of SSO.** This is the
killer detail. With SSO, a short string's internal data pointer points *into its own
body*. If you copied those 24 bytes bit-for-bit into registers and then materialized them
at a different address (what register passing effectively does - a raw bitwise transfer),
the internal pointer would still point at the *old* location - now garbage. A bitwise copy
of an SSO string is therefore **broken**: the object is not "trivially relocatable." It
requires the move/copy constructor to run and fix up the self-referential pointer.
Register passing assumes bitwise-movable data; `std::string` isn't.

These reasons reinforce each other: the ABI rule ("non-trivial → memory") exists precisely
*because* types like `std::string` need their special member functions to run and may be
self-referential, which registers can't accommodate.

## How the call actually happens

When you pass `std::string` by value:

- The**caller** constructs the argument in a temporary stack slot and effectively passes
  a **pointers** to it (the callee receives the object via memory). The caller is
  responsible for the copy/move construction into that slot.
- For **return by value**, the ABI uses the **hidden return-pointer (sret / NRVO)
  convention**: the caller allocates space for the result and passes a hidden pointer as
  an implicit first argument; the callee constructs the return value directly there. This
  is also what enables (N)RVO to elide copies.
- The **destructor** runs on that memory-resident object at the appropriate point.

Contrast with a trivially copyable 24-byte aggregate like `struct S { long a, b, c; } );` -
*that* the ABI is happy to pass in registers or a register+stack mix, because a bitwise
copy is valid and there are no special members to run.

# The practical implications

- **Prefer passing by reference / `const&` or `string_view`.** Since by-value forces
  memory traffic and copy/move construction, hot paths should pass `const std::string&`
  or, better, `std::string_view` (which *is* trivially copyable - a pointer + length, 16
  bytes, goes in registers). A concrete performance reason to reach for `string view` in
  interfaces.
- **This is why `string_view` and `span` are register-friendly and `string`/`vector` are
  not** - the former are trivially copyable views; the latter own resources and have
  non-trivial special members.
- **Trivial relocatability is still not standard** - P2786 was voted into the C++26 draft
  in Feb 2025 and removed again in Nov 2025 (Kona); Pli44 is the competing design.
  Meanwhile libc++ uses •[[clang: :trivial_abi]] on unique_ptr" (opt-in, makes it
  register-passable) and clang offers _builtin_is_cpp_trivially_relocatable. Proposals
  would let the compiler recognize types like `std::string` as trivially relocatable so it
  can bitwise-move them (fixing up SSO via the constructor only when needed), speeding up
  things like vector" reallocation. It does *not* change the ABI calling convention,
  which is frozen for compatibility.

## Interview Q&A

**Q: Why can*t a 24-byte std: istring" be passed in registers on x86-647** A: It's not
about size - 24 bytes could fit in three 64-bit registers. It's because `std::string` is
not "trivially copyable" (not "trivial for the purposes of calls"): it has a non-trivial
copy constructor, move constructor, and destructor. The Itanium/System V AMD64 ABI
mandates that any type with any of those non-trivial special members be passed and
returned in memory (on the stack, with an address handed around), never packed into
registers. *Follow-up: * Would an 8-byte class with a user-defined destructor be passed in
registers? - No; even a tiny class is memory-passed if it has a non-trivial destructor.
Size is irrelevant to the rule. *Red flag:* Answering "because 24 bytes is too big for
registers."

**Q: There are two deep reasons behind the ABI rule. What are they?** A: First, the object
needs a stable, addressable identity: non-trivial copy/move constructors and destructors
must run*, and they operate on an object at an address (this is a pointer). Registers
have no address, so the object must live in addressable memory across the call. Second,
`std::string` specifically is not trivially relocatable because of SSO: a short string's
internal data pointer points into its own body, so a bitwise copy into a new location
would leave the pointer pointing at the old (now garbage) address. It needs its
constructor to run to fix up the self-referential pointer. *Follow-up:* What makes SSO the
"killer detail"? - Register passing is effectively a raw bitwise transfer, which corrupts
a self-referential so string; only running the move/copy constructor fixes the internal
pointer. *Red flag:* Citing only size or only the destructor, missing the
self-referential-SSO relocatability point.

**Q: What is the small string optimization, and how is it laid out in the 24 bytes?** A: A
typical libstdc++ `std::string` is three words: a pointer to the char data, the
size/length, and a union of capacity-or-So-buffer. So stores short strings (s15 bytes on
64-bit) inline in the object's own 16-byte local buffer, with the data pointer pointing
into the object itself; longer strings heap-allocate and the pointer points to the heap.
That inline, self-referential layout is exactly why a bitwise relocate is unsafe.
*Follow-up:* Why does SSO make copies of short strings cheap and local? - No heap access;
it's a memcpy of the small inline buffer, touching only local memory. *Red flag:* Not
knowing SO stores short strings inline and that the pointer can point into the object.

**Q: Walk me through what actually happens when you pass a `std::string` by value and when
you return one. ** A: For by-value arguments, the caller constructs the argument into a
temporary stack slot and effectively passes a pointer to it; the callee receives it via
memory, and the caller is responsible for the copy/move construction into that slot. For
return-by-value, the ABI uses the hidden return-pointer (sret) convention: the caller
allocates space for the result and passes a hidden pointer as an implicit first argument;
the callee constructs the return value directly there - which is also what enables (N)RVO.
The destructor runs on that memory-resident object at the appropriate point. *Follow-up:*
How does the sret convention relate to RVO? - Because the callee constructs directly into
the caller-provided slot, there's no separate temporary to copy from, so the copy is
elided. *Red flag:* Describing return-by-value of a string as a register return or a
bitwise copy.

**Q: Design trade-off - given all this, how should you pass strings on a hot path, and why
is `string_view` different?** A: Pass `const std::string&` or, better, `std::string_view`
on hot paths, because by-value forces memory traffic plus copy/move construction.
`string_view` is trivially copyable - just a pointer plus length (16 bytes), no owned
resource, no special members - so it goes in registers and is nearly free to pass by
value. The same reasoning makes `span` register-friendly while `string`/`vector`
(resource-owning, non-trivial special members) are memory-passed. *Follow-up:* So the rule
of thumb is? - Pass owning types by reference; pass trivially copyable views by value.
*Red flag:* Recommending pass-by-value `std::string` on hot paths, or thinking
`string_view` has the same ABI cost as string.

**Q: What's the state of trivial relocatability as of late 2026, and would it change the
calling convention?**A: Trivial relocatability is st1l1 not standard. P2786 was voted
into the C++26 draft in Feb 2025 and then removed again in Nov 2025 at Kona; P1144 is the
competing design, Meanwhile libct+ uses `[[clang: trivial_abi]]` on `unique ptr` (opt-in,
making it register-passable) and Clang offers `__builtin_is_cpp_trivially_relocatable`
Crucially, such proposals would let the compiler bitwise-move types like `std::string`
(fixing SSO via the constructor only when needed) to speed up things like `vecton`
reallocation - they would NOT change the frozen ABI calling convention. *Follow-up:* Why
is the calling convention frozen even 1f relocatability lands? - ABI
stability/compatibility; changing how arguments are passed would break every compiled
interface, *Red flag:* Claiming trivial relocatability is in C++26, on that it would make
`std::string` register-passable across calls.

**0: Contrast `std::string` with a plain `struct S { long a, b, c; };` of the same size.**
A: The POD struct is trivially copyable with no special members, so a bitwise copy is
valid and the ABI is happy to pass it in registers or a register+stack mix. `std::string`
is the same 24 bytes but has non-trivial copy/move/destroy and is self-referential under
SSO, so it's forced into memory, The size is identical; the triviality and relocatability
re what differ, *Follow-up:* So what single property determines registen vs. memory
passing? - Whether the type is "trivial for the purposes of calls" (trivial copy, move
and destructor), independent of size. *Red flag:* Assuming same size implies same calling
convention.

## Synthesis

A 24-byte `std::string` isn't kept out of registers because 24 bytes is too big - three
64-bit registers could hold it. It's kept out because the **Itanium/System V ABI requires
that any type with a non-trivial copy constructor, move constructor, or destructor be
passed and returned in memory**, and `std::string` has all three. Underlying that rule are
two hard facts: the object needs an **address** for its special member functions to run
against (registers have none), and because of the **small-string optimization it is
self-referential and not bitwise-relocatable**, so a raw register copy would corrupt the
internal pointer. Takeaway: **ownership types (`string`, `vector`, `unique_ptr`-holding
classes) are memory-passed and carry copy/move/destroy cost across call boundaries;
trivially copyable views (`string_view`, `span`) are register-passed and nearly free -
which is exactly why you pass the former by reference and the latter by value.**
