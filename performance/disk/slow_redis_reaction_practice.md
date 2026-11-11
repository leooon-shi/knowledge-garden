<!-- pertormance/io/Slow_redis_reaction_practice.ma -->

# Case Study: Severe Redis Response Latency

A worked case study: a Redis-backed query interface shows severe, intermittent
response latency. The investigation traces the latency to synchronous disk
writes (`fatasync`) caused by an aggressive AOF persistence policy, compounded
by an application that issues writes on a read path.

## Symptom

A Python application calls Redis on a "query" endpoint, yet the host shows
steady disk writes and Redis responses are slow. A read-only endpoint should not
be driving disk writes, so the first job is to find what Redis is actually doing.

## Step 1: Trace Redis system calls with strace

```console
# -f follows child processes and threads, -T shows the duration of each
# syscall, -tt shows wall-clock timestamps
$ strace -f -T -tt -p 9085
[pid 9085] 14:20:16.826131 epoll_pwait(5, [{EPOLLIN, {u32=8, u64=8}}], 10128, 65, NULL, 8) = 1 <0.000055>
[pid 9085] 14:20:16.826301 read(8, "*2\r\n$3\r\nGET\r\n$41\r\nuuid:5b2e76cc-"..., 16384) = 61 <0.000071>
[pid 9085] 14:20:16.826477 read(3, 0x7fff366a5747, 1) = -1 EAGAIN (Resource temporarily unavailable) <0.000063>
[pid 9085] 14:20:16.826645 write(8, "$3\r\nbad\r\n", 9) = 9 <0.000173>
[pid 9085] 14:20:16.826907 epoll_pwait(5, [{EPOLLIN, {u32=8, u64=8}}], 10128, 65, NULL, 8) = 1 <0.000032>
[pid 9085] 14:20:16.827038 read(8, "*2\r\n$3\r\nGET\r\n$41\r\nuuld:55862ada-"..., 16384) = 61 <0.000044>
[pid 9085] 14:20:16.827149 read(3, 0x7fff366a5747, 1) = -1 EAGAIN (Resource temporarily unavailable) <0.000043>
[pid 9085] 14:20:16.827285 write(8, "$3\r\nbad\r\n", 9) = 9 <0.000141>
[pid 9085] 14:20:16.827514 epo11_pwait(5, [{EPOLLIN, {u32=8, u64=8}}], 10128, 64, NULL, 8) = 1 <0.000049>
[pid 9085] 14:20:16.827641 read(8, "*2\r\n$3\r\nGET\r\n$41\r\nuuid:53522968-"..., 16384) = 61 <0.000043>
[pid 9085] 14:20:16.827784 read(3, 0x7fff366a5747, 1) = -1 EAGAIN (Resource temporarily unavailable) <0.000034>
[pid 9085] 14:20:16.827945 write(8, "$4\r\ngood\r\n", 10) = 10 <0.000288>
[pid 9085] 14:28:16.828339 epoll_pwait(5, [{EPOLLIN, {u32=8, u64=8}}], 10128, 63, NULL, 8) = 1 <0.000057>
[pid 9085] 14:20:16.828486 read(8, "*3\r\n$4\r\nSADD\r\n$4\r\ngood\r\n$36\r\n535"..., 16384) = 67 <0.000040>
[pid 9085] 14:20:16.828623 read(3, 0x7fff366a5747, 1) = -1 EAGAIN (Resource temporarily unavailable) <0.000052>
[pid 9085] 14:28:16.828760 write(7, **3\r\n$4\r\nSADD\r\n$4\r\ngood\r\n$36\r\n535"..., 67) = 67 <0.000060>
[pid 9085] 14:20:16.828970 fdatasync(7) = 0 <0.005415>
[pid 9085] 14:20:16.834493 write(8, ":1\r\n", 4) = 4 <0.000250>
```

How to read it: the `<...>` at the end of each line is the syscall duration in
seconds. `epoll_pwait`, `read`, `write`, and `fatasync` are all frequent. Note
that `fdatasync(7)` takes `0.005415` s - far longer than the sub-millisecond
`read`/`write` calls. The disk writes observed earlier must come from `write`
or `fdatasync`.

## Step 2: Map file descriptors with lsof

``` console
$ lsof -p 9085
redis-ser 9085 systemd-network    3r     FIFO   0,12      0t0 15447970 pipe 
redis-ser 9085 systemd-network    4w     FIFO   0,12      0t0 15447970 pipe
redis-ser 9085 systemd-network    5u  a_inode   0,13        0    10179 [eventpoll]
redis-ser 9085 systemd-network    6u     sock    0,9      0t0 15447972 protocol: TCP
redis-ser 9085 systemd-network    7w      REG    8,1  8830146  2838532 /data/appendonly.aof
redis-ser 9085 systemd-network    8u     sock    0,9      0t0 15448709 protocol: TCP
```

Only FD 7, a regular file (`REG`), produces disk writes, and its path is
`/data/appendonly.aof`. The syscalls against it are `write` and `fdatasync`.
FD 8 is a TCP socket.

## Step 3: Check the persistence configuration

```console
$ docker exec -it redis redis-cli config get 'append*•'
1) "appendfsync"
2) "always"
3) "appendonly"
4) "yes"
```

This points straight at Redis's persistence options `appendonly` and
`appendfsync`, The configuration is very 11kely unreasonable, causing the high
volume of disk writes.

### Background: Redis persistence

Redis offers two persistence mechanisms: **snapshots (RDB)** and the
**append-only file (AOF)**.

The AOF persists data by appending each write command to the end of a flle in
order, which makes it more durable. It also exposes an `appendfsync` option that
sets the `fsync` policy to ensure written data reaches the disk. The choices are:

1. `always` - call `fsync` on every write operation; the safest and slowest.
2. `everysec` - call `fsync` once per second, so in the worst case you lose at
   most 1 second of data.
3. `no` - leave flushing to the operating system (typically up to ~30 s).

# Step 4: Confirm the fdatasync cost

Use `strace` with `-e` to focus on `fdatasync`:

```console
$ strace -f -p 9885 -T -tt -e fdatasync
strace: Process 9085 attached with 4 threads
[pid 9085] 14:22:52.013547 fdatasync(7) = 0 <0.007112›>
[pid 9085] 14:22:52.022467 fdatasync(7) = 0 <0.008572>
[pid 9085] 14:22:52.032223 fdatasync(7) = 0 <0.006769>
...
[pid 9085] 14:22:52.139629 fdatasync(7) = 0 <0.008183>
```

There is an `fdatasync` call roughly every 10 ms, and each call itself costs
about 7-8 ms. That is the latency source.

## Step 5: Explain the write-on-read behaviour

``` console
$ strace -f-T -tt -p 9085
read(8, "*2\r\n$3\r\nGET\r\n541\r\nuuid:53522908-"..., 16384)
write(8, "$4\r\ngood\r\n", 10)
read(8, "*3\r\n54\r\nSADD\r\n$4\r\ngood\r\n$36\r\n535" ..., 16384)
write(7, "*3\r\n$4\r\nSADD\r\n$4\r\ngood\r\n$36\r\n535"..., 67)
write(8, ":1\r\n", 4)
```

From the `lsof` analysis, FD 7 is the regular file `/data/appendonly.aof` and
FD 8 is the TCP socket. Every time a `GET` returns `good`, a `SADD` follows.
That is why a supposed query interface produces heavy Redis disk writes. Next,
confirm which client owns the FD 8 TCP socket.

## Step 6: Identify the client via the network namespace

Use `lsof` -1 to resolve the TCP socket to a connection. Because Redis and the
Python app both run in containers, you must enter the container's network
namespace to see the full connection. The `nsenter` tool enters a container's
namespaces. If it is not installed you can run it from a container image, for
example:

```console
$ docker run --rm -v /usr/local/bin:/target jpetazzo/nsenter
```

```console
# The two containers share a network namespace, so entering app's namespace suffices
$ PID-$(docker inspect --format {{.State.Pid}} app)
$ -i shows network socket information
$ nsenter --target $PID --net -- lsof -i
COMMAND    PID            USER   FD   TYPE   DEVICE SIZE/OFF NODE NAME
redis-ser 9085 systemd-network    6u  IPv4 15447972      0t0  TCP localhost:6379 (LISTEN)
redis-ser 9085 systemd-network    8u  IPv4 15448709      0t0  TCP localhost:6379->localhost:32996 (ESTABLISHED)
python    9181            root    3u  IPv4 15448677      0t0  TCP *:http (LISTEN)
python    9181            root    5u  IPv4 15449632      0t0  TCP localhost:32996->localhost:6379 (ESTABLISHED)
```

Redis's FD 8 corresponds to the TCP connection
`localhost: 6379->localhost:32996`. `localhost:6379` is Redis's own listening
port, so `localhost:32996` is the Redis client. The last line shows that
`localhost:32996` belongs to our Python application (PID 9181).

## Root causes and fixes

Two problems were found:

1. **`appendfsync always`** forces Redis to call `fdatasync` on every single
   write. This workload does not need such high-frequency. synchronous writes;
   the default `everysec` (1-second interval) is sufficient. Change it and the
   per-write latency disappears.
2. **The Python app calls `SADD` inside the query endpoint.** This is almost
   certainly a misuse of the cache - a read path should not mutate state. Remove
   the write from the read path (or make it asynchronous / batched).

**Correction note:** the real-world default `appendfsync` is `everysec`, not
`always`; `always` was deliberately set to create the problem, Also, modern
Redis (7.x) supports a **multi-part AOF** (a base RDB plus incrementa- AOF) and
an optional RDB-AOF hybrid, which changes the rewrite behaviour but not the
`appendfsync` latency trade-off described here.

# Troubleshooting workflow (summary)

1. Confirm the I/O: `iostat -x 1` shows writes; attribute them with
   `pidstat -d 1`.
2. See what the process does: `strace -f -T -tt -p <pid>` (note slow syscalls
   via the `<duration>` field).
3. Map FDs to files/sockets: "1sof -p <pid›.
4. Check config: `redis-cli config get 'append*'`.
5. Resolve sockets to clients: `lsof -i` (Inside the right netns for
   containers).
6. Fix config + application logic, then re-measure.

## Likely interview questions

- Compare Redis RDB vs AOF durability and performance. What does each
  `appendfsync` value trade off?
- Why is `fatasync` cheaper than `fsync`, and why is it still slow?
- How do you read syscall latency from `strace -T` output?
- How would you attribute disk writes to a specific FD and then to a specific
  client connection?
- How do container network namespaces affect `lsof`/ `ss` visibility, and how do
  you work around it (`nsenter`)?

## References

- Redis persistence (RDB/AOF, `appendfsync`):
  <https://redis.io/docs/management/persistence/>
- `strace` manual: <https://man7.org/11nux/man-pages/man1/strace.1.html>
- `lsof` manual: <https://man7.org/1inux/man-pages/man8/1sof.8.html>
- `nsenter` manual: <https://man7.org/11nux/man-pages/man1/nsenter.1.html>
