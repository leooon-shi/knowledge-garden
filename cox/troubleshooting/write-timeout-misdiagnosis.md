
# Troubleshooting: the write-timeout that "didn't fire" - a misdiagnosis caught and reverted

A worked record of load-validation scenario S2 (write timeout vs slow-read,
`performance/hardening-validation.md`) where the write timeout appeared
broken, the corE library was changed to "fix" it on an unproven theory, and the
change was then reverted once the theory could not be demonstrated. Kept because
the most valuable lesson is procedural: **validate the measurement tool before
"fixing" the thing it measures**, and do not keep a core change you cannot prove
is necessary.

## Symptom

S2 drives a slow-read attack: N clients each send a request to the echo server,
then NEVER read the reply, so the server's echo backs up its output buffer until
the write timeout (`--write-timeout`) force-closes them. Expected: all N slow
readers reaped; `cox_connections_closed_total{reason="write_timeout"}` -> N.

observed with 50 attackers against `--write-timeout 3`:

```
closed{reason="write_timeout"} = 2      (expected 50)
cox_connections_live           = 48     (stuck, not dropping)
```

Only 2 of 50 were reaped; the other 48 survived until they disconnected
themselves. RSS also stayed flat (~20 MiB) instead of spiking - a hint that
little was being buffered at all.

## Diagnosis path （including the wrong turn）

### 1. First hypothesis: transient-drain defeats the one-shot timer - PLAUSIBLE BUT WRO

Reading `TcpConnection::armWriteTimer()`: it armed a one-shot `runAfter` on the
first backup and `handleWrite()` cancelled it whenever the buffer fully drained.
Theory: on fast loopback the buffer drains in bursts, so the timer is repeatedly
cancelled/re-armed and rarely survives a full window - a slow reader that drains
a little each interval would dodge it.

This is a *real-sounding* failure mode, and the fix felt principled: replace the
one-shot with a periodic "has the buffer been continuously non-empty for the
whole window?" watchdog, tracking `output_backed_up_since_`. Implemented it in
`tcp_connection.{h, cc}`. All 214 tests still passed.
 
**This was the wrong turn.** The theory was adopted because it was plausible and
the fix was satisfying - not because it had been demonstrated.

### 2.Re-run with the "fix" - STILL 2/50

The redesign did not change the outcome. That is the moment the theory should
have been doubted: if the diagnosis were right, the fix would have moved the
number.

### 3. Try to write a discriminating regression test - IT WOULDN'T DISCRIMINATE

To justify the core change, wrote a test meant to fail under the OLD one-shot
and pass under the NEW watchdog (a consumer that drains small sips but never
empties a 64 MiB backlog). It **passed under both** implementations: a 4 KiB sip
can never empty 64 MiB, so the buffer is always non-empty when the one-shot
fires - the one-shot closes it correctly. The test did not reproduce the
theorized hole at all.

**Lesson:** if you cannot write a test that fails on the old code and passes on
the new, you have not shown the old code was broken. No discriminating test =>
no justified fix.

### 4. The decisive clue: RSS stayed FLAT, and only the first couple backed up

RSS never spiked, and exactly the first 2 connections were the only ones reaped.
That pattern says the server's output buffer was **barely backing up at all** -
so the write timeout had almost nothing to fire on. The question flipped from
"why doesn't the timer fire?" to "why isn"t the server's output backing up?"

### 5. Root cause: a bug in the LOAD GENERATOR, not the server

In slow-read mode the client sent only the FIRST partial write of its request,
then stopped: the `EPOLLOUT` continuation that keeps flushing a backed-up send
was gated to non-attack mode (`o.attack == Attack::kNone`). So the client never
finished SENDING its large request; the server had almost nothing to echo; its
output never backed up; the write timeout had no reason to fire. The 2 that
fired were the ones whose small request happened to flush fully before the
client stalled.

Fixing the client to keep flushing its request on `EPOLLOUT` (while still never
READING the reply) made the server's output back up as intended.

### 6. Confirm with the ORIGINAL one-shot timer - 50/50

Reverted the core redesign and re-ran S2 with the harness fix alone:

```
closed{reason="write_timeout"} = 50
VmRSS: 212 MiB -> 711 MiB -> 20 MiB   (buffer grows, then freed on force-close)
probe liveness: 8/8 ok
```

The original one-shot write timeout was correct all along. The RSS spike-then-
collapse is the real containment story the scenario exists to show.

## Fix

Two distinct changes, only one of which was warranted:

- **Kept (the real fix):** `c10k_client` slow-read mode now honours `EPOLLOUT`
  to finish sending its request. (Harness only - `examples/c10k/c10k_client.cc`.)
- **Reverted (the wrong turn):** the write-timeout redesign in
  `tcp_connection.{h, cc}` was undone - back to the original one-shot
  `armWriteTimer()`, dropping the added member/includes and the non-
  discriminating test. The unrelated close-reason-label change in the same files
  was validated and kept.

## Verification

- S2: 50/50 reaped with the original one-shot timer; full suite 214/214;
  containment (write_timeout closes + RSS spike/collapse) and liveness (probe
  8/8) both measured.

## Takeaways

1. **Validate the measurement tool before "fixing" the thing it measures.** The
   defect was in the load generator; the server was correct. A harness that
   under-drives the target looks exactly like a target that under-responds.
2. **A plausible theory is not a diagnosis.** The transient-drain story was
   believable and the fix was elegant, which made it seductive - and it was
   still wrong. Demand evidence (a reproduction) before changing code,
   especially core code.
3. **If the fix doesn't move the number, stop.** Re-running after the redesign
   still showed 2/50; that was the signal the root cause was elsewhere.
4. **No discriminating test => no justified change.** Being unable to write a
   test that distinguishes old from new is proof the change isn't earned.
5. **Let a surprising invariant redirect the question.** Flat RSS + only-the-
   first-two-fired reframed "why won't the timer fire!" into "why isn't output
   backing up?", which pointed straight at the sender.
6. **prefer reverting an unproven core change** over keeping "probably better"
   code. Speculative core changes are how subtle bugs enter; the bar for
   touching `net/` is a demonstrated need.

## Related

- The genuine-but-narrow one-shot weakness the wrong theory imagined (a consumer
  that fully drains the buffer just under each deadline to reset the clock)
  remains NOTED, not acted on, in `hardening-validation.md` (S2) - a real fix
  would require a reproducing case first, exactly the bar this episode failed to
  meet the first time.
