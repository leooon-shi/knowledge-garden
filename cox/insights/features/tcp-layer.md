
# Feature: TCP Layer (Sockets, Acceptor/Connector, Connections)

## Purpose

The connection-oriented networking stack: listen and accept, connect out,
manage established connections with buffered non-blocking I/O, and expose a
callback API to applications. Works for both TCP and Unix-domain stream sockets.

## Background

on Tcp of the reactor, a usable networking library must provide: RAII socket
ownership, the passive-open (server) and active-open (client) roles, and a
connection abstraction that handles partial reads/writes, buffering, half-close,
and lifetime safety across async callbacks.

## Design & technology choices (the journey)

### Thin socket ops + RAII Socket (Redis/anet style)

`sockets::` is a namespace of thin one-syscall helpers (create/bind/listen/
accept4/connect/close/shutdown/options), leaving policy to the caller (e.g.
`accept` preserves errno so the caller can handle EAGAIN/EMFILE). `Socket` is a
move-only RAII fd owner. This favors readability and explicit control over
heavy abstraction.

### Acceptor-Connector pattern (POSA2)

Passive and active open are separated:

- **Acceptor** - owns the listening socket, accepts connections, hands (fd,
  peer) to a callback. Drains all pending connections per readable event and
  includes an #*EMFILE idle-fd guard** (reserve a spare fd; on fd exhaustion,
  close it, accept+close the wedged connection, reopen) to avoid a busy-loop.
- **Connector** - non-blocking connect with async completion (writable ->
  check so_ERROR), self-connect detection, and jittered exponential backoff on
  failure (see features/flow-controller-design.md).

This mirrors Boost.Asio/Netty's role separation and keeps each side focused.

### TcpConnection: shared_ptr + channel tie

Connection lifetime across async callbacks is the classic hazard. Cox uses
`std::shared_ptr<TcpConnection>` and a **`Channel::tie(weak_ptr)`**: during event
dispatch the channel promotes the weak_ptr to a shared_ptr, guaranteeing the
connection cannot be destroyed mid-callback (muduo's technique). A state machine
(connecting -> connected -> disconnecting -> disconnected) governs transitions.

### Thread-safe send with a direct-write fast path

`send()` may be called from any thread; off-loop calls copy the data and marshal
via `runInLoop`. In-loop, it first tries a direct `write`; only the unwritten
remainder is buffered and `EPOLLOUT` enabled. On drain, write interest is
disabled and the write-complete callback fires. Half-close (`shutdown`) waits
for output to drain before `shutdown(SHUT_WR)`.

### TcpServer / Tcpclient

`TcpServer` owns the Acceptor+ connection map, assigns connections to io loops
see features/threading-model.md), and propagates callbacks + hardening knobs.
`Tcpclient` wraps a Connector, builds a TcpConnection on success, and supports
auto-reconnect.

### Stream-family reuse (TCP + Unix)

Because a connected Unix stream socket behaves like a TCP one, the entire
Acceptor/Connector/TcConnection/Server/Client machinery is reused for Unix
domain sockets with no duplication - only the address/socket-creation boundary
differs.

The one family-specific detail is in `Connector`'s retry set: for a Unix
socket, `connect` failing with `ENOENT` (the path does not exist yet) is the
normal "server not up yet" case - the analog of TCP's `ECONNREFUSED` - so
`ENOENT` (and`ECONNRESET`) are treated as retryable. A UDS client started
before its server now reconnects correctly instead of giving up on the first
attempt.

Two other boundary details: `bindorDie` unlinks a stale socket file before
binding a Unix address (otherwise a restart fails with `EADDRINUSE`), and
"handleError no longer logs a misleading error on a clean hangup - a hangup
with `SO_ERROR == 0` is a normal peer close, not a socket error.

## API surface

```cpp
// Server
TcpServer server (&loop, addr, "name");
server.setConnectionCallback(cb);
server.setMessageCallback(cb);
server.start();

// Client
Tcpclient client (&loop, addr, "name");
client.enableRetry();
client.setConnectionCallback(cb);
client.setMessageCallback(cb);
client.connect();

// Connection
conn->send (data);
conn->shutdown();       // half-close
conn->forceclose();
```

## Usage (echo server)

```cpp
server.setMessageCallback([](const TcpConnectionPtr& conn, Buffer* buf) {
    conn->send(buf->retrieveAllAsstring());   // echo
});

server.start();
```

## Caveats

- All connection methods are for the connection's loop thread; send() is th
  exception (thread-safe).
- Production knobs (timeouts, caps, backpressure) are documented in
  features/production-hardening.md.

## References

- Source: `net/socket.*`, `net/acceptor.*`, `net/connector.*`,
  `net/tcp_connection.*`, `net/tcp_server.*`, `net/tcp_client.*`.
- features/addressing.md, features/threading-model.md,
  features/production-hardening.md.
