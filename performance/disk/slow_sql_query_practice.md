<!-- performance/io/slow_sq_query_practice.md -->

# Case Study: Slow SQL Query

A worked case study in diagnosing a MySQL workload whose queries run extremely
slowly because of an I/O bottleneck. The root cause turns out to be a second
process that repeatedly drops the page cache, starving the `MyISAM` -backed
query path of the file cache it depends on.

## Symptom

A query interface backed by MysQL responds very slowly. CPU is mostly idle but
`iowait` is high, and the system shows sustained disk reads. The classic
checklist is: confirm the bottleneck is disk I/O, find which process drives the
I/O, find which files it touches, then explain *why* the I/O happens.

## Tooling

### filetop (bcc / eBPF)

`filetop` is part of the bcc package. It is built on the Linux kernel's eBPF
(extended Berkeley Packet Filter) mechanism and primarily traces file reads and
writes inside the kernel. It prints the thread ID (TID), the read/write size,
the read/write type, and the file name.

```console
# Switch to the tools directory
$ cd /usr/share/bcc/tools


# The -C option keeps the screen from being cleared on each new batch of output
$ ./filetop -C

TID    COMM             READS  WRITES R_Kb    W_Kb    T FILE
514    python           0      1      0       2832    R 669.txt
514    python           0      1      0       2490    R 667.txt
514    python           0      1      0       2685    R 671.txt
514    python           0      1      0       2392    R 670.txt
514    python           0      1      0       2050    R 672.txt

...

TID    COMM             READS  WRITES R_Kb    W_Kb    T FILE
514    python           2      0      5957    0       R 651.txt
514    python           2      0      5371    0       R 112.txt
514    python           2      0      4785    0       R 861.txt
514    python           2      0      4736    0       R 213.txt
514    python           2      0      4443    0       R 45.txt
```

How to read it: `T` is the file type (`R` regular file, `S` socket, `O` other);
`R_Kb`/`W_Kb` are KiB read/written during the interval. Here a `python` thread
is reading many txt files.

## opensnoop (bcc / eBPF)

`opensnoop` is also part of the bec package. It dynamically traces the `open`
family of system calls in the kernel, so you can see exactly which paths are
being opened and whether the open succeeded.

```console
$ opensnoop
12288 python              6   0 /tmp/9046db9e-fe25-11e8-b13f-0242ac110002/650.txt
12280 python              6   0 /tmp/9046db9e-fe25-11e8-b13f-0242ac110002/651.txt
12280 python              6   0 /tmp/9846db9e-fe25-11e8-b13f-0242ac116002/652.txt
```

Combining `filetop` and `opensnoop`: the case application dynamically generates
a batch of files used as temporary scratch storage, which it deletes once it is
done with them, Unfortunately, it is precisely these flle reads and writes that
create the I/O bottleneck and make the whole processing pipeline very slow.

### Inspecting the MysQL process

``` console
# -t shows threads, -a shows the full command line
$ pstree -t -a -p 27458
mysqld,27458 --log_bin=on --sync_binlog=1
...
  └─{mysqld},27922
  └─{mysqld},27923
  └─{mysqld},28014
```

## Root cause: a process dropping the page cache

A second process, `DataService`, periodically writes `1` to
`/proc/sys/vm/drop_caches`. Writing to `drop_caches` releases the page cache
(the file cache). The data MySQL reads lives in that flle cache, so a process
that continuously drops the page cache prevents MySQL from benefiting from the
disk cache at all.

The key insight: the data table the case application queries uses the `MyISAM`
engine, and a defining characteristic of `MyISAM` is that it caches **only
indexes in memory, not row data**, So when a query cannot use an index, the
table data has to be read from the database file into memory before it can be
processed. If some other process keeps dropping the page cache, every query
re-reads the data from disk.

### Confirming with vistat

```console
$ vastat 1
procs -----------memor---------- ---swap-- -----io---- -system-- ------cpu-----
 r  b   swpd   free   buff  cache   si   so    bi    bo   in   cs us sy id wa st

# Note: DataService is running
 0  1      0 6747540    132 353084    0    0 20572     4   90  574  1 4 69 27  0
 0  2      0 6747540    132 368536    0    0 15468     0   32  304  1 0 79 20  0

# Note: DataService is stopped starting here
 0  0      0 6747540   1360 424164    0    0   864     0  133 1266  1 0 94  5  0
 0  1      0 6747540   1368 437400    0    0 13328     0   45  366  0 0 83 17  0
 0  1      0 6747540   1368 470148    0    0 32056     0   42  413  1 1 50 49  0
...
 0  0      0 6747540   1368 918576    0    0 29056     0   42  568  0 0 56 44  0
 0  0      0 6747540   1368 918576    0    0     0     0   42  568  0 0 100  0  0
```

How to read it: `bi` is blocks read in from disk per second, `bo` is blocks
written out, and `wa` is the CPU time spent waiting on I/O (iowait).

Before `DataService` is stopped, `cache` grows for about three samples and then
drops back - exactly because `Dataservice` drops the page cache every 3 seconds.
After `DataService` is stopped, `cache` keeps climbing until it reaches 918576
and then stays flat. At that point disk reads (`bi`) fall to 0 and iowait (`wa`)
also falls to 0, which shows that all the data is now served from the system
cache.

## Lessons and fixes

MySQL's `MyISAM` engine does not cache row data itself; it relies on the system
page cache to accelerate disk I/O. Once any other application runs on the same
host, `MyISAM` struggles to make full use of the system cache, because that
cache can be consumed by other processes - or, as here, dropped outright.

The broader recommendation: do not build an application's performance entirely
on top of the system cache. It is better to allocate memory inside the
application and build a cache you fully control. For example, MySQL's `InnoDB`
engine caches both indexes and data; alternatively you can use a third-party
cache such as Memcached or Redis.

**Correction note:** Modern MySQL defaults to `InnoDB` (since MySQL 5.5, 2010),
which has its on buffer pool (`innodb_buffer_pool_size`) and does not depend on
the os page cache the way `MyISAM` does. `MyISAM` is effectively legacy today.
Also, `echo 1 > /proc/sys/vm/drop_caches` drops only clean page cache; `2`
drops dentries/inodes (slab); `3` drops both. It is a diagnostic/benchmarking
tool, never something a production service should do on a loop.

## Prefix indexes (a related MySQL tuning topic)

This technique is described in *High Performance MysQL* as choosing an optimal
prefix. Sometimes you must index a long string column, which makes the index
large and slow, You can often index only the leading characters, which saves a
lot of index space and improves index efficiency - but it also lowers the
index's **selectivity**.

Selectivity is the ratio of distinct index values (the *cardinality*) to the
total number of rows, ranging from `1/#T` to `1`, Higher selectivity means
higher query efficiency, because a highly selective index lets MysQL filter out
more rows during a lookup. A unique index has selectivity `1`, the best possible
value and the best performance.

In general a prefix is often selective enough to satisfy query performance, For
`BLOB`, `TEXT`, or very long `VARCHAR` columns you **must** use a prefix index,
because MysQL does not allow indexing the full length of those columns.

The trick is to pick a prefix long enough for high selectivity but not so long
that you waste space. The prefix should be long enough that its selectivity
approaches that of indexing the whole column - in other words, the prefix's
cardinality should approach the full column's cardinality.

```sql
-- Compute the full-column selectivity ratio:
SELECT COUNT (DISTINCT city)/COUNT(*) FROM city_demo;
-- Then increase the prefix length until it approaches that ratio:
SELECT COUNT (DISTINCT LEFT(city, 3))/COUNT(*) FROM city_demo;
-- Grow the 3 gradually until the result is close to the full-column ratio;
-- that length gives the best-performing prefix index.
```

## Troubleshooting workflow (summary)

1. Confirm the bottleneck is disk I/O: `top`/`vmstat` show high `wa` (iowait),
   low user/system CPU.
2. Find the busy device: `iostat -x 1` (high `%util`, high `r/s`, high
   `r_await`).
3. Find the process: `pidstat -d 1`, `iotop`.
4. Find the files and syscalls: `filetop`, `opensnoop`, `strace -e trace=file`,
   `lsof -p <pid>`.
5. Explain *why*: check for cache-dropping, missing indexes, engine choice.
6. Verify the fix by re-running the same measurements.
  
# Likely interview questions

- Why does `MyISAM` depend on the 05 page cache while `InnoDB` does not?
- What do `vmstat`'s `bi`, `bo`, and `wa` columns mean, and how do they relate
  to `iostat` output?
- hihat do the three values of `/proc/sys/vm/drop_caches` do? Why is dropping
  caches in a loop harmful?
- How would you use eBPF tools (`filetop`, `opensnoop`, `biosnoop`) to attribute
  I/O to a process and a file?
- What is index selectivity/cardinality, and how do you choose a prefix-index
  length?
 
## References

- `filetop`, `opensnoop` - bcc tools: <https://github.com/iovisor/bcc>
- Linux `vm` sysctls (`drop_caches`):
  <https://docs.kernel.org/admin-guide/sysct1/vm.html>
- *High Performance MySQL* (prefix index / selectivity discussion)
 