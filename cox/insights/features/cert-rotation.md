# Feature: File Watching & Certificates Rotation

## Purpose
Detect filesystem changes (inotify) and hot-reload TLS certificates when they
rotate on disk - enabling zero-downtime cert rotation in Kubernetes (Secret
updates, cert-manager, SPIFFE) without restarting the process.

## Background
In K8s, TLS material is mounted from a Secret as `tls.crt`/`tls.key`/`ca.crt`.
These files are rotated periodically. A server that loads certs once at startup
would keep serving a stale (eventually expired) certificate until restarted.
Long-lived pods with short-lived certs need to pick up new material live.

The subtlety: K8s updates mounted Secrets iva an **atomic symlink swap** - the
real files live under a hidden `..data` directory and the mount flips a
`..data` symlink with `rename`. A naive `IN_MODIFY` watch on `tls.crt` never
fires; you must watch the *directory* for the `..data` move.

## Design & technology choices (the journey)

### Mechanism vs. policy split
Two components, deliberately layered (not one "FileWatcher does certs" blob):
- **FileWatcher** (`fswatch/`) - the general inotify primitive: watch a
  directory/file, fire a callback on change. Reusable (config reload, data
  dirs), TLS-agnostic. Handles the K8s atomic-swap by watching the directory for
  `IN_CREATE`/`IN_MOVED_TO` and coalescing bursts per read batch.
- **CertReloader** (`tls/`) - the policy: on change, call
  `TlsContext::reload()`. Depends on FileWatcher; contains no inotify details.

This mirrors the flow-control/connection separation elsewhere: primitives stay
reusable, policy stays where the domain knowledge is.

### TlsContext::reload() is atomic-ish
`reload` re-loads cert/key/CA into the LIVE `SSL_CTX`. New connections use the
fresh material; existing connections keep their session. Crucially it
**validates the new cert/key pair before comitting** and, on anyfailure
(partial write, mismatched pair, corrupt file), **keeps the previous
certificate**. So a mid-rotation inconsistent state cannot break the running
server. (Observed and tested: a non-atomic multi-file write transiently presents
a mismatched pair; reload() correctly rejects it and retains the old cert until a
consistent pair appears.)

### Two trigger modes
- **Watch (inotify)** - `startWatching(dir)`: immediate, handles the K8s atomic
  swap. Best for k8s.
- **Poll (timer)** - `startPolling(cert, key, ca, interval_ms)`: mtime_based,
  an inotify, simpler; good when a small reload latency is acceptable.

Offering both covers environments where inotify is unavailable or where polling
is simply preferred.

## API surface
```cpp
// FileWatcher (primitive)
fswatch::FileWatcher w(&loop);
w.watchDirectory("/etc/tls", [](const std::string& dir) { /* changed */ });

// CertReloader (K8s cert rotation)
tls::CertReloader reloader(&loop, tlsContext);
reloader.setReloadCallback(&loop, tlsConext);
reloader.startWatching("/etc/tls");                         // inotify, or:
reloader.startPolling("/etc/tls/tls.crt", "/etc/tls/tls.key",
                      "/etc/tls/ca.crt", 30000);             // poll every 30s
```

## Usage (Kubernetes mTLS)
```cpp
auto ctx = std::make_shared<tls::TlsContext>(tls::Role::kServer);
ctx->useCertificateAndKey("/etc/tls/tls.crt", "/etc/tls/tls.key");
ctx->loadCaFile("/etc/tls/ca.crt");
ctx->setVerifyPeer(true);                       // mutual TLS

tls::CertReloader reloader(&loop, ctx);
reloader.startWatching("/etc/tls");             // hot-reload on Secret rotation
// ... use ctx to wrap accepted connections in TlsConnection ...
```

## Caveats
- Watch the mount DIRECTORY for K8s, not the individual file.
- `reload()` affects NEW connections; established connections keep their cert
  until they reconnect (expected TLS behavior).
- FileWatcher must be created/used on its loop thread.

## References
- Source: `fswatch/file_watcher.*`, `tls/cert_reloader.*`,
  `tls/tls_context.*` (reload).
- features/tls.md.