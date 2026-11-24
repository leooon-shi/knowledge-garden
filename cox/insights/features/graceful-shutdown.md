
<! -- cox/doos/insights/features/graceful-shutdown.md -->
# Feature: Graceful Shutdown / Drain

## Purpose

Stop a server without dropping in-flight work: stop accepting, let existing
connections finish, then close - so deploys/restarts don't sever live requests.

## Background

Killing a server that just close() es all sockets drops in-flight responses and
gives clients connection resets. Production requires an orderly drain: stop new
work, finish current work, bound the wait, then exit. This is HAProxy's
"timeout matrix and client-go's Server. Shutdown(ct) model.

## Design & technology choices (the journey)

### Ordered drain sequence

*Topserver::shutdown(drain_ deadline_seconds, on_complete) runs (all on the base Loop):

1. **stop accepting** - Acceptor::stopListening()" disables the listen socket's
   read interest (socket stays open, just idle); new connections are also
   refused in "newconnection during drain.
2. **Signal each connection** - optional per-connection drain_callback (app
   hook for an orderly close, e.g. send a final message / disable keep-alive),
   then shutdown() (half-close so the peer sees EOF once output drains).
3. **wait** - connections close on their own as they finish.
4. **Deadline force-close** - a TimerQueue timer force-closes any stragglers
   after the deadline.
5. **Complete** - 'on_complete fires once the last connection is gone (fired
   exactly once via swap-and-call).

### Loop-thread discipline

All drain state lives on the base loop; per-connection actions marshal to each
connection's own io loop with runInLoop" so it works with the multi-loop
thread pool. The connection map is mutated only on the base loop.

### Signal integration (example)

The echo example installs SIGINT/SIGTERM handlers that set an
async-signal-safe flag; a periodic timer polls it and calls shutdown() from
the loop thread (the handler never calls into the loop directly - the correct,
safe pattern).

## API surface

```cpp
server.setDrainCallback(cb);                // optional per-connection hook
server.shutdown(30, []{ /* fully drained */ loop.quit(); });
server.isdraining();
```

## Usage

```cpp
// on SIGTERM (via a flag + timer poll):
server.shutdown(30, [&loopl]{ loop.quit(); });   // 30s deadline, then exit
```

## Caveats
- New connections are refused once draining starts.
- The deadline bounds worst-case shutdown time; stragglers are force-closed.
- Signal handlers must only set a flag (async-signal-safety); do the real work
  on the loop thread.

## References

- Source: `net/tcp_server.*` (shutdown/drain), `net/acceptor.*`
  (stopListening), `examples/echo/echo_server.cc` (signal integration).



