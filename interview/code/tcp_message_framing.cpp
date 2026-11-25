Runnable examples for "tcp_message_framing-md".
11
11
B1: varint (LEB128) length prefix
B2: length-of-length (nlen + length)
A : fixed u32 big-endian length prefix
(Protobuf-style)
(ASN.1 BER/DER-style)
(used by the mixed-scheme demos)
1/
1/
11
Design 1: fixed 1-byte tag selects A/B1/B2 per message on one connection
Design 2: message TYPE implies framing on one connection
Non-blocking epoll parser: a per-connection acumulation buffer (FrameParser) that yields complete frames from raw recv() chunks, driven by a real edge-triggered epoll loop that drains until EAGAIN (the ET discipline).
11
Adversarial tests: oversized length, malformed varint, unknown tag, mid-frame EOF - proving the guards fire.
//
// Blocking schemes are exercised over a real socketpair() with the sender
// dribbling bytes ONE ATA TIME, forcing genuine partial reads across many
// recv() calls - exactly the real-world hazard the doc warns about.
g++-std=c++20-wall -Nextra-pedantic-pthread tcp_message_framing.cpp -0 framing &&./framing
// clang++-std=c++20-Wall-Wextra-pedantic-pthread tcp_message_framing.cpp-o framing &&./framing
// Linux-only (epoll). Exit status e => all assertions passed.
#include ‹cassert>
#include ‹cerrno› #include ‹chrono>
#include <algorithm›
#include ‹cstdint› #include ‹cstring›
#include <iostream› #include <optional›
#include ‹string>
#include <thread› #include ‹vector›
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h›
namespace
constexpr std: suint64_t MAX_MSG_SIZE - 16 * 1024 * 1024;
constexpr int MAX_VARINT_BYTES = 10;
constexpr int MAX NLEN = 8:
// 16 MB guard
// ceil(64/7)
1/ 64-bit length
// read_full: read EXACTLY n bytes, looping over partial reads and EINTR.
Returns e on success, -1 on EOF or error. This is the piece people omit.
int read full(int fd, void* buf, std::size_tn) (
auto*
p = static_cast<std: :uint8_t*>(buf);
std:: size_t got
= 0;
while (got < n) {
ssize_t r = ::recv(fd, p + got, n - got, e);
if (r== 0)
return -1;
// orderly peer close mid-frame => truncated
if (r < 0)
if (errno == EINTR) continue; return -1;
// retry
// real error
got += static_cast<std: :size_t>(r);
0;
int write_full(int fd, const void* buf,
std: :size_t n) {
const auto* p = static_cast<const std: ruint8_t*> (buf);
std::size t sent = 0;
while (sent < n) {
ssize_t w = ;:send(fd, p + sent, n - sent, e);
if (w < e)
{
if (errno == EINTR)
continue;
return -1;
sent += static_cast<std: :size_t> (w);
return 0;
B1: VARINT (LEB128) length prefix
// Encode a u64 as a base-128 varint into out.
void varint_encodestd::uint64_t value, std::vector<std:suint8_t>& out) (
while (value ›= 0x80) {
out.push_back(static_cast<std::uint8_t>((value & 8x7F) | 0x80)): // MSB set => more
value >=7;
out.push_back(static_cast<std:suint8_t>(value & 0x7F));
11 MSB clear => last

I/ Decode a base-128 varint from the socket, one byte at a time.
// Returns 8 on success; -1 on EOF/error or malformed (too many bytes) .
int read_varint(int fd, std: uint64_t* out) (
std::uint64_t result = e;
int shift = 8;
for (int 1 = e; i < MAX_VARINT_BYTES; +1) I
std::uint8tbyte;
if (read_full(fd, &byte, 1) 1= e) return -1;
result |- static_cast<std::uint64_t>(byte & 0x7F) « shift;
if ((byte & ex80) = e) (
// continuation bit clear => done
*out = result;
return e;
}
shift += 7;
return -1;
1/ malformed: varint too long
1/ Frame = varint(length) followed by the payload bytes.
int send_frame_varint(int fd, const std: istring& payload) (
std::vector<std::uint8_t>hdr;
varint_encode(payload.size(), hdr);
if (write_full(fd, hdr.data(), hdr.size()) |= e) return -1;
return write_full(fd, payload.data(), payload.size());
int rec frame_varint (int fd, std::string* out) {
std::uint64_t len = 0;
if (read_varint(fd, &len) |= e) return -1; if (len > MAX_MSG_SIZE) return -1;
// bounds check BEFORE allocating
out-›resize(len);
if (len & read_fullfd, out->data(), len) l= e) return -1;
return 0;
B2: LENGTH-OF-LENGTH (nlen byte, then nlen-byte big-endian length)
1/ Minimal-width big-endian encoding of length, prefixed by its byte count.
int send_frame_lol(int fd, const std::string& payload) {
std::uint64_t len = payload.size():
// Determine minimal number of bytes to represent len (at least 1).
std::uint8_t tmp[8];
int nlen
= 0;
if (len == 0)
tmp[e]
= 0;
nen = 1;
} else [
std::uint64_t v = len;
std::uint8_t rev[8];
while (v) { rev[nlen++] = static_cast<std::uint8_t>(v & exFF); v >>= 8; }
11 rev is little-endian; store big-endian into tmp for (int i = e; i ‹ nlen; ++1) tmp[i] = rev[nlen - 1 - i];
}
std:suint8_t nlen_byte = static_cast<std::uint8_t>(nlen);
if (write_full(fd, &nlen_byte, 1) != e) return -1;
if (write_ful1(fd, tmp, static_cast<std::size_t>(nen)) = e) return -1;
return write_full(fd, payload.data(), payload.size());
int recy frame_lo1 (int fd, std: :string" out) {
std::uint8_t nlen = 0;
if (read_fullfd, &nlen, 1) = e)
return -1;
if (nlen == e Il nlen > MAX_NLEN)
return -1;
// reject absurd header width
std::uint8_t lenbuf[MAX_NLEN];
if (read fullfd, lenbuf, nlen) = e)
return -1;
std::uint64_t len = e;
for (int 1 = 0; 1 ‹ nlen; ++1) len = (len «< 8) | lenbuf[1]; // big-endian
// bounds check BEFORE allocating
if (len > MAX_MSG_SIZE) return -3; out-›resize (len);
if (len 8& read_fullfd, olt->data(), len) I=e) return -1;
return e;
A: fixed-size u32
big-endian length prefix (for the mixed-scheme demos)
--------------
int send_frame_aint
fd,
const std: :string& payload) {
std::uint32_t len - static_cast<std: suint32_t>(payload.size());
std::uint8_t be[4]
static_cast<std::uint8_t>((1en » 24) & exFF),
static_cast<std::uint8_t>((1en > 16) & OxFF),
static_cast<std::uint8_t>((1en >> 8) & OxFF),
static_cast<std::uint8_t>(len & OxFF),
if (write_ful1(fd, be, 4) != e) return -1;

return write_fullfd, payload.data, payload. size()):
int recy frame_aint fd, std: string" out) (
std: :uint8_t be[4]:
if (read_full(fd, be, 4) I- 0) return -1; std: suint64_t len - (static_cast<std: suint64_t>(be[0]) « 24)
(static_cast<std::uint64_t>(be[2]) « 8) |
static_castestd: suint64_t>(be[31):
if (len > MAX_MSG_SIZE) return -1; out->resize(len):
if (len && read_full(fd, out-›data(), len) l= e) return -1; return e;
// DESIGN 1: fixed 1-byte scheme TAG selects A / B1 / B2 per message.
// One uniform outer rule (read 1 tag byte), scheme chosen inside.
enum Scheme: std::uint&_t {SCHEME_A - 0x01, SCHEME_B1 = 0x02, SCHEME_B2 = 8x83 );
int send_tagged(int fd, Scheme scheme, const std: string& payload) ( std::uint8_t tag - static_cast<std::uint8_t>(scheme);
if (write_full(fd, &tag, 1) I- e) return -1;
switch (scheme) {
case SCHEME A:
return
send_frame_a(fd, payload):
case SCHEME_B1: return case SCHEME B2:
send_frame_varint(fd, payload);
｝
return send_frame_1o1(fd, payload):
return -1;
// Reader reads the tag FIRST, then dispatches. Reports which scheme it saw.
int rec_tagged (int fd, std::string* out, Scheme* scheme_out) {
std::uint8_t tag;
if (read_full(fd, &tag, 1) |= e) return -1; switch (tag)
case SCHEME A:
*scheme_out = SCHEME A;
return recv_frame_afd, out);
case SCHEME_B1:
*scheme_out SCHEME_B1; return recv_frame_varint (fd, out);
case SCHEME_B2:
*scheme_out - SCHEME_B2; return recv_frame_lol(fd, out);
default:
return -1;
// unknown tag => protocol error (closed set)
｝
DESIGN 2: message TYPE implies framing. A 2-byte big-endian type is read first;
the type's schema fixes the framing rule (compiled into both ends).
enum MsgType : std::uint16_t
TYPE_HEARTBEAT
= 0x0001,
TYPE_CONTROL
= 0x0002,
TYPE_BULK_DATA
= 0x0003,
// fixed e-length, NO length field at all
// always tiny => varint length (B1)
// always large => u32 length
(A)
// Per-type size guards: a CONTROL message claiming megabytes is a reject.
constexpr std: :uint64_t MAX_CONTROL_SIZE - 4096;
intendypedinto stype type, const st: string payload i
std::uint8_t t[2] - (static_cast<std: suint8_t>(type » 8) & OxFF),
static_cast<std: suint8_t>(type & OxFF)):
if (write_full(fd, t, 2) I- 6) return -1;
switch (type) {
case TYPE_HEARTBEAT: return e;
// no length, no body
case TYPE_CONTROL:
return send_frame_varint(fd, payload);
case TYPE_BULK_DATA; return send_frame_a(fd, payload);
｝
return -1;
I
int recy_typed (int fd, std: string" out, MsgType* type_out) (
std: uint8_t t[2];
if (read_ full(fd, t, 2) |= e) return -1; std::uint16_t type - static_castestd: suint16_t>(t[0] << 8) | t(3]);
switch (type) {
case TYPE_HEARTBEAT:
*type_out - TYPE_HEARTBEAT; out->clear);
return e;
case TYPE_CONTROL: 1
*type_out = TYPE_CONTROL;
if (recv_frame_varint(fd, out) |- e) return -1; if (out-›size() > MAX_CONTROL_SIZE) return -1;
I/per-type guard
return e;
case TYPE_BULK_DATA:
*type_out - TYPE_BULK_DATA; return rec_frame_afd, out);
default:
return -1;
// unknown type => protocol error

wing
sendFrameFn int int, const sta:: stringa);
using RecvFrameFn = int (*)(int, std::string*);
// Dribble a pre-framed byte buffer to fd ONE BYTE AT A TIME with a tiny delay,
// forcing the reader to loop across many rec() calls (genuine partial reads).
void dribble_bytes(int fd, const std: :vector«std::uint8_t>&framed) (
for byte byte: framed) [ if (write_full(fd, &byte, 1) |= e) return; std:: this_thread: :sleep_ for(std:: chrono: microseconds (5)):
1/ Capture the exact on-wire bytes a sender produces, by running it against a I/ throwaway socketpair and draining the result. template <typename Sendop>
std: :vector std: uint8_t> capture_framed (Sendop&& send_op) (
int sp[2]:
if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sp) |= e) { std::perror"socketpair"); return 0): } send_op(sp[1]):
::shutdown (sp[1], SHUT_NR);
std::vector<std::uint8_t>framed;
std::uint8_t b;
while (ssrecv(sp[®], &b, 1, e) == 1) framed.push_back(b);
::close(sp[0]):
::close (sp[1]); return framed;
void drip send(int fd, const std: :vector<std::string>&msgs, SendFramen send_frame) {
for (const auto& m : msgs) {
auto framed - capture_framed([&] (int wfd) {send_frame(wfd, m); 3);
dribble_bytes(fd, framed);
}
bool
run_scheme(const char* name, SendFramen send_frame, RecFramen rec_frame) (
const std:: vector<std::string> msgs = {
"hi", std::string(127, std: :string(128,
*  a'),
*  b'),
std::string(300,
°c°),
"the quick brown fox jumps over"
// empty payload (edge case)
// tiny => 1-byte length header
// varint boundary: fits in 1 byte
// varint boundary: needs 2 bytes
// multi-byte length in both schemes
/ arbitrary text
int fds[2];
if (::socketpair(AF_UNIX, SOCK_STREAM, e, fds) |= e) { std::perror("socketpair"); return false; } int reader - fds[0], writer - fds[1];
std:: thread sender[&] {
drip_send(writer, msgs, send_frame);
::shutdown (writer, SHUT_WR);
3);
bool ok = true;
for (const auto& expected : msgs) {
std:: string got;
if recy_frame reader,
&got) != 0) ٤
std: :cout «<
« name « "] FAIL: recv error\n";
ok = false;
break;
if (got !-expected) i
std：：cout くく
[" « name ‹< "] FAIL: mismatch (len " « got.size()
• vs expected " « expected.size() « ")\n";
ok = false;
break;
std: cout « " [" « name « "] ok: len=" « got.size() « "In":;
sender- join();
::close(reader);
::close (writer);
return ok;
// DESIGN 1 test: a MIX of tiny (B1/varint) and big (A/u32) messages on ONE
connection, each self-describing via its 1-byte tag. Proves A and B1 coexist.
bool run_mixed_tagged()
struct Item ( Scheme scheme; std::string payload; );
const std:: vector<Item> items - {
{SCHEME_B1,
"ping"),
{SCHEME_A, std:: string(5000, "X*)), {SCHEME_B1,
"ok"),
{SCHEME_B2,
std: string(400,
•z')},
{SCHEME_A,
std:: string(72000, "Y)),
{SCHEME_B1,
*"),
// tiny control -> varint
1/ big bulk
=> u32
// tiny
I/ standards interop
1/ big (> u16 range)
I/ tiny empty

Int
fds[2]:
if (::socketpair(AF_UNIX, 50CK_STREAM,, fds) 1=e) (std::perror("socketpair"); return false;
int reader = fds[0], writer = fds[a]:
std:: thread sender&] (
for (const auto& it: items) {
auto framed = capture_framed([&] (int wfd) { send tagged(wfd, it.scheme, it.payload): 1):
dribble_bytes(writer, framed);
:: shutdown(writer, SHUT_WR);
}):
bool ok = true;
const char* names[] - {"3", "A", "B1", "B2");
for (const auto& it : items) {
std::string got;
Scheme seen();
if (recv_ tagged(reader, &got, &seen) [= e) { std: :cout « " [tagged] FAIL: recv error\n"; ok = false; break; 1 if (seen l= it.scheme I got l- it.payload) (
std:: cout « " [tagged] FAIL: scheme/payload mismatchin"; ok - false; break:
}
std:: cout « " [tagged] ok: scheme=" « names[it.scheme] « " len=" « got.size() « "In":
sender. join():
:: close (reader); ::close(writer):
return ok:
// DESIGN 2 test: message TYPE implies framing (heartbeat=none, control-varint,
// bulk=u32), all on ONE connection. Proves type-driven dispatch.
bool
run_mixed_typed() (
struct Item {MsgType type; std::string payload; }:
const std::vector<Item> items = {
{TYPE_HEARTBEAT, {TYPE_CONTROL,
"set-rate 10"1,
(TYPE_BULK_DATA, std: :string(9000,
•D')),
// no length field at all
Il tiny => varint
// big
=> u32
{TYPE_HEARTBEAT,
""),
{TYPE_CONTROL,
"shutdown"),
{TYPE_BULK_DATA, std:: string(123,
int fds[2];
if (::socketpairAF_UNIX, SOCK_STREAM, e, fds) 1-) {std::perror("socketpair"); return false;
int reader = fds[e], writer - fds[1]:
std::thread sender([&] {
for (const auto& it : items) {
auto framed - capture_framed([&](int wfd) { send_typed(wfd, it.type, it.payload); 3);
dribble_bytes(writer, framed);
}
::shutdown (writer, SHUT_WR);
3);
bool ok - true;
for (const auto& it : items) {
std:: string got;
MsgType seen();
if (recv_ typed (reader, &got, &seen) |= •) { std:: cout « " [typed] FAIL: recv errorln"; ok - false; breaks 3
if (seen l- it.type |/ got I-it.payload) {
std: :cout «<
" [typed] FAIL: type/payload mismatch\n"; ok - false; break;
}
const char* tn= (it,type TYPE_HEARTBEAT) ? "HEARTBEAT" : (it.type =- TYPE_CONTROL)
3 "CONTROL (B1)"
: "BULK DATA(A)";
std::cout « " [typed] ok: type-" « tn « " len=" « got.size() « "In";
}
sender-join(); :: close(reader); ::close(writer);
return ok;
// NON-BLOCKING FRAME PARSER (Design B1/varint) + edge-triggered epoll loop.
1/
// In a non-blocking event loop you CANNOT block in readfull(). Instead you
/l append every rec() chunk to a per-connection buffer and parse out whole
// frames as they become available.
A partial header or partial body simply
/1 stays buffered until the next readable event. This class is that buffer.
//
class FrameParser ( public:
// Append raw bytes received from the socket.
void feed(const std: suint8_t* data, std::size_t n) ( buf_.insert(buf_.end(), data, data + n); }
// Try to extract ONE complete frame. Returns the payload if a full frame is

// Try to extract ONE complete frame. Returns the payload if a full frame is
// buffered; std: :nullopt if more bytes are needed. Sets error_
on malformed
// input (oversized length or malformed varint) - caller should drop the conn.
std: :optional<std::string> next) |
if (error_) return std: :nullopt;
// 1) Decode the varint length without consuming until we know it's complete.
std::uint64_t len = e;
int shift - e: std:: size_t 1 - 0; bool have_len - false;
for (1 ‹ buf_size() &&1&static_castestd:tsize_t>(MAX_VARINT_BYTES); +41)
std::uint8_t byte = buf [i];
len |- static_cast<std: :uint64_t>(byte & 0x7F) « shift; if ((byte & 0x80) == e) {have_len - true; ++1; break; shift += 7;
if (lhave_len) (
if (1 » static_castestd::size_t>(MAX_VARINT_BYTES)) (error - true; } // malformed return std: :nullopt;
// need more bytes (or errored)
}
if (len > MAX_MSG_SIZE) { error_ - true; return std::nullopt; } // bounds guard (/ 2) Do we have the whole body yet? std::size_t header_bytes - i;
if (buf_-size() - header_bytes < len) return std::nullopt; // partial body; wait 1/ 3) Extract the frame and erase consumed bytes from the front.
std::string payload(reinterpret_cast<const char*> (buf_-data()) + header_bytes, len); buf_-erase(buf_-begin(), buf_•begin() + header_bytes + len); return payload;
bool error(
const (return error;}
private:
std:: vector<std::uint8_t> buf_:
bool error - false;
static int set_nonblocking(int fd) (
int flags = ::fcntl(fd, F_GETFL, e):
if (flags < e) return -1; return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
// Drive the FrameParser from a real edge-triggered epoll loop over a
// non-blocking socketpair. The sender writes several varint frames back-to-back
1/ (and in chunks); the reader must drain until EAGAIN on every edge or it will / miss buffered bytes- the classic ET rule from epoll_edge_triggered.md.
bool run poll parser) {
const std::vector<std::string> msgs = {
"a", "bb", std::string(200,
c"), std: :string(5080, "d'), "*, "last",
int fds[2];
if (::socketpairAF_UNIX, SOCK_STREAM, e, fds) '= e) { std: :perror("socketpair"); return false; }
int reader - fds[0], writer = fds[1];
if (set_nonblocking(reader) - 0) { std: perror"fcntl); return false; )
// Sender: frame everything into one blob, then write in medium chunks so
/l frames straddle rec() boundaries. Runs on another thread.
std:: thread sender&] {
std:: vector<std: uint8_t blob;
for (const auto& m ; msgs) {
auto framed - capture_framed([&](int wfd) {send_frame_varint(wfd, m); });
blob.insertblob.end(), framed.begin(), framed.end()) :
}
std::size_t off = e;
while (off < blob.size()) (
std: :size_t chunk = std: :min<std::size_t>(97, blob.size() - off): I/ odd size on purpose
if (write_full(writer, blob,data() + off, chunk) i= 0) break; off += chunk;
std: this_thread: :sleep_for(std: chrono: microseconds (100)) :
}
::shutdown writer, SHUT_WR);
});
int ep = ::epol1_create1(0);
if (ep < 0) { std::perror("epo11_create1"); sender. join(); return false;) epoll_event ev):
ev.events = EPOLLIN | EPOLLET;
1/ EDGE -TRIGGERED
ev.data.fd = reader;
::epoll_ctlep, EPOLL_CTL_ADD, reader, dev);
FrameParser parser;
std:: vector<std:: string> got;
bool done = false, ok = true;
while (!done

epol_event out [8]:
int n= ::epoll_ waitep, out, 8, 5000);
// 5s safety timeout
if (n = e) { std: :cout « " [epoll] FAIL: timeout\n"; ok - false; break; ) if (n ‹ e) { if (errno == EINTR) continue; std: :perror("epoll wait"); ok - false; break; }
for (int k = 0; k < n && Idone; ++k) {
11 ET: must drain until EAGAIN, else buffered bytes are stranded. for (s:) +
std: :uint8_t tmp[64]:
ssize_t r•::rec(reader, tmp, sizeof tmp, e):
if (r> e) r
parser feed(tmp, static_cast‹std: :size_t>(r)):
} else if (r == e) {
done = true; break;
/ peer closed
} else {
if (errno = EAGAIN || errno = EWOULDBLOCK) break;
if (errno = EINTR)
continue;
std::perror("recv"); ok • false; done = true; break;
// drained
Parse out every complete frame currently buffered.
for (33) {
auto frame - parser.next():
if (Iframe) { if (parser. error()) { std: :cout « " [epoll] FAIL: parser error\n"; ok = false; done = true: 1 got-push_back(std: :move(*frame)) ;
sender-join(): ::close(reader):
::close (writer);
::close(ep);
if (ok)
if (got.size()
l= msgs.size()) {
std:: cout « "
[epol] FAIL: got " « got .size() « " of " « msgs.size() « " framesIn";
LepolL FAL: frame " « « " mismatchin"; ok - falses breaks 1
}
if (ok)
std::cout « " [epoll] ok: " « got.size() « " frames reassembled across recv boundaries\n";
return ok;
// ADVERSARIAL / GUARD TESTS: malformed input must be REJECTED, not crash.
// Each case feeds crafted bytes through a socketpair and expects rec to fail.
// Push raw bytes and close the write end, then run a decoder; expect failure.
template ‹typename RecOp> bool
expect_reject(const char* name, const std::vector<std::uint8_t>& raw, Recop&& recv_op) {
int fds[2];
if (3:50cketpair(AF_UNIX, SOCK_STREAM, 0, fds) '= 0) { std:: perror("socketpair"); return false; } if (fraw.empty()) write_full(fds[1], raw.data(), raw.size());
B:shutdown(fds[1], SHUT_WR;
std:: string out;
int rc = recv_op(fds[0], &out);
::close(fds[e]);
::close (Fds[1]);
bool rejected = (rc l= e);
std;: cout «< [guard]
« name «"; " « (rejected ? "rejected (ok)": "ACCEPTED (FAIL)") « "In";
return rejected;
bool run_adversarial() (
bool ok = true;
// 1) Oversized A length: 32 = 0xFFFFFFFF (> MAX_MSG_SIZE) must be rejected
BEFORE any allocation.
ok &- expect_reject"A oversized length, {0xFF,
(/ 2) Malformed varint: 184 continuation bytes with MSB always set,
std::vector<std::uint8_t> bad(MAX_VARINT BYTES + 2, 0x80);
ok &- expect_reject("B1 malformed varint", bad,
[l (int fd, std: istring* o) {return recy_frame_varint(fd, o); )):
11 3) Length-of-length with absurd nen (OxFF > MAX_NLEN) must be rejected. ok &- expect_reject("B2 absurd nlen", {exFF),
[](int fd, std: :string* o) (return recv_frame_1o1(fd, o); ));
114) Unknown scheme tag in Design 1 must be a protocol error.

/1 4) Unknown scheme tag in Design 1 must be a protocol error. ok &= expect reject ("D1 unknown tag", {0x7E, 8x00),
[int fd, stdistring* o) {Schemes; return rec_tagged(fd, o, &s); 1):
1/ 5) Mid-frame EOF: varint says 100 bytes, only 3 provided, then EOF.
std::vector<std: :uint8_t> trunc;
varint_encode(100, trunc);
trunc.push_back('x"); trunc.push_back(®y°); trunc.push_back('z'):
ok &- expect_reject("B1 mid-frame EOF", trunc,
[lint fd, std: string* o) {return recv_frame_varint (fd, o); 3);
6) CONTROL message exceeding its per-type cap (Design 2) must be rejected.
auto raw = capture_framed([&] (int wfd) {
send_typed (wfd, TYPE_CONTROL, std::string(MAX_CONTROL_STZE + 1, 'q')):
ok & expect_reject("D2 control over cap", raw,
[lint fd, std: :string* o) { MsgType t; return rec_typed(fd, o, &t); }):
return ok;
I namespace
int main() {
std::cout «"=- TCP framing with variable-length headers (partial reads forced) ==-\n*; std::cout « "B1 varint (LEB128) length prefix: \n";
bool b1 - run_scheme("varint", send_frame_varint, recv_frame_varint);
std:: cout « "B2 length-of-length (nlen + big-endian length): \n";
bool b2 = run_scheme ("len-of-len", send_frame_lol, recv_frame_lo1);
std;:cout & "Design 1 - fixed 1-byte tag selects A/B1/B2 (mixed on one conn): In*;
bool d1 = run_mixed_tagged ();
std;: cout « "Design 2 - message type implies framing (mixed on one conn): In*: bool dz= run_mixed_typed ();
std; cout "Non-blocking edge-triggered epoll parser (varint, chunked): \n";
bool ep = run_epoll parser();
std;: cout « Adversarial / guard tests (malformed input must be rejected): \n*:
bool ady = run_adversarial();
if (b1 8& b2 && d1 88 d2 88 ep 8& ady) {
std:: cout « "ALL PASS\n";
return e;
std:: cout « "FAILURES PRESENTAn";
return 1;
