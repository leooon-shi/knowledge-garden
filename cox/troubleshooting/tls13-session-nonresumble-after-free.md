
# Troubleshooting: TLS 1.3 resumption failing because the cached session was invalidated when its connection closed

A worked record of why `TlsResumptionTest.ClientResumeswithServerTickets`
reported a full handshake on the second connection
(`SSL_session_reused == 0`). Kept because the symptom points squarely at Cox's
resumption wiring (cache, tickets, engine) while the real cause is an OpenSSL
TLS 1.3 lifetime rule - and because the decisive move was reproducing it in
PURE OpenSSL to exonerate Cox before changing any Cox code.

## symptom

```
[ RUN      ] TlsResumptionTest.ClientResumeswithServerTickets
tests/tls_session_resumption_test.cc:181: Failure
Expected equality of these values:
  SSL_session_reused(client.nativeHandle())  Which is: 0
  1
second connection to the same origin must resume
```

Everything that "should" prove resumption works was already true:

- the client's `Sessioncache` held exactly one entry after connection 1
  (`sessioncache()->size() == 1`, the test's own `ASSERT` passed);
- the engine offered it on connection 2 (`SSL_set_session` returned 1);
- the server issued tickets (ticket-key callback fired in `enc=1` / seal mode).
Yet the client did a full handshake.

## Diagnosis path (including the dead ends)

### 1. First instinct: a timing bug delivering the NewSessionTicket - PARTIALLY WRONG

TLS 1.3 sends `NewSessionTicket` as POST-handshake records, so the obvious guess
was that the test's `exchangeAndsettle()` closed the client before the ticket
arrived and nothing got cached. Ruled out as the cause: instrumenting the
new-session callback showed the session WAS cached, `resumable=1`,
`ticket_len=208`, `proto=0x304` (TLS 1.3). The ticket arrived and was stored.
(The drain loop was still worth hardening - pump until no more bytes flow - but
it was not the bug.)

### 2. Look at what's actually on the wire - THE KEY OBSERVATION

Logging the per-round byte counts in the handshake driver exposed it:

```
conn 1 (full):    c2s=311  s2c=1337   (ClientHello 311 bytes)
conn 2 (resume?): c2s-311  s2c=1337   (ClientHello STILL 311 bytes)
```

A ClientHello offering a PSK/ticket carries the 208-byte ticket plus the
`pre_shared_key` extension - it cannot be byte-identical to a full-handshake
ClientHello. Identical size => **no PSK was sent**, even though
`SSL_set_session` had returned 1. Correspondingly, the server's ticket-key
callback NEVER ran in `enc=0` (decrypt) mode - it had no offered ticket to open.

### 3. Reproduce in pure OpenSSL - EXONERATE COX

Before touching `SessionCache`/`TsEngine`/`TicketKeyManager`, a ~100-line
standalone C program using raw OpenS with memory BIOs (same shape as the test:
client offers a cached `SSL_SESSION` via `SSL_set_session`, server uses a
ticket-key callback) reproduced it EXACTLY: 2nd ClientHello 311 bytes,
`reused=0`. No Cox code involved => the fault is in how OpenSSL is being driven,
not in Cox's wiring.

### 4. Bisect the session's resumability - ROOT CAUSE

Printing `SSL_SESSION_is_resumable()` on the cached session at each step:

```
[newcb]           resumable=1   (cached at handshake completion)
[before SSL_free] resumable=1
[after SSL_free]  resumable=0   <-- flipped here
```

Freeing the client `SSL` the session came from flips it to non-resumable. The
test (and the pure-OpenSSL repro) fully close connection 1 - the first
`TlsEngine` leaves scope and its destructor calls `SSL_free` - BEFORE opening
connection 2. By offer time the cached ticket is `resumable=0`, so
`SSL_set_session` accepts it but the handshake code omits the PSK.

Confirmed both directions:

- Keep connection 1's `SSL` alive past connection 2's offer -> `reused=1`
  (ClientHello grows 311 -> 582, server replies without a cert).
- `SSL_SESSION_dup()` the session at cache time -> survives the free,
  `resumable=1` after, `reused=1` even with connection 1 fully closed.

## Root cause

For TLS 1.3, an `SSL_SESSION` delivered to the new-session callback stays bound
to the `SSL` connection that produced it. When that connection is freed, OpenSSL
marks the session non-resumable. Cox's `SessionCache::store()` only took an
`SSL_SESSION_ up_ref` of that SAME object, so the cache was holding a reference
to a session whose resumability was still tied to a connection it did not own.
Once the originating connection closed - exactly what a connection POOL does
before it reconnects to the same origin - the cached ticket silently stopped
offering a PSK.

This is not a defect in the cache logic, the ticket-key manager, or the engine's
offer/stash path; all of those were correct. It is an ownership/lifetime
mismatch with an OpenSSL rule that only bites TS 1.3 and only once the minting
connection is gone.

## Fix

`SessionCache::store()` now keeps an INDEPENDENT copy via `SSL_SESSION_dup()`
instead of sharing a reference with `SSL_SESSION_up_ref()`. A dup'd session is
standalone - its resumability does not depend on any live `SSL` - so a pooled
ticket survives the churn of the connection that created it. The new-session
callback still returns 0 (does not take the callback ref); the cache owns its
copy and frees it on eviction/replacement/clear. Verified: the test passes,
ASan/LSan clean (no leak from the extra object), full suite 215/215.

## Takeaways

1. **`SSL_set_session()` returning 1 does NOT mean a PSK will be offered.** It
   accepts the session; whether a PSK goes on the wire depends on the session
   still being resumable at handshake time. Check `SSL_SESSION_is_resumable()`
   at OFFER time, not just at cache time.
2. **The ClientHello size is a free oracle for "did we offer a ticket?"** A
   resumption attempt is visibly larger than a full-handshake ClientHello. Two
   identical sizes mean no PSK, regardless of what the cache or `set_session`
   say.
3. **For TLS 1.3, dup sessions into a cache; do not just up-ref them.** A shared
   ref keeps the session alive but tied to its originating `SSL`; freeing that
   connection (what a pool does) makes the ticket non-resumable. `SSL_SESSION_dup`
   decouples it.
4.  **Reproduce in the bare library before editing your own code.** A short
    pure-OpenSSL program reproduced the exact symptom and proved the fault was in
    how OpenSSL was driven, not in Cox - turning a hunt through four Cox classes
    into a one-line change with a known reason.
5.  **Beware the single-SL unit test for a multi-connection feature.** The test
6.  closed connection 1 before opening connection 2, which is the precise
7.  condition that triggers the bug; real pooled use (old connection lingers)
8.  would have masked it. The feature's test shape must match its real lifetime.