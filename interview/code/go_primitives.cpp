
Prcuce.sh
// go_primitives.cpp
// Proof-of-concept: Go's Channel / Select / WaitGroup semantics in C++20.
17
// SCOPE (see report): correctness-first Poc using o5 threads (std: jthread).
// We do NOT implement goroutine (M:N) scheduling; each "goroutine" is a thread.
// Deviations from Go are deliberate and documented:
- rec() returns std:: optional<T> (nullopt == channel closed & drained),
11
1/
instead of Go's (zerovalue, ok-false).
-   Programmer errors (send-on-closed, double-close, negative WaitGroup)
- throw std: :logic_error instead of panicking.
-   select fairness: uniform-random choice among ready cases (matches Go's intent of preventing starvation), implemented via a readiness re-scan under a shared notifier. Simplified vs Go's per-case wait queues.
// Build:
g++ -std=c++20 -02 -Wall -Wextra -pthread go_primitives.cpp -0 gop
/1 TSan:
g++ -std-c++20-01 -g-fsanitize-thread -pthread go_primitives.cpp -0 gop_tsan
#include <atomic>
#include <condition_variable>
#include <cstdio> #include <deque>
#include <functional>
#include <memory> #include <mutex>
#include <optional>
#include <random>
#include <stdexcept>
#include <thread> #include <vector>
namespace go {
/ A shared notifier lets select wait on state changes from many channels.
struct Notifier std:: mutex m;
std:: condition_variable cv;
void notify() {
std:: lock_guard 1k(m); cv.notify_all();
// WaitGroup
class WaitGroup ( std: mutex m;
std::condition_variable cv;
int64_t count_ = e;
public:
void add(int64_t n) {
std::lock guard 1k(m_);
1f (count e) throu std :Logic erver ("HaitGroup: negattve counter"):
if (count_= e) cv_.notify_all();
void done() {add(-1); }
void wait() {
std: :unique_lock 1k(m_);
cv_-wait(1k, [&] { return count_ -= e; });
// Channel<T> : buffered (cap>0) or unbuffered/rendezvous (cap==0), closeable.
template <class T>
class Channel {
mutable std:mutex m std:: condition_variable not_full not_empty;
std:: dequesT> buf: std::size_t caps
bool closed_ - false;
// Optional select-notifier hooks (weakly held; set via register_notifler).
std::vector<std:: shared_ptr Notifier»> observers_;
void notify_observers_locked() {
for (auto& o; observers_) o->notify();
public:
explicit Channel(std::size_t capacity - e) : cap_(capacity) ()
void register_notifier(std:: shared_ptr<Notifier> n) (
std:: lock_guard 1km_);
observers_-push_back(std:: move(n));
}
void send (Tv) {
std::unique_lock 1k(m_);
const stdst effective_cap - std: max<std: size_t>(cap_, 1);

not_full_•wait(lk, [8] {return closed_ | buf_size() < effective_cap: }): if (closed_) throw std: :logic_error("send on closed channel"); buf_-push_back(std: move(v)) :
not_empty-notify_one): notify_observers_locked():
/l rendezvous: block until a receiver actually consumes the item. not_full_.wait(1k, [&] (return closed || buf empty): 7):
// Non-blocking send: true if delivered/queued, false if would block.
bool try send(Tv) {
std: unique_lock 1k(m):
if (closed_) throw std: :logic_error("send on closed channel"): const std::size_t effective_cap • std: :max<std::size_t>(cap, 1): if (buf_size() > effective_cap) return false;
if (cap_ 0) return false; //unbuffered try_send needs a waiting recv; keep simple buf_-push_back(std: :move(v));
not_ empty_•notify_one();
notify_observers_locked();
return true;
std: optional<T> rec() {
std: sunique_lock 1km_);
not_empty_-wait(1k, [&] {return closed I|Ibuf_•empty): :
if (buf_empty)) return std: :nullopt;
// closed & drained
Tv = std:: std::move(buf_.front()):
buf_-pop_front):
not_full_.notify_one():
// wakes blocked senders / rendezvous
notify_observers_locked ():
return v;
// Non-blocking recv: fills out and returns true if a value was available.
bool try_rec(std:: optional<T>& out) {
std::unique_lock 1k(m_);
if (Ibuf_-empty)) {
out = std:: move(buf_.front());
buf_-pop_front();
not_full_.notify_one();
notify_observers_locked():
return true;
if (closed_) {out = std:: nullopt; return true;} // closed counts as ready
return false;
bool ready_to_rec() const {
std::lock guard 1k(m_);
return closed_ 1| !buf_-empty();
}
void close) {
std::lock_guard 1k(m_);
if (closed) throw std: :logic_error"close of closed channel"); closed_ • true;
not_empty-notify_all();
not_full_-notify_all();
notify_observers_locked();
bool is_closed() const { std::lock_guard 1k(m_); return closed: 3
select: wait on multiple recy cases; uniform-random choice among ready.
/ Each case = {channel, handler (optional<T>)}. Optional default handler.
This Poc supports recv-cases (the common form) for clarity.
template <class T> struct RecCase
Channel<T>* ch;
std:: function<void(std: :optional<T>)> on_recv;
template <class T>
class Selector {
std:: vector<RecCase<T>> cases; std:: function<void()> default_;
bool has_default_
- false;
std:: shared_ptr<Notifier> notifler_ - std: make_shared Notifler» (); std::mt19937 rng_(std: :random_device?)C)!;
public:
Selector& rec(Channel<T>& ch, std:: function<void(std: :optional<T>)> h) (
ch. register_notifier(notifler_);
cases_-push_back({&ch, std:: move(h)});
return
*this;

return *this;
Selector& otherwise(std:: function<void()> h) {
default_ • std::move(h); has_default_- true; return *this;
void run(
for (و)
// Collect indices of ready cases.
std:: vectorsstd:: size_t> ready;
for (std: size_t 1 = 0; 1 < cases_,size(); ++1)
if (cases_[1].ch->ready_to_rec()) ready. push_back(1);
if (fready, empty)) /
// Uniform-random tiebreak (Go semantics: fair, anti-starvation).
std:: uniform_int_distributionsstd: size_t> d(0, ready,size() - 1); std:: size_t pick - ready[d(rng)];
std:: optional<T> v;
if (cases_[pick].ch->try_recv(v)) {
cases_[pick].on_recv(std: move(v)) ;
return;
continue; // raced away; re-scan if (has_default_) {default_(); return; }
// Block until some channel signals a state change, then re-scan.
std: unique_lock 1k(notifier_-m);
notifier_-→cv.walt_for(1k, std! chrono: mt 111seconds (50)):
｝；
} /l namespace go
// Demos / self-checks
static int g failures = 0;
#define CHECK(cond, msg) do { if (1(cond)) { std: printf ("FAIL: %s\n", msg); +g_failures; ) 1 while (0)
int main() {
using namespace go;
//
-  -- WaitGroup: wait returns after all Done --.WaitGroup wg;std:: atomickint> counter(0}; wg.add(5);std::vector<std::jthread> ts;for (int 1=e; 1 < 5; +1) wg-wait():ts. emplace_back[&] { counter. fetch_add(1); wg.done(); });CHECK (counter load() = 5, "WaitGroup did not wait for all workers");std: printf(" [WaitGroup] all %d workers done\n", counter.load()) :
-  -- Buffered channel: FIFO, producer/consumer ---Channel<int>ch(4) ;WaitGroup wg; wg.add(1);intSum=0；std:-jthread
consumer（［&］{
while (auto v = ch.recv()) sum += *v;
wg-done();
for (int i
- 1; i <= 10； +i）ch.send（i）；
ch.close():
wg.wait();
CHECK (sum
== 55，
"buffered channel sum mismatch");
std::printf("[Channel buffered] sum 1..10 - %d\n", sum);
｝
{
--- Unbuffered channel: rendezvous handshake
Channel<std: :string> che);
std::string got;
std:: ithread receiver([&] {if (auto v ch.recv()) got = *v; });
ch.send(std: :string("hello");
receiver-join);
CHECK (got ==
"hello",
"unbuffered rendezvous failed");
std::printf("[Channel unbuffered] received %s"In", got.c_str()):
}
{
-  -- Close semantics: rec drains then nullopt; send-on-closed throws ---Channel<int> ch(2);ch.send(1); ch.send (2); ch.close();auto a= ch.recy(); auto b = ch.recv(); auto c - ch.recv();CHECK (a && *a
*   1 8& b && *b -= 2 88 Ic, "close/drain semantics wrong");
bool threw = false;
try {ch.send(3); } catch (const std: :logic_error&)
{ threw - true; }
CHECK (threw,
"send on closed channel did not throw");
std::printf("[Channel close] drain ok, send-on-closed threw = %d\n", threw):
// --- Select: default when nothing ready; picks ready case otherwise --.
{
Channel<int> a(1), b(1); bool took_default - false;
Selector<int> ()
rec(a, [](std:: optional<int>)
rec(b, [](std: :optional<int>)
*  otherwise([&] { took_default - true; })
*   run();
CHECK (took_ default, "select default not taken when no case ready");
b. send(42):
int received = -1;
Selector<int>()
• rec(b, [&](std: :optional<int> v) { if
received -
CHECK(received == 42,
"select did not pick the ready case");
std::printf("[Select] default-path ok; ready-path received - %d\n", received);

// --- Select fairness: two always-ready channels chosen ~50/50 ---
const int N = 20000;
int ca = e, cb = 0;
for (int i = 0; i < N; ++1) { Channel<int> a(1), b(1); a.send(1); b.send (1);
Selector<int>()
recva, [&] (std::optional<int>) {++ca; })
- rec(b, [&](std: :optional<int>) {++cb; })
}
double ratio = double(ca) / N;
CHECK(ratio > 0.4 && ratio < 0.6, "select not fair (uniform random) "): std::printf("[Select fairness] a-% b-%d ratio-%.3f (expect ~8.5) \n", ca, cb, ratio);
std::printf(g_failures ? "\n%d CHECK(s) FAILED\n" : "nALL CHECKS PASSED\n", g_failures);
return g failures ? 1: 6;
}