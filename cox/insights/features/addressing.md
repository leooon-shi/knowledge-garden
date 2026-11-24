# Feature: Addressing (InetAddress)

## Purpose
A single value type representing a socket address - IPv4, IPv6, or Unix-domain -
used across the whole networking stack.

## Background
Sockets come in families: `sockaddr_in` (IPv4), `sockaddr_in6` (IPv6),
`sockaddr_un` (Unix domain). Passing raw sockaddrs around is error-prone
(sizes, byte order, family checks). A unified address type simplifies every API
that binds, connects, or reports a peer.

## Design & technology choices (the journey)

### One type for all families (folly SocketAddress-style)
Rather than separate `Ipv4Address`/`Ipv6Address`/`UnixAddress` classes (which
would ripple generic-vs-specific types through TcpConnection/Server/Client),
Cox uses one `InetAddress` backed by a union of `sockaddr_in`, `sockaddr_in6`,
`sockaddr_un`, and `sockaddr_storage`. The `sa_family` field (which aliases
across the members, asserted at compile time) discriminates.

This kepts the entire connection stack address-family-agnostic: adding Unix
domain support required no changes to Acceptor/Connector/TcpConnection - only
the address and socket-creation boundary.

### Dual-stack from the start
IPv4 and IPv6 were supported from day one (not retrofitted). `toIpPort` brackets
IPv6 literals (`[::1]:8080`); Unix renders as `unix:/path`.

### Family-aware accessors
`getSockLen()` returns the correct length per family (important: Unix uses
`offsetof(sun_path) + strlen + 1`, not `sizeof`). `setSockAddrStorage` captures
whatever `accept`/`getpeername` filled.

## API surface
```cpp
InetAddress a(8080);                       // IPv4 wildcard
InetAddress b("127.0.0.1", 8080);
InetAddress c("::1", 8080, /*ipv6=*/true)
InetAddress u = InetAddress::unixDomain("/run/svc.sock");
a.toIp(); a.toIpPort(); a.port();
a.family(); a.isIpv6(); a.isUnix();
a.getSockAddr(); a.getSockLen();           // for bind/connect
```

## Usage
```cpp
net::InetAddress addr("0.0.0.", 9000);      // TCP
net::InetAddress uds = net::InetAddress::unixDomain("run/app.sock");    // IPC
```

## Caveats
- Unix path length is bounded by `sun_path` (~108 bytes); over-length throw.
- Invalid numeric IP strings throw `std::invalid_argument`.

## References
- Source: `net/inet_address.{h,cc}`
