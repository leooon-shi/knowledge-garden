
‹!-- cox/docs/insights/features/tls.md -->
# Feature: TLS >
## Design & technol
# Feature: TLS
## Purpose
Encrypt and authenticate connections using TS 1.2+/1.3 over the existing TCP layer, including mutual TS (mTS) for service-to-service authentication.
## Background
Secure services need TLS. The challenge in a non-blocking reactor: OpenSSL's socket BIOs assume blocking or their own I/O control, which conflicts with the event loop owning all I/O. The solution is memory BIOS - the loop does the socket I/0 and shuttles encrypted bytes into/out of openSSL.
## Design & technology choices （the journey）
### Memory-BIO architecture （1oop owns z/0）
The reactor must own all socket I/O, so cox drives OpenSSL with **memory BIOS**:
network (encrypted) --feedEncrypted--> rbio--> SSL --> readPlaintext (app) app plaintext --writeplaintext--> SSL --> wbio --takeEncrypted--> network
、、
The event loop reads/writes the socket (via TepConnection) and moves encrypted bytes in/out of openSSL; OpenS never touches the fd. "WANT_READ"/ WANT_WRITE" map to "need more network bytes"/"have bytes to flush".
##* Three-layer split (context / engine / connection)
- **T1scontext** - wraps SSL_CT: cert/key/CA loading, verification mode, protocol floor (TLS 1.2), shared by all connections.
- **T1sEngine** - the memory-BIO state machine, with NO sockets or loops. This isolation is deliberate: the tricky TS logic is unit-tested by piping two engines' encrypted output into each other, no networking required.
I
-**TIsConnection**- layers TS over anet:: TcpConnection by intercepting its byte stream. Because it composes (rather than forks) TcpConnection, TLS connections inherit all the hardening (timeouts, backpressure, drain).
Composition over inheritance/forking was chosen so TS doesn't duplicate - and doesn't drift from - the TCP connection lifecycle.
### Verification / mTLS
client verifies the server by default (system CA store). setverifyPeer(true) enables verification; on a server it additionally requires a client certificate 

(mutual TLS) - the model for in-cluster service-to-service auth.
## Design & technology ch
usecertificateAndkey(cert, key)" validates that the private key matches the certificate at load time, so a mismatched pair fails immediately rather than at the first handshake. This maps directly onto a Kubernetes "kubernetes. io/tls" Secret mounted as a volume (tls.crt"/ tls.key") plus a CA (ca.crt"): both peers load their own 'tls.crt/ tls.key", "loadcaFile("ca.crt")" the shared CA, and
• setverifypeer(true)" - the standard in-cluster mTS setup, supported today. Live rotation of that mounted material is covered in features/cert-rotationd.
### Hostname verification (client side)
Verifying the certificate chain proves only "signed by a trusted CA" - not "is the host I meant to reach"
• Without a name check, any valid certificate from any
trusted CA would pass, which defeats the point. So when a client is given a server hostname, Cox calls •SSL_set1_host()" so openssL checks the name against the certificate's SAN, with X509_CHECK_FLAG_NO_PARTIAL WILDCARDS" to reject sloppy wildcards like a*.example.com.
### SNI: one port, many certificates
ATLS server must choose a certificate *before* it has seen any application data, so it cannot use the HTTP "Host header. SNI (RFC 6066) solves this: the client puts the hostname it wants in the Clienthello, and the server picks accordingly. This is what makes virtual hosting, multi-tenant servers, and reverse proxies possible on a single port.
Server side: addsniContexthostname, ct)" registers a per-hostname
"TlsContext"
, and the context it is called on remains the **default**. Selection
happens in openSSL's "tlsext_servername callback, which swaps the connection onto the matched context with SSL_set_SSL_CT. That call does *not* carry bver options or verification mode, so both are re-applied from the chosen context - otherwise a per-host "setverifypeer" would be silently ignored.
Matching supports exact names and single-label wildcards (*.example.com matches api.example.com, but neither example.com nor a.b. example.com
• per
RFC 6125). Exact always beats wildcard, and matching is case-insensitive since DNS names are. The callback is installed lazily on first registration, so a server that never uses SNI pays nothing.
An **unknown hostname falls back to the default certificate rather than aborting 
the handshake**. Failing closed would turn any unrecognized name into a hard connection error, which is both hostile to debug and a trivial way to break clients; nginx's "default_server behaves the same way.
client side had to be built too - Cox previously never *sent* SNI, so server selection was untestable. "TlsEngine / TlsConnection"
hostname, send it via SSL_set_tisext
now accept a server
_host_name"
and reuse it for hostname
verification. IP literals are suppressed, since RFC 6066 §3 forbids an address in SNI. http::ConnectionPool" passes its host header through, so the HTTPS client and pools get SNI automatically.
#*# OCSP stapling: revocation without a client round trip
A certificate can be revoked before it expires, but checking that is awkward.
If the *client* queries the CA's OCSP responder it costs an extra round trip on the critical path, leaks the user's browsing to the CA, and usually **fails soft** - clients proceed when the responder is unreachable, making revocation checking largely decorative.
Stapling (RFC 6066 status
_request) inverts it: the **server** fetches the
signed "not revoked" proof and attaches it to the handshake. The cost moves from once-per-client to once-per-refresh, and the client gets the proof inside a handshake it was already doing.
"Ocspstapler" fetches **out of band** and caches the DER response; the handshake callback only copies cached bytes, so TS never blocks on the network - the same discipline as everything else in the reactor. It discovers the responder URL from the certificate's AIA extension (overridable for testing), and validates before caching: a non-successful response, a status that is not good, or one outside its validity window is rejected. Stapling a revoked" response wouldI actively tell clients to reject your own server, and a malformed staple is worse than none, so "cache only what is provably good" is the rule.
It **fails open**: with nothing cached the callback returns NOACK and the handshake proceeds unstapled. A responder outage must degrade stapling, not take the server down.
"nextupdate is OPTIONAL in RFC 6960 §4.2.1, so "secondsuntilExpiry() distinguishes three cases: › time remaining, ' expired or nothing cached, and " -1" cached but with no expiry hint. Collapsing the last two would make a 
perfectly valid response look stale. (OpenSSL's own responder omits "nextUpdate" unless started with "-nmin /-ndays"
, so this is the common case, not a corner.)
Two deployment shapes are supported: refresh) fetches over HTTP directly (blocking - call it from a worker thread or timer, never a loop thread), and
*loadFromFile() reads a DER written by an external agent such as certbot or a sidecar, which is what most production setups actually do.

## APT surface

```cpp
auto ct = std: make_shared<1sContext>(Role:: kServer /* or kclient */);
ctx->useCertificateAndkey("tls.crt"
ctx->loadcaFile("ca.crt");
" "tIs.key");
ctx->setverifypeer(true);
1/ mTLS
// SNI: extra certificates on the same port (server role only).
ctx->addSnicontext("api.example.com"
, api_ctx);
ctx->addSnicontext("*.internal.example.com", wildcard_ctx);
ctx->sniCount);
ctx-›findsniContext("api.example.com"); // nullptr use the default
// OCSP stapling (server role, publicly-trusted certs).
tls::Ocspstapler stapler("tls.crt", "issuer.crt");
stapler. setResponderurl("http://ocsp.example-ca.com"); // else read from AIA stapler.refresh();
// BLOCKING: worker thread or timer
stapler.LoadFromFile("resp.der");
I or load one an agent wrote
stapler. attachTo(*ctx);
// serve it on handshakes
stapler. secondsUntilExpiry);
I/ › valid, 0 expired, -1 no hint
stapler.hasResponse() ;
I
auto tc = std:: make_shared<1sConnection>(tcpConn, ctx, role);
// client: send SNI + verify the certificate against this name.
auto tc = std:: make_shared<T1sConnection>(tcpConn, ct, Role::kclient,
"api. example.com");
tc->setHandshakeDoneCallback(cb);
tc->setMessageCallback(cb);
tc-›send (plaintext);
tc->requestedServerName() ;
tc-›start) :
// decrypted plaintext
// encrypted to the wire
// server: the SNI name the peer sent
// begin handshake
```

```cpp
server. setConnectionCallback[ctx](const TcpConnectionPtr& conn) {
if (conn-›connected()) {
auto tc = std: make_shared<lsConnection>(conn, ctx, Role::kServer);
tc-›setMessagecallback(I](const TlsConnectionPtr& t, std::string& m) {
t-›send (m);
// encrypted echo
});
keepAlive(conn->name(), tc);
// hold the Tisconnection alive
tc-›start();
```

## ALPN (Application-Layer Protocol Negotiation, RFC 7301)
TlsContext:: setAlpnProtocols(f•..}) configures ALPN on both roles: a client advertises the list (preference order); a server uses it as its preference list and installs a selection callback that picks the first server-preferred protocol the client also offered ('SSL_select next_proto" - server preference wins, as nginx/nghttp2 do). After the handshake, "TlsEngine: :negotiatedprotocol() returns the agreed name (e.g. ""h2"" , ""http/1.1"") on both ends, or """ if
ALN was unused or the sides shared no protocol.
Fail-open by design: no shared protocol does NOT abort the handshake - it completes with no ALPN protocol selected, and the caller falls back (typically to HTTP/1.1). This is the negotiation hook a future HTTP/2 layer needs to route an "h2" connection to an HTTP/2 engine versus the HTTP/1.1 parser.
#* DTLS / UDP(not supported -and why)
Cox does not do DTS, and the reason is structural, not a backlog gap. DTLS is TLS over a datagram transport, but UDP was deliberately dropped from cox (see roadmap "Discarded"'): the library carries only stream transports (TCP + Unix domain). DTLS needs a datagram substrate, so it is blocked on the thing that was explicitly removed.
DTLS is also not "TLS with UDP plugged in". It re-adds, inside the TS layer, the reliability UDP lacks:
- **Handshake retransmission + timers** - datagrams can be lost or reordered, so
- DTLS has its own retransmission state machine for handshake messages.
- **Record sequence numbers + a replay window** - to tolerate reordering and reject replays.

- **Per-datagram records** - no stream framing; each record must fit a datagram，which brings PMTU concerns.
- **Cookie exchange ("HelloVerifyRequest" )** - a stateless cookie resists spoofed-source amplification DoS, since UDP has no connection to bind state to.
Cox's "TlsEngine" is built on memory BIos over a reliable, ordered, framed TCP byte stream. DTS would need a separate engine using DTLS_method() with datagram BIOs plus the retransmission/cookie machinery - a distinct subsystem, not a flag on the existing one.
Supporting it would therefore take two prerequisites, in order: (1) revive UDP as an independent Channel-based "UdpSocket"/datagram layer, and (2) build the datagram-BIO DTLS engine (retransmission timers tied into the "TimerQueue, cookie exchange, per-datagram records). That is comparable to the original TCP+TLS effort.
Decision: DTS is a conditional future item gated on a concrete datagram-security requirement (WebRT data channels, VPN, SIP/media, COAP/IoT), exactly as UDP is gated. For the service-to-service, RPC, backend, and edge-termination use cases Cox targets, it is not needed, and modern secure-datagram needs are increasingly served by QUIC (TLS 1.3 built in over UD) rather than DTS. Prefer QUIC, or do not build it.
## Caveats
- Hold the Tlsconnection" alive for the underlying connection's lifetime.
- **Session resumption / tickets** remains unimplemented (deferred with a trigger; see roadmap). TLS 1.3 already cuts the full handshake to 1-RTT and OpenSSL handles much of this by default, so the ROI is low for long-lived internal connections.
**SNI**: registration is server-role only and throws otherwise. An unknown hostname gets the default certificate rather than an error, so a misconfigured client sees a name-mismatch failure at verification time rather than a handshake abort. The SNI name is attacker-controlled - do not use it for authorization decisions.
- **OCSP stapling** only makes sense for publicly-trusted, cA-issued certificates served to public clients. It is pointless for private-CA, self-signed, or short-lived certs: there is no responder to ask, and a cert that expires in hours cannot meaningfully be revoked.
- "refresh() BLOCKS on network I/O. Never call it from an event-loop thread; use the worker "Threadpool or a timer.
I
- Nothing refreshes automatically - the caller schedules it. Use secondsUntilexpiry) and refresh well before it reaches .
- The nonce is sent but a mismatch is tolerated, since many public responders pre-sign and ignore nonces.
- Stapling is not combined with SNI-selected contexts: "attachto() installs the callback on one context, so a multi-certificate server needs a stapler per context.
- Certificate rotation: see features/cert-rotation.md. Note that reload() does not re-fetch an OSP response; refresh the staple after a rotation.
## References
- Source: "tls/tls_context.** (incI. SNI registry/callback), "tis/tls_engine.*-(incl. client SNI + hostname verification), "tls/tls_connection.*,
*tls/ocsp_stapler.*-
- Tests: *tests/tls_engine_test.cc,"tests/tls_integration_test.cc,"tests/sni_test.cc*, "tests/ocsp_test.cc*.
- features/cert-rotation.md, features/http.md (HTTPS client/pool SNI).
I

