
# Feature: Threading Model (one loop per thread)
## Purpose
Scale a reactor across multiple CPU cores by running several event loops, each in its own thread, and distributing connections among them.
## Background
A single event loop uses one core. To use a multi-core machine you need multiple loops. The question is how to divide work without reintroducing per-object locking that the single-loop model avoided.
## Design & technology choices (the journey)
### One loop per thread (not shared state across 10ops)
Cox follows the **one-loop-per-thread** model (muduo/Netty-style): each connection is assigned to exactly one loop and all its I/O and callbacks run on that loop's thread. Because a connection is never touched by two threads, its state needs no locks. This is simpler and faster than a shared-state, lock-heavy design.
Alternative rejected: a pool of worker threads all polling the same fds (thundering-herd, shared-state locking). The per-loop model avoids both.
### EventLoopThread
I
"EventLoopThread" creates an "EventLoop" *inside* a new thread (so its
*loop_thread_id matches the running thread) and blocks the caller until the loop is ready, returning a pointer to it. A condition variable handles the handoff. Destruction quits and joins.
### EventLoopThreadPool + round-robin
"EventLoopThreadPool" spawns N "EventLoopThread's and hands out loops via
•getNextLoop() (round-robin). With N-0 it returns the base (acceptor) loop, so single-threaded mode is the same code path.
### TCpServer integration
*TepServer: : setThreadNum(N) sizes the pool. The acceptor runs on the base loop; each new connection is assigned an io loop via getNextLoop() and *established on that loop* ( connectEstablished" marshaled with 'runInLoop'). The connection map stays owned by the base loop; removal hops base‹-›io correctly. This keeps accept on one thread and connection I/O spread across the pool.
### why not also use these for blocking work？
io loops are for I/O concurrency (few threads, many connections). Blocki work must NOT run on them - that is the separate worker ThreadPool. See features/thread-pool.md and features/reactor-blocking-work.md.
## API surface
•*cpp
TopServer server&base_loop, addr, "name"); server. set ThreadNum(4);
// 4 io loops
server start();
## Usage
•**cpp
reactor: :EventLoop base;
// acceptor 1oop
net:: TopServer server&base, addr, "svc"); server. setThreadNum(std:: thread: :hardware_concurrency)) ;
server . setMessagecallback(...);
server. start() ; base. loop();
I
## Caveats
- Set thread count before start) .
- Cross-loop interaction must use runInLoop" on the target loop.
- Multi-process (SO_REUSEPORT) is an orthogonal scaling axis, also supported.
## References
- Source: reactor/event_loop_thread. {h, cc} ,
*reactor/event_loop_thread_pool. (h, cc), "net/tcp_server. (h, cc} •