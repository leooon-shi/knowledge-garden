K!-- performance/network/network_io_model.md
eomodel.md...
# Network I/O Models
How Linux delivers 1/0-readiness notifications, the multiplexing APts, and the process/thread models that build on them.
#* Event notification: level-triggered vs, edge-triggered
-   **Level-triggered (LT):** you are notified as long as the file descriptor can perform non-blocking I/O. The application can check the FD's state at any time and then do I/0. Concretely: notified while the read buffer is non-empty (readable) or the write buffer is not full (writable).
-   **Edge-triggered (ET):** you are notified only when the FD's state "changes* (i.e. new I/0 arrives) - exactly once. The application must then do as much I/o as possible until it can read/write no more. If you don't drain it, the notification is lost. Concretely: notified when the read buffer goes from empty to non-empty (new data) or the write buffer goes from full to having space.
#* I/o multiplexing
##* Non-blocking 1/0 with level-triggered notification: select / poll
"select and "poll scan a list of FDs to find which are ready, then perform the actual non-blocking I/O. Because the I/0 is non-blocking, a single thread can monitor many socket FDs at once - single-threaded handling of many requests.
-   **Advantages:** simple, application-friendly API.
*   w*Disadvantages：**
-   Must poll the entire FD list each call → 0(n), costly with many FDs.
-   select uses a fixed-length bitmap for the FD set, so there is a maximum FD limit - on 32-bit systems the default is 1024 ("FD_SETSIZE*). "poll" removes the hard cap but is still o(n).
-   Each call copies the FD set from user space to kernel space and back, which is expensive.
### Non-blocking I/0 with edge-triggered notification: epoll
-   **Advantages:**
-   epoll manages the FD set in the kernel with a red-black tree, so the app does not pass the whole set on every call.
-   It is event-driven: it reports only FDs with pending I/0 events, with no need to scan the whole set → o(1) amortized readiness retrieval.
-   **Disadvantages:**
-   With edge-triggered mode, the app must drain each ready FD fully and handle more edge cases (eg. "EAGAIN®), making it harder to get right.
### Asynchronous 1/0
Asynchronous I/0 lets the application issue many I/0 operations without waiting for completion; the kernel notifies it (via signal or callback) when each completes, and only then does the app read the result.
- **Disadvantage:** historically hard to use. POSIX AIO (librt) was limited (effectively thread-pool-backed for sockets) and rarely used for networking.
**Update - io_uring.** Since kernel 5.1 (mature from 5.10+), "io_uring" provides a true asynchronous, batched, low-syscall interface that supports socket operations (accept,
"connect,
"send /rec, sendmsg /recumsg, and
multishot varíants). For new high-concurrency designs it is the modern alternative to both POSIX AIO and much epoll boilerplate, reducing syscall overhead via shared submission/completion ring buffers. (See
* C10k_c10M problem.md".)
## Process / thread models built on these APIs a# 1. Master process + multiple worker children (most common)
-   The master runs •bind() + "listen(), then forks several children.
-   Each child handles the same socket via 'accept) on epoll wait()'.
Classic example: Nginx - a master plus multiple worker processes. The master initializes sockets and manages worker 11fecycles; workers do the real request handling.
*  *Thundering herd,** When an I/0 event occurs, multiple processes blocked in
*  accept) /epol1_walt) may all wake, but only one actually handles the event; the rest go back to sleep, wasting work,
-   The accept() thundering-herd problem was fixed in Linux 2.6.
-   The epol1 wait() thundering-herd problem was addressed in Linux 4.5 via the
*EPOLLEXCLUSIVE flag.
To avoid it historically, Nginx added a global lock (accept_mutex") per worker: only the worker that wins the lock joins epoll, ensuring a single worker wakes. (with "EPOLLEXCLUSIVE" / SO_REUSEPORT, accept_mutex" 1s often unnecessary on modern kernels.)
*  *Threads instead of processes:** a main thread handles socket init and worker state; worker threads handle requests. Because thread switching is cheaper, you can even keep
*  epoll wait() in the main thread so each event wakes only the
main thread, with worker threads doing follow-up processing.


	- **Client side with a single IP:** the connection count is bounded by the
	ephemeral port range (-65536 max, in practice "ip_local port _range").
	**Server side with a single IP:** the connection count can be very large,
	because each distinct (client IP + client port) is a different tuple against
	
All processes listen on the same port with SO_REUSEPORT enabled Linux 3.4
The kernel load-balances incoming connections across the listeners by hashing the 4-tuple, so each connection goes to exactly one process.
Because the kernel wakes exactly one process, there is no thundering herd. This is the preferred model for modern multi-core servers.
## Fundamentals recap
-**LT:* notified whenever the FD is ready (read buffer non-empty / write buffer has room). Matches "notified as long as the FD can do non-blocking 1/0."
**ET:**notified once on each state change (empty-non-empty read, ful1-has-room write). Matches "notified only when the FD state changes."
Classic choices: **Nginx uses epoll + ET**; **Redis uses epoll + LT**,
## Connections and the 5-tuple
Linux identifies a connection by a **5-tuple**: protocol, source IP, source port, destination IP, destination port.
the same server IP: port.
## Troubleshooting / design workflow
1. Decide the concurrency target (C10K vs. C1eM) - see
* c10k_c1eM problem.md •
1.    Pick the readiness mechanism: epoll for most servers; io_uring for new, syscall-sensitive designs.
2.    Pick the worker model: *SO_REUSEPORT per-core listeners to avoid a shared accept bottleneck and thundering herd.
3.    If using ET, verify every ready FD is drained until EAGAIN.
4.    Measure: syscalls per request (strace -c"), wakeups, and CPU per core.
## Pitfalls
-   **ET without full draining** loses events and stalls connections.
-   **select's 1024 FD cap** silently corrupts memory if you exceed FD_SETSIZE".
-   **accept_mutex vs. EPOLLEXCLUSIVE/SO_REUSEPORT:** mixing old and new mechanisms can hurt latency; on modern kernels prefer SO_REUSEPORT.
-   Copying FD sets each select/ poll' call dominates cost at high FD counts.
## Likely interview questions
-   Compare select, poll, and epoll in time complexity and memory model.
-   LT vs. ET - correctness requirements and why Nginx chose ET.
-   What is the thundering-herd problem and how do 'accept mutex", | "EPOLLEXCLUSIVE, and "S0_REUSEPORT® each address it?
-   Where does io_uring fit relative to epoll and POSIX AIO?
-   Why can a server hold far more connections than a single-IP client?
## References
- Course material; Geek Time, "Linux Performance Optimization in Practice".



