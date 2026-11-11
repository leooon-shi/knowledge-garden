
# Troubleshooting: every HTTPS connection's server state leaked until shutdown
A worked record of a TS-layer bug found while adding HTTP/2 GOAWAY tests.
"TIsconnection replaced the TCP connection's callback instead of chaining onto it, so HttpServer never learned that a TLS connection had closed and kept its per-connection state (parser, buffers, TS objects) until the server itself was destroyed. It affected HTTPS on HTTP/1.1 as well, not only HTTP/2.
Kept because no existing test looked at what happens after a client disconnects.
## symptom
A new test, "HttpzIntegrationTest.ClosedConnectionsAreheieased, opens six short-lived HTTPS connections with curl (three HTTP/1.1, three HTTP/2) and then asks the server how many connections it is still tracking:
tracked
Which is: 6
(expected 0)
Every connection was still in Httpserver:: conns long after curl had exited. Plain HTTP (no TLS) was fine.
## Diagnosis path
#*# 1. Suspect the new HTTP/2 close path - WRONG
The GoAwAY work had just added new ways to close h2 connections, so the first guess was that h2 sessions were not marked finished. But the three HTTP/1.1 connections leaked too, and those do not touch the h2 code.
### 2. Follow the disconnect notification
"HttpServer releases a connection in its connection callback when
• connected() is false. That callback is installed on the TcpConnection" by Tepserver. Reading TlsConnection:: start() :
*** cpp
conn_-›setConnectionCallback([weak](const net:: TcpConnectionPtr& c) {
if (Ic-›connected()) { if (auto self = weak. lock()) self-›onTcpClose(); }
It **overwrote** the callback Tepserver" had installed. From then on, disconnects of TLS connections went only to "TisConnection"; "Httpserver
(and anything else on that callback, such as metrics) never heard of them.
## Root cause
A layer that wraps an object and installs its own callback on it must chain to whatever callback was there before. "TlsConnection" did not. The bug was in the committed code and affected every HTTPS server: under load the per-connection map grew with every connection ever accepted.
## Fix
"TopConnection gained connectioncallback()" (read the current callback), and Tisconnection: start() chains:
*** cpp
net: :ConnectionCallback prev = conn_->connectionCallback();
conn
_-›setConnectionCallback[weak, prev](const net::TcpConnectionPtr& c) {
if (Ic-connected)) {if auto self weaklock)) self-›onTcclose();}
if (prev) prev(c);
});
The weak capture is kept (a strong one would recreate the ownership cycle the LeakSanitizer work removed earlier). Verified: the test sees e tracked connections after all six closes;, HTTPS, TLS-pool and HTTP/2 tests pass under ASan/LSan and TSan.
## Takeaways
1. **Wrapping layers must chain callbacks, not replace them.** Any set×Callback on an object you did not create may already have an owner. Read it first and call it.
2.    **Test the end of a connection, not just the request.** "The response was correct" says nothing about whether per-connection state is freed.Assert the server's tracked-connection count goes back to zero.
3.    **Check whether the bug is protocol-specific before blaming new code.**The HTTP/1.1 connections leaking too pointed below HTTP/2 at once.

1.    **Wrapping layers must chain callbacks, not replace them.** Any setxcallback on an object you did not create may already have an owner. Read it first and call it.
2.    **Test the end of a connection, not just the request.** *The response was correct" says nothing about whether per-connection state is freed.Assert the server's tracked-connection count goes back to zero.
3.    **check whether the bug is protocol-specific before blaming new code.**The HTTP/1.1 connections leaking too pointed below HTTP/2 at once.
