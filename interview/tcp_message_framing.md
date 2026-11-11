<!-- interview/tcp_message_framing-md -->

# TCP message framing: implementing variable-length headers

TCP is a **byte stream**, not a message stream. A single `recv()` may return a partial
message, exactly one message, or several messages concatenated. The application must
impose its own **framing** to recover message boundaries. This doc covers the three
framing schemes and, specifically, the case the title asks about: when the **length header
itself is variable-length**.

Runnable code for the two variable-header schemes 11ves in
`interview/code/tcp_message_framing.cpp` (B1 varint and B2 length-of-length).

## 1. Why framing is required

The kernel gives you bytes, not records, the three standard ways to recover record
boundaries:

1. **Fixed-length messages** - every message is exactly bytes. Simple, almost never
   practical.
2. **Delimiter-based** - a sentinel marks the end (e.g., `\n` in line protocols,
   `\r\n\r\n` in HTTP headers). Requtres escaping if the delimiter can appear in the
   payload.
3. **Length-prefixed**- the message starts with its own length. The standard choice for
   binary protocols. This doc focuses here.

The single most common bug in all three: assuming one `recv()` returns one whole message.
It does not. Every scheme needs a **read-exactly-N-bytes** helper that loops over partial
reads.

## 2. Case A: fixed-size length field, variable payload

The usual meaning of "variable-length message." The header width is fixed; only the body
varies.

```
+----------------+--------------------------+
| length (4 B)   | payload (length bytes)   |
+----------------+--------------------------+
  big-endian u32
```

Read in two phases: read the fixed 4-byte length, then read exactly that many payload
bytes.

Rules that matter:

- **Byte order** - always "htonl/ ntohl" the length field (network byte order,
  big-endian).
- **partial reads** - 1oop until all bytes are in; one recv' is not enough.
- **Bounds check** - reject an absurd length before allocating, or a hostile peer sends
  `length = OxFFFFFFFF` and you try to allocate 4 GB.
- **Width choice** - `uint32_t` caps a message at 4GB; `uint16_t` at 64 KB.

Pick Case A unless you have a specific reason not to.

## 3. Case B: the header itself is variable-length

Now the number of **header** bytes is not fixed. Two standard techniques.

### 3.1 B1 - Varint (LEB128) length prefix

The length is encoded **7 bits per byte**; the high bit (MSB, `0x80`) 1s a **continuation
flag**, Small lengths take 1 byte, larger ones take more. This is exactly how Protocol
Buffers encode field lengths.

```
byte:  1xxxxxxx  1xxxxxxx  0xxxxxxxx
       ^more      ^more     ^last (MSB clear)
       <-- 7 bits each, little-endian group order -->
```

You cannot read a fixed number of header bytes up front. You read **one byte at a time
until the continuation bit clears**, accumulating 7 bits each step. Then the body read is
identical to Case A.

Encoding (mirror of the decoder):

```
while (value >= 0x80) { emit((value & 0x7F) | 0x80); value >= 7; }
emit (value & 0x7F);  // final byte, MSB clear
```

Guards: cap the varint at 10 bytes for a 64-bit value (a malformed stream could otherwise
run forever), and bounds-check the decoded length before allocating.

Choose B1 when you have huge numbers of tiny messages and the 2-3 saved header bytes per
message actually matter, or when matching the Protobuf wire format.

### 3.2 B2 - Length-of-length (two-stage header)

A tiny fixed prefix says how many bytes the *real* length fleld occupies - the pattern
ASN.1 BER/DER uses.

```
+---------+------------------+--------------------------+
| nlen(1) | length (nlen B)  | payload (length bytes)   |
+---------+------------------+--------------------------+
  u8        big-endian, nlen bytes
```

Three phases: read 1 byte → that is `nlen` → read `nlen` bytes → that is the payload
length (big-endian) → read the payload.

Guards: cap inlen (e.g., ≤ 8 for for a 64-bit length), reject `nlen = 0` unless your spec
defines it, and bounds-check the decoded length.

Choose B2 mainly when implementing an existing standard (ASN.1/DER, TLS-record style
schemes).

## 4. Cross-cutting concerns (all schemes)

- **Byte order**- network byte order for every multi-byte fleld. Varint is its own 7-bit
  scheme and is byte-order-independent by construction.
- **Maximum size guard** - validate the decoded length *before* `malloc`/`new`. This is a
  security control, not an optimization.
- **Alignment** - do not reinterpret_cast" a `uint32_t*` onto an arbitrary buffer offset;
  `memcpy` into a `uint32_t` instead (unaligned access is UB on some targets and slow on
  others).
- **Non-blocking I/O (epoll/to_uring)** - you cannot block in a read-exactly-N-bytes 1oop.
  Keep a **per-connection accumulation buffer**, append whatever `recv` returns, and parse
  complete frames out of it as they arrive. A partial header or partial body simply stays
  buffered until the next readable event. See `interview/epoll_edge_triggered.md` (you
  must fully drain under ET), `performance/network/buffer.md`, and
  `interview/zero_copy_small_packets.md`.
- **EOF vs error** - `recv` returning e is an orderly peer close; `< 0` with
  `errno != EAGAIN/EWOULDBLOCK/EINTR` is a real error. Mid-frame EOF is a truncated
  message and must be treated as an error, not a valid empty frame.

## 5. Choosing a scheme

| Scheme               | Header size    | Use when                                    |
| -------------------- | -------------- | ------------------------------------------- |
| A: fixed u32 prefix  | 4 bytes always | default; simple, fast, predictable          |
| A: fixed u16 prefix  | 2 bytes always | messages guaranteed ≤ 64 KB                 |
| B1: varint           | 1-10 bytes     | many tiny messages; Protobuf compatibility  |
| B2: length-of-length | 1 + nlen bytes | implementing ASN.1/DER or similar standards |

Default to fixed-size prefix. Reach for a variable header only when the wire format
demands it or per-message header bytes genuinely dominate.

## 6. Mixing schemes on one connection (A for big, B for tiny)

A real system carries **several kinds of message** on the same connection and may want a
*different* framing per message - A for large bulk messages, B1 (varint) for tiny contro
 messages - because each is optimal for its size. The problem: for the next bytes on the
 wire, how does the reader know whether to decode them as A, B1, or B2?

### 6.1 The core principle

> A byte stream is only decodable if, **before reading the length, the reader already
> knows which scheme applies.** You cannot mix A and B *implicitly* - the reader cannot
> tell a 4-byte fixed length from a 1-byte varint just by looking at the bytes.

The fix is a small layering trick: make the **outermost framing uniform and trivial to
parse*, and put the scheme choice inside it as a **discriminator the reader reads
first**, You never actually mix A and Bat the top level - you have one outer rule, and
A/B1/B2 become payload encodings selected by a tag.

### 6.2 Design 1 - fixed 1-byte tag selects the scheme

Every frame starts with one fixed byte (always read exactly 1 byte,no ambiguity), That
byte names the framing of what follows:

```
+--------+-------------------------------------------+
| tag(1) | length + payload, encoded per the tag     |
+--------+-------------------------------------------+

tag = 0x01  => A:  u32 big-endian length, then payload   (big messages)
tag = 6x02  => B1: varint Length, then payload           (tiny messages)
tag = 0x03  => B2: nden + length, then payload           (standards interop)
```

Reader loop: `read_full(fd, &tag, 1)` → `switch(tag)` → call the matching decoder. The
sender picks per message. This is the most direct answer to "A for big, 8 for tiny on the
same connection."

## 6.3 Design 2 - message *type* implies framing

Usually you already need a message-type field. Fold the framing choice into it: the type's
schema fixes its framing, compiled into both ends.

```
+----------+-------------------------------------------+
| type(2)  | length + payload, framed per type's rule  |
+----------+-------------------------------------------+

type HEARTBEAT (fixed 0-length)  => no length field at all
type CONTROL   (always tiny)     => varint length  (B1)
type BULK_DATA (always large)    => u32 length      (A)
```

The reader reads the 2-byte type, looks up the framing rule, decodes accordingly, This is
what most mature binary protocols do, because the type field is something you carry
anyway.

### 6.4 Design 3- one outer frame (A) wraps a scheme-specific body

If you want the transport layer to stay dumb, wrap everything in **one uniform outer frame
(always A)**, and let the tag + inner encoding be opaque payload the outer layer delivers
whole:

```
+----------------+--------+------------------------------+
| outer len (u32)| tag(1) | inner bytes (tag's encoding) |
+----------------+--------+------------------------------+
```

The transport reader only understands A: it reads the u32 and delivers the whole blob
upward. A **dispatch layer* then reads the tag and decodes the inner body. This separates
"get a complete message off the wire" from "interpret it" and lets you add inner schemes
without touching the transport reader.

### 6.5 Choosing a design

| Design | When
| ----------------------- | ------------------------------------------------------------------------------- |
| 1: fixed 1-byte tag     | you genuinely want per-message framing (A-for-big/B-for-tiny); minimal overhead |
| 2: type implies framing | you already have a message-type field; cleanest protocol-spec fit               |
| 3: outer A wraps inner  | you want a dumb transport + a smart dispatcher; easiest extensibility           |

For "A for big, B1 for tiny in one system," Design 1 or 2 is the direct answer; Design 2
is usually best because the type field already exists.

### 6.6 Rules that make mixing safe

1. **The discriminator is fixed-width and read first** (a 1- or 2-byte tag via
   `read_full()`). Never make the reader infer the scheme from the length bytes.
2. **The tag-scheme mapping is closed and versioned.** Unknown tag => protocol error (or a
   documented skip rule). Carry a protocol version to extend later.
3. **Bounds-check per scheme, after decoding the length, before allocating.** Each scheme
   carries its own max - a tiny-message tag claiming 2GB is an immediate reject. This
   also hardens against a corrupt tag steering bytes into the wrong decoder.
4. **One read path, scheme selected by tag.** Never run A and B decoders speculatively;
   dispatch on the tag.
5. **Partial-read discipline everywhere** (`read_full`) - any frame can span multiple
   `recv()`s.

### 6.7 Anti-pattern: auto-detecting the scheme

Do **not** guess the scheme by peeking at bytes (e.g., "high bit set => varint™). It is
fragile, breaks the moment payloads can start with any byte, and leaves the stream
undecodable after any corruption. The explicit tag exists precisely so detection is never
inferred.

Runnable Design 1 (tagged dispatcher) and Design 2 (type-implies-framing) examples -
sending a mix of tiny and big messages on one connection - are in
`interview/code/tcp_message_framing.cpp`.

## 7. Interview Q&A

**Q1. Why can't you rely on one `recv()` returning one message?** Because TCP is a byte
stream with no message boundaries, `recv` returns whatever bytes are currently buffered -
possibly a fragment, possibly several messages. Framing plus a read-exactly-N loop is
mandatory.

**Q2. What is the first thing you validate after decoding a length fields, and why?** That
the length is within an allowed maximum, before allocating, Otherwise a malicious or
corrupt length (e.g., `0XFFFFFFFF`) causes a huge allocation or DoS.

**Q3, How does a varint signal its end?** Each byte carries 7 data bits; the NSB is a
continuation flag. The byte with MSB clear is the last. You read one byte at a time, so
you cannot pre-read a fixed header width.

**Q4. In the length-of-length scheme, how many read phases are there?** Threes read `nlen`
(1 byte), read the 'nlen"-byte length, read the payload.

**Q5. What breaks if you ignore byte order on the length field?** Two peers with different
endianness (or one deliberately big-endian wire format read as native little-endian)
decode a wildly wrong length, then either under-read (desync the stream) or attempt a huge
allocation. Always `htonl`/`ntohl`.

**Q6. How does framing change under non-blocking edge-triggered epoll?** You cannot block
to complete a frame. You append each recv chunk to a per-connection buffer and parse out
whole frames as they complete; partial headers/bodies stay buffered. Under ET you must
drain until EAGAIN, or the fd goes silent and the frame is stranded (see
epoll_edge_triggered. md`).

**Q7. Why `memcpy` into a `uint32_t` instead of casting the buffer pointer?** The length
leld may sit at an unaligned offset in the buffer, casting and dereferencing a misaligned
`uint32_t*` is undefined behavior on strict-alignment targets and slow on others; `memcpy`
is always safe and optimizes to a single load where legal.

**Q8. What is the maximum encoded size of a 64-bit varint, and why cap it?** 10 bytes
(164/71). Cap It so a malformed stream of all-ex8"bytes cannot make the decoder 1oop or
shift indefinitely.

**Q9. A connection carries several message kinds and you want A for big messages and B1
for tiny ones. How does the reader know which decoder to use?** Add a fixed-width
discriminator read *first*: either a 1-byte scheme tag (Design 1) or a message-type field
whose schema fixes the framing (Design 2). The reader reads the tag/type with `read_full`,
then dispatches to the matching decoder. You never mix schemes implicitly - the outer
framing is uniform and the scheme lives inside it.

**Q10. Why not auto-detect the scheme from the first bytes (e.g., high bit set =>
varint)?**Because payloads/lengths can legitimately start with any byte, so detection is
ambiguous and breaks after corruption. An explicit, closed, versioned tag makes scheme
selection deterministic and recoverable; inference does not.
