# Feature: Logging

## Purpose

A lightweight logger for internal diagnostics and application use, with severity
levels and pluggable formatting.

## Background

A library needs internal logging (connection lifecycle, errors, overload events)
without imposing a heavy logging framework on consumers. It should be cheap and
not block the event loop.

## Design & technology choices （the journey）

### Level-based, formatter-pluggable

`Logger` exposes `debug/info/warning/error/fatal`, each tagged with a
`LogLevel`. A `Formatter` abstraction allows different output shapes:
`DefaultFormatter` (human-readable) and `JsonFormatter` (structured JSON for log
aggregation). Timestamps come from the `Timestamp` type (microsecond precision).

### Structured fields over string interpolation

A record carries optional `LogFields` (key/value pairs) alongside its message,
rather than baking context into the message text. Interpolated strings are
readable but unqueryable; separate fields survive into JSON as real members, so a
log aggregator can filter on `status` or `path` without regex-scraping. This
is also the mechanism a `trace_id` will ride on if tracing is ever added (roadmap,
OaM section).

### Formatter vs Sink: appearance vs destination

These are two independent axes, so they are two abstractions:

- **Formatter** decides how a record LOOKS (`DefaultFormatter`, `JsonFormatter`).
- **Sink** decides where it GOES (stout, file, rotating file, fan-out).

Keeping them orthogonal means "JSON to a rotating file" needs no new class - it
is a `JsonFormatter` plus a `RotatingFileSink`. Collapsing them would require a
class per combination. This is splog's split, and the contract is that
formatters never emit a trailing newline; the sink owns the line terminator.

Available sinks: `StdoutSink`, `FileSink`, `RotatingFileSink` (rolls at a byte
cap and keeps N files, so logging cannot fill a volume), `Multisink` (fan-out to
several destinations at once), and `Memorysink` (collects in memory - used by the
tests to assert on real output instead of redirecting `std::cout`).

Default-constructed `Logger`'s share ONE process-wide stdout sink. The common
pattern here is a `static logging::Logger` per translation unit; without sharing,
each would hold its own destination, interleaving output badly and making a
single async writer thread impossible.

### Flush on Fatal

A log line explaining a crash is worthless if it is still sitting in a buffer
when the process dies. Any record at or above `flush_level_` (default `Fatal`)
forces a flush, and the level is configurable via `setFlushLevel()`. This is
glog's behavior.

### Async logging: double buffering, bounded, drop-and-count

`Asyncsink` moves file I/O off the calling thread. This is the piece the reactor
genuinely needs: a synchronous `write()` on an event-loop thread stalls every
connection on that loop (features/reactor-blocking-work.md) .

The mechanism is muduo's `AsyncLogging` design - **double buffering**, not a
per-message handoff. Producers append into a current buffer under a very short
Lock; the writer thread SWAPS in an empty buffer and then does all its I/O with
the lock released. Producer cost is an append plus an occasional swap, never a
syscall and never a wait on disk. A per-message mutex-and-notify would put the
producer behind the writer's I/O, which is the thing being avoided.

The queue is **BOUNDED and DROPS when full**, incrementing a counter. The two
alternatives are both worse:

- *Block the producer* - defeats the entire purpose, since a slow disk would then
  stall the event loop.
- *Grow without Limit* - turns a logging burst into an OOM.

Dropping is consistent with how Cox sheds load everywhere else
(`ThreadPool::tryRun`, accept-rate shedding), and matches Fluent Bit's
`mem_buf_limit` behavior. Crucially the drops are **accounted for**, not hidden:
`droppedcount()` exposes the tally and a summary line is written when the writer
catches up, so silent log loss cannot masquerade as a quiet system. `stop()`
drains whatever is still queued, so shutdown loses nothing.

Note that Fluent Bit itself is NOT a library like this one - it is a separate log
processor/forwarder agent that would *consume* Cox's output (typically the JSON
formatter's one-event-per-line stdout). It is referenced here for its buffering
policy, not as a comparable component.

### Current state and honest limitations

- Level filtering is enforced before formatting, so a suppressed record costs
  only a comparison.
- Formatters and structured fields are wired end-to-end.
- Sinks are pluggable (stout / file / rotating / fan-out / memory), and
  `Asyncsink` keeps I/O off the Loop.
- No syslog sink, no time-based (daily) rotation - only size-based.
- No compile-time-eliminated log macros; arguments are evaluated even when the
  record is later dropped by the level filter.
- No `TRACE` level yet (deliberate; see roadmap for the trigger).
- No flood suppression / duplicate collapsing yet (planned - see below).
- No `Nullsink` yet (planned - see below).

### Planned: Nullsink (discard all output)

TRIGGER (observed 2026-11-03): the load-validation attack scenarios (roadmap
P1 #6) want the target server completely silent - under a flood the net layer's
per-connection logs are pure noise and there is no value in retaining them. The
blunt-but-total tool for that is a `Nullsink`: a `Sink` whose `append()`/`flush()`
do nothing, the software analogue of `/dev/null` (spdlog's `null_sink`).

Design:

- `Sink::newNullsink()` factory returning a sink that discards every line. A few
  lines of code; no state, no lock needed (nothing is touched).
- Wiring: set it as a Logger's sink (`setSink`) or as the process-wide default
  (`defaultsink()`), so `c10k_server --quiet` can silence the whole server at the
  source - cheaper and cleaner than redirecting a firehose to a file nobody
  reads.
- Cost note: the simple version still FORMATS the record before discarding it
  (the formatter runs, then NullSink throws the string away). A "skip formatting
  when the sink is null" optimization would couple Logger to a concrete Sink
  type and break the Formatter/Sink orthogonality that is the whole point of the
  split - so it is deliberately NOT done. The level filter already gates the
  genuinely hot cases (a dropped-by-level record costs one comparison), and
  formatting a line that is about to be discarded is cheap next to the syscall
  a NullSink avoids. Keep the abstraction clean.

## When to use which silencing tool

Three distinct needs, three tools (do not conflate them):

- **NullSink** - discard EVERYTHING, unconditionally. For silent
  libraries/examples, benchmarks, and the attack-scenario target. Total.
- **Flood suppression (DedupSink, below)** - keep the FIRST of each repeated
  event, drop the duplicates. For production, where you still want signal, just
  not 10,000 copies of it.
- **process stdout redirect (`>log 2>&1`)** - keep everything, move it off the
  terminal into a file. For when the full log must be retained. (Note the sink
  writes to STDOUT, not stderr, so a bare `2>log` does NOT capture it.)

### Planned: flood suppression (duplicate / similar-event collapsing)

TRIGGER (observed 2026-11-03): running the load-validation adversarial scenarios
(roadmap P1 #6), a flood attack makes the net layer emit one `ERROR` per dropped
connection - hundreds of near-identical handleError: ... Connection reset by
peer lines per second. The lines are not byte-identical (each carries a distinct
connection name `#1`, `#2`, -), so they are the SAME KIND of event, not the same
string. This both obscures the real signal and, ironically for a logger that
protects the loop with async I/O, turns a traffic burst into a logging burst.

The fix is a sink-level throttle that collapses similar events, modelled on
systemd-journald's "message repeated N times" and glog's `LOG_EVERY_T`:

- **Dedup key = (level, call site), NOT the full formatted string-** Byte-equality
  would miss the flood above because the connection name varies. The call site is
  already available cheaply - `COX_LOG_EMIT` prefixes `__func__`; threading a
  lightweight site id (e.g. `__FILE_:__LINE__` or a hash) into `LogRecord` gives a
  stable key that groups "the same log statement firing repeatedly" regardless of
  its interpolated parameters.
- **Window + burst policy (token-bucket).** Within a window (default ~1s) the first
  occurrence of a key prints normally; subsequent ones are COUNTED, not printed.
  when the window closes (or a different key arrives), emit a summary:
  `suppressed N similar '<func>' messages in the last <window>`. Cox already has a
  `TokenBucket` in `flowcontrol/` and the drop-and-account pattern in `Asyncsink`,
  so this reuses existing machinery and philosophy (shed, but ACCOUNT for it -
  never silently).
- **where it lives:** a decorator `Sink` (e.g. `DedupSink` wrapping another sink),
  so it composes with the existing stack - `DedupSink(AsyncSink(RotatingFileSink))`
  - and an application that wants every line pays nothing by not installing it.
  Keeping it in the sink (not the macro) means it collapses floods regardless of
  which call site or how many distinct connection names are involved.
- **Ordering vs async:** the dedup must run on the PRODUCER side of the key
  decision but can summarize on the writer side; the simplest correct placement is
  in front of `Asyncsink` so suppressed lines never enter the bounded queue (a
  flood should not consume the async buffer just to be dropped later).

Effort: moderate. The one real design commitment is adding a call-site key to
`LogRecord`; everything else is a self-contained decorator sink with unit tests
(flood Nidentical-site lines, assert 1 line + 1 summary; assert distinct sites
are not collapsed).

### Naming fix

An earlier misspelling (`Fetal`/`fetal`) was corrected to `Fatal`/`fatal`
across the enum, method, and call sites.

## API surface

 ``` cpp
// Plain logging (default: Info level, human-readable formatter).
logging::Logger log;
log.info("message");
Log.warning("...");
Log.error("...");
log.fatal("...");
log.setLevel(logging::LogLevel::Debug);   // enable Debug

// Structured fields.
log.info("request done", {{"method", "GET"}, {"path", "/x"}, {"status", "200"}});


// JSON output for log aggregation.
logging::Logger jlog(logging::LogLevel::Info,
                     logging::Formatter::newsonFormatter());

jlog.info("request done", {{"status", "200"}});
// ("ts": "...", "level": "INFO", "tid": "...", "msg": "request done", "status"; "200"}

// Sinks: where the lines go.
auto file = logging::Sink::newFileSink("/var/log/svc.log");
auto rot = logging::Sink::newRotatingFileSink("/var/log/svc.log"
                                              64 * 1024 * 1024，/*keep=*/5);

logging: Logger flog(logging::LogLevel::Info,
                     logging::Formatter::newsonFormatter(),
                     std::shared_ptr<logging::Sink>(std::move(rot)));

// Async: keeps file I/O off an event-loop thread (bounded; drops when full).
auto async = std::make_shared<logging::AsyncSink>(
    logging::Sink::newRotatingFileSink("/var/log/svc.log", 64 << 20, 5),
    /*max_buffered_lines=*/8192, /*flush_interval_ms=*/500);
log.setsink(async);
async->droppedCount();     // lines shed under overload

log.setFlushLevel(logging::LogLevel::Error); // default: Fatal only
```

## Usage

Most modules keep a file-local logger; these share one process-wide stout sink:

```cpp
namespace { Logging::Logger& log() { static logging::Logger l; return l; } }
log().info("TcpServer started");
```

For a service that logs from event-loop threads, install an async sink once at
startup so no loop thread ever performs file I/O:

```cpp
auto sink = std: make_shared<logging::AsyncSink>(
    logging::Sink::newRotatingFileSink("/var/log/svc.log", 64 << 20, 5));
myLogger.setSink(sink);   // JSON formatter + rotating file + async writer
```

## Caveats

- **`AsyncSink` drops under sustained overload** - by design, and never silently:
  check `droppedcount()`, and watch for the "dropped N log line(s)" summary in
  the log. If you see drops, the destination is too slow or the buffer too small.
- Structured field values are strings; the caller stringifies other types.
- Rotation is size-based only; there is no daily/time-based rotation and no
  compression of rolled files.
- `RotatingFileSink` bounds disk use to roughly `max_bytes × (max_files + 1)`.
  Size it deliberately - that is the whole disk budget for logs.
- Log arguments are always evaluated, even when the level filter later drops the
  record. Avoid expensive expressions in `debug()` calls on hot paths.
- Sink implementations must be thread-safe; the bundled ones are (the
  synchronous ones lock, `Asyncsink` hands off to one writer thread).
- For OAM concerns beyond logging - metrics, alarms, audit, tracing - treat this
  as a hook point (see roadmap; OAM section).

## References

- Source: `logging/logger.{h,cc}`, `logging/log_record.h`,
  `logging/formatter.*`, `logging/default_formatter.*`,
  `logging/json_formatter.*`, `logging/sink.{h, cc}`,
  `logging/async_sink.{h,cc}`, `logging/timestamp.*`.
- Tests: `tests/logger_test.cc`.
- References consulted: muduo `AsyncLogging` (double buffering), splug (sink
  abstraction, flush-on-level), glog (flush on fatal), Fluent Bit
  (`mem_buf_limit` bounded-buffer-with-drop policy; an external log forwarder,
  not a library).
- features/reactor-blocking-work.md (why loop threads must not do file I/O).
