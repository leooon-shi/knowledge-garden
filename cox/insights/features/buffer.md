# Feature: Application Buffer

## Purpose
A growable byte buffer that bridges non-blocking socket reads/writes and
application data, absorbing partial I/O and enabling efficient framing.

## Background
Non-blocking sockets deliver data in arbitrary chunks: a `read` may return part
of a message a `write` may accept only part of a send. The application needs a
place to accumulate inbound bytes until a full message is present, and to queue
outbound bytes the kernel could not take yet. A naive `std::string` append/erase
churns memory.

## Design & technology choices (the journey)

### Prepend / readable / writable layout (muduo-style)
```
+-------------------+-------------------+-------------------+
| prependable bytes |  readable bytes.  |  writable bytes.  |
+-------------------+-------------------+-------------------+
0         readerIndex         writerIndex                size
```
- **Readable** region = unconsumed content.
- **Writable** region = space for new data.
- Prependable** region = cheap space to prepend a length header without
  shifting the payload (usefol for framing).

### Compaction before growth
When writable space runs out, the buffer first tries to **compact** (move
readable bytes back over the consumed prependable space) before **growing** the
vector. This reclaims space from already-consumed data and avoids unbounded
reallocation for steady read/consume patterns.

## readv scatter read
`readFd` uses `readv` with a 64KB stack "extrabuf": one syscall can pull a large
chunk even when the buffer's writable region is small; overflow spills into the
extrabuf and is appended after. This minimizes syscalls and avoids
pre-growing the buffer for every read.

### A dedicated module for future buffer types
`Buffer` lives in its own `buffer/` module so other buffer categories (ring,
chained/zero-copy, fixed-size) can be added later without disturbing the net
layer.

## API surface
```cpp
buffer::Buffer buf;
buf.append(data, len);          // or append(std::string)
buf.readableBytes(): buf.writableBytes(); buf.appendableBytes();
buf.peek();                     // start of readable
buf.retrieve(n); buf.retrieveAll();
std::string s = buf.retrieveAsString(n);
ssize_t n = buf.readFd(fd, &err);           scatter read from a socket
```

## Usage
```cpp
// In TcpConnection::handleRead:
int err = 0;
ssize_t n = input_buffer_.readFd(fd, &err);
if (n > 0) messageCallback(conn, &input_buffer_);   // app consumes via retrieve*
```

## Caveats
- Single-threaded use per connection (lives on the connection's loop thread).
- The caller must `retrieve` consumed bytes; unconsumed data persists (and is
  subject to the max-input-buffer cap in TcpConnection).

## References
- Source: `buffer/buffer.{h,cc}.