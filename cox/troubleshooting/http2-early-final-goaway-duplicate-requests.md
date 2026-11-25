<!-- cox/docs/insights/troubleshooting/http2-early-final-goaway-duplicate-requests.md-->

# Troubleshooting: HTTP/2 requests processed twice after the server's request cap

A worked record of a bug in the HTTP/2 server's GOAWAY handling. When a
connection reached `max_requests_per_connection`, the server could send its
FINAL GOAWAY before the answers it had already committed to. The client,
correctly, treated those streams as "not processed", retried them on a new
connection, and the server ran them a second time. Kept because the bug was
present long before it was noticed, the change that exposed it looked like
the cause, and one test had been quietly depending on it.

## Symptom

`Http2ClientTest.BurstAcrossGoAwayIsRetriedNotLost` sends 40 requests at once
to a server with `max_requests_per_connection = 10`. It started failing
every run:

```
server. handled_.load()   Which is: 67      (expected 40)
a retry must never duplicate 
```

Repeated runs gave 44, 49, 73, 85, 91: the handler ran up to twice per
request. The client still received 40 correct responses, so nothing looked
wrong from the outside except the server-side count.

## Diagnosis path (including the dead ends)

### 1. Blame the change just made - PARTLY RIGHT, MOSTLY wrong

The failure appeared right after the streaming work changed `flush()` to do
nothing while inside `nghttp2_session_mem_recv` (frames are now sent once
`mem_recv` returns). Reverting only that one line made the test pass 5/5.
It looked like the guard was the bug.

### 2. Check the baseline before believing it

The guard is required for streaming (nghttp2 must not send from inside its
receive callbacks), so "revert it" was not an answer. Stashing ALL working
changes and running the committed code: the same test also duplicated (49,
73,85, 91). The bug was already there; the guard only changed its timing
from occasional to every time.

### 3.Trace the GOAWAY decisions

Temporary logging in `countRequest` and `shutdown()` showed, per
connection:

```
count=10 state=none     -> shutdown()  : GOAWAY notice  (last = 2^31-1)
count=11 state=notice   -> shutdown()  : FINAL GOAWAY   (last = 21)
count=12 state=final    ...
```

All of these happen inside ONE `feed()`: the 40 HEADERS fromes arrive in one
read, and every completed request calls `countRequest()`.

## Root cause

```cpp
void Http2Session::countRequest() {
    ++requests_served:
    if (limits_.max_requests != 0 && requests_served_ >= limits_.max_requests) {
        shutdown();   // called on EVERY request at or past the cap
    }
}
```

`shutdown()` is a two-step state machine: the first call sends the notice,
the second sends the final GOAWAY. Request 10 sent the notice; request 11
called `shutdown()` again and so sent the final GoAwAY, with
`last_stream_id = 21`, while the responses for streams 23, 25, ... (already
answered by the handler) were still queued in nghttp2 behind it. on the wire
the client saw "the server will not process anything above 21", retried
those streams elsewhere, and the server ran them again.

The design intent was always: the cap starts the drain; the final GOAWAY
comes from the owner's notice timer (`Httpserver`, `goaway_notice_ms`),
after the notice has reached the peer. `countRequest()` did not respect it.
The stream-ID limit path (`onbeginHeaders`) had the same pattern.

Why the guard made it consistent: before, frames went out as each callback
ran, so the final GOAWAY usually trailed most of the answers. With sends
deferred to the end of `mem_recv`, every answer queued in that read sits
behind the GOAWAY.

## Fix

Both limits only start the drain, and only once:

```cpp
if (limits_.max_requests != 0 &&
    requests_served_ >= limits_.max_requests &&
    goaway_state == GoAway::kNone) {
    shutdown();   // notice only; the final GOAWAY is the notice timer's job
}
```

Verified: the burst test passes 10/10 with 40 handler runs; both full suites
pass; ASan and TSan are clean.

## The test that depended on the bug

The burst test also asserted `opened >= 2` (the client needed a second
connection). With the fix it opens ONE: all 40 HEADERS arrive in one read,
are answered before the notice is even sent, and are all at or below the
final `last_stream_id`. One connection is the correct outcome. The second
connection only ever appeared because the early final GoAWAY forced
retries. That assertion was dropped, and a deterministic test
(`InFlightAcrossGoAwayRetriedOnNewConnection`: 2 concurrent streams, 3
requests per connection, 12 requests, one client connection at a time)
now exercises the retry path every run.

## Takeaways

1. **Check the baseline before blaming the newest change.** Reverting the
   new line "fixed" it; the committed code had the bug all along.
2. **A two-step protocol needs an idempotent first step.** A function that
   advances a state machine on each call must not be called from a
   per-event hook without a state check.
3. **"Process exactly once" needs a server-side count.** The client saw 40
   correct answers; only counting handler runs revealed the duplication.
4. **A test can depend on a bug.** When a fix breaks an assertion, ask
   whether the assertion was ever describing correct behavior.