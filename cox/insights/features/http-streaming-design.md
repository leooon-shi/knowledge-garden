<!-- cox/insights/features/http-streaming-design.md -->

# Design: streaming bodies and asynchronous handlers (HTTP/1.1 and HTTP/2)

> **Status:** phase 1 DONE : server side, HTTP/2. `ServerStream`
> (`http/server_stream.*`) + `HttpServer::setStreamHandler`. On HTTP/2 a
> handler may answer after returning and stream the response body (DATA
> frames, no content-length needed; nghttp2 data provider DEFERRED/resumed),
> with per-stream backpressure (write() → false above
> `Limits::stream_high_water` , default 256 KB; `setonWritable`), abort on
> client RST_STREAM or connection loss, 500 if the handler drops the stream
> unanswered, RST_STREAM if it drops it mid-body. On HTTP/1.1 the stream
> handler works if it finishes before returning (collected and sent); a
> deferred answer gets 500 until phase 2. The synchronous `setHttpHandler` is
> unchanged. Tests: 9 socket-free (`Http2StreamingTest`) + 2 end-to-end with
> curl (events of an SSE stream arrive ~100 ms apart, not batched; same
> handler on h2 and HTTP/1.1). NEXT: phase 2 (HTTP/1.1 chunked streaming),
> phase 3 (streamed request bodies), phase 4 (client streaming).
> Found while doing this: two HTTP/2 GOAWAY bugs, fixed - see §7.

## 1. Problem

Today a handler is `void(const HttpRequest&, HttpResponse*)`:

- the request body is fully buffered before the handler runs (capped by
  `setMaxBodySize`, default 8 MB);
- the response must be complete when the handler returns;
- the handler runs on the connection's loop thread, so any waiting (database,
  upstream call) blocks the loop. On HTTP/2 that stalls **every stream on the
  connection**, not just one request.

Mainstream libraries (Go `net/http`, Netty, h2o, nghttp2's asio server) all
let a handler (a) answer later and (b) stream either body. Without that Cox
cannot do large transfers within a memory budget, server-sent events,
long-polling, or gRPC.

## 2.Goals and non-goals

Goals:

- A handler may respond **after returning** (async), from the loop thread.
- A handler may write the response body **in pieces**, with backpressure, and
  an unknown total length (HTTP/1.1 chunked; HTTP/2 DATA frames).
- A handler may consume the request body **in pieces** as it arrives, with
  flow control so a slow consumer does not buffer an unbounded upload.
- The handler learns when the client goes away (connection closed, h2
  RST_STREAM), so it can stop work.
- **The existing synchronous handler keeps working unchanged** (22 call sites
  in tests/examples, plus users). It becomes a thin adapter over the new API.
- Same API for HTTP/1.1 and HTTP/2, plain and TLS.

Non-goals (this phase): trailers (roadmap #3, builds on this), client-side
streaming (phase 3 below), running handlers on other threads (the app
marshals back with `runInLoop`, as everywhere else in Cox).

## 3. API

```cpp
// http/http_stream.h
class ServerStream {           // one request/response exchange
public:
     const HttpRequest& request() const;   // method, path, headers (body may
                                           // be empty when streaming it in
     // ---- response ----
     void respond (const HttpResponse& resp);       // whole response at once
     void writeHead (int status, Headers headers);  // start a streamed response
     bool write(std::string_view data);             // append body bytes; returns
                                                    // false above the high-water
                                                    // mark (still accepted)
     void end();                                    // finish the body
     void setonWritable(std::function<void()>);    // fires when write() may 
     									  // continue after •false
     
     //---- request body (opt-in streaming) ----
     void onData(std::function<void(std::string_view)>);  // body chunks
     void onEnd (std::function<void ()>);                 // body complete
     void pauseBody (); void resumeBody();                // backpressure
     
     // ---- lifetime ----
     void onAbort(std::function<void()>);   // client went away; stop work bool aborted () const;
     reactor::EventLoop* loop() const;      // the thread all calls must use
};
using ServerStreamptr = std::shared_ptr<ServerStream>;

// HttpServer
using StreamHandler = std::function<void(const ServerStreamPtr&)>;
void setStreamHandler(StreamHandler h);
// Request-body mode: buffered (default; the body is in request() bef
// handler runs, as today) or streamed (handler runs after headers; br
// onData/onEnd). Per server for now.

void setrequestBodyMode(BodyMode m); // kBuffered | kstreamed
```

The existing sethttpHandler(sync)* is implemented as:

```cpp
setStreamHandler([h](const ServerStreamPtr& s) {
     HttpResponse resp(...);
     h(s->request(), &resp);
     s->respond (resp);
});
```

### Contract

- All `Serverstream` calls happen on `loop()`. From another thread, use
  `stream->loop()->runInLoop(...)`. The stream holds no locks.
- Exactly one response per stream: `respond()` once, or `writeHead()`
  +`write()`* + `end()`. A second response, or `write()` before `writeHead()`,
  is a programming error: logged, ignored.
- Keeping a `ServerStreamPtr` alive keeps the exchange alive. If the handle
  drops every reference without responding, the server sends 500 (so a
  forgotten response cannot hang a client).
- After `onAbort` fires, writes are ignored. The callback fires at most once
  and not at all if the exchange completes normally.

## 4. How it maps to each protocol

### HTTP/1.1

- `writeHead()` with a Content-Length header: sent as-is, body written raw.
  Without one: `Transfer-Encoding: chunked`, each `write()` is a chunk, `end()`
  writes `0\r\n\r\n`. (HTTP/1.0 client: no chunked; close-delimited body.) 
- One exchange at a time per connection, as now. **Pipelining:** the next
  pipelined request is not dispatched until the current response has
  `end()`ed; its bytes stay buffered (bounded by the TCP input-buffer cap).
- Backpressure: `write()` returns false when the connection's output buffer
  is above the high-water mark; `onWritable` fires on write-complete.
- Streamed request body: the parser hands body bytes to `onData` instead of
  accumulating them. `pauseBody()` stops reading the socket
  (`TopConnection::stopRead`), so TCP flow control pushes back on the client.
- Abort: the connection closes before `end()`.

### HTTP/2

- `writeHead()` → HEADERS; `write()` → appended to the stream's outgoing
  buffer; nghttp2's data-source callback reads from it and returns
  `NGHTTP2_ERR_DEFERRED` when it is empty and not ended,
  `nghttp2_session_resume_data()` is called on the next `write()`/end()`.
- Backpressure: per stream, `write()` returns false when that stream's
  buffered bytes exceed the high-water mark (the peer's flow-control window
  stops nghttp2 from draining it).
- Streamed request body: DATA chunks go to `onData`. Flow control is manual
  in this made not (`nghttp2_option_set_auto_window_update`) window is
  re-opened with `nghttp2_session_consume()` as `onData` returns, and not
  while paused, so the HTTP/2 window pushes back on the client.
- Abort: RST_STREAM from the client, or connection loss.
- Multiplexing: other streams keep flowing while one handler is pending.

## 5. Phases

1. **Server, HTTP/2** (first: the motivating case). `Serverstream`,
   `setStreamHandler`, async + streamed responses + abort, buffered request
   bodies. Sync handler becomes the adapter.
2. **Server, HTTP/1.1** - same API on the HTTP/1.1 path, chunked responses,
   pipelining held behind an unfinished response.
3. **Streamed request bodies** on both, with flow control.
4. **client streaming** - `Http2Client` response body chunks / streamed
   uploads; `Httpclient` likewise.

## 6.Tests per phase

- Socket-tree (`Httpsession` piped to an ngnttp cient): async response
  after the handler returned; streamed body across many DATA frames with no
  Content-Length; a slow stream does not delay another stream; handler drops
  the stream → 500; client RST_STREAM → `onAbort`; write backpressure.
- End-to-end with curl: SSE-style incremental response is received before
  `end()`; large streamed download; HTTP/1.1 chunked (phase 2).
- All existing HTTP tests must pass unchanged (the sync adapter).
- ASan/TSan over all of it (stream lifetime is the risk).

## 7. Findings during phase 1

**GOAWAY sent too early (duplicated requests)** (troubleshooting/http2-early-final-goaway-duplicate-requests.md).
`Http2Session::countRequest`
called `shutdown()` whenever the request cap was reached, i.e. on every
request past it. The first call sends the GOAWAY notice; the second turned it
into the FINAL GOAWAY. Several requests can complete inside one `feed()`, so
the final GoAWAY was submitted while responses for streams at or below its
`last_stream_id` were still queued behind it. The client then saw "server
will not process stream N" before stream N's answer, retried it, and the
server ran it twice: the burst test showed 44-91 handler runs for 40
requests. This existed before streaming (it reproduced on the committed
code); making `flush()` wait until `mem_recv` returns made it consistent
instead of rare. Fix: the request cap and the stream-ID limit only ever
start the drain (notice); the final GoAwAY is the owner's notice timer, after
the notice reached the peer. The burst test's old "needs >= 2 connections"
assertion only held because of this bug; it now accepts one connection, and
a deterministic test (`InFlightAcrossGoAwayRetriedOnNewConnection`) covers
the retry path instead.

**Self-referencing callbacks leaked streams** (troubleshooting/serverstream-self-reference-leak.md). Handlers naturally capture
their own `Serverstreamptr` in `onWritable`/`onAbort` (to keep writing).
`ServerStream` now drops its callbacks when the exchange ends (`end()`
`respond()`, abort), breaking that cycle. A timer closure that captures
itself strongly is a separate leak in user code; the SSE test shows the weak
pattern.

**nghttp2 re-entrancy.** Handlers may answer synchronously from inside
`nghttp_session_mem_recv` callbakcks , and `onWritable` fires from the data
provider inside `mem_send`. `flush`() is a no-op while receiving (the flush
after `mem_recv` sends everything), and writable notifications are deferred
until `mem_send` returns.