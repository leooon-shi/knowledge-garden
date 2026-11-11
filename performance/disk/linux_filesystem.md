<!-- pertormance/io/linux_filesystem.md -->

# Linux Filesystems and the VFS

A senior-level reference on how the Linux filesystem layer works: the VFS
abstraction and its four core objects, the page cache, the dentry/inode slab
caches, journaling, file descriptors, and the durability semantics that matter
for performance (`fsync`, `O_DIRECT`).

## 1. The Virtual File System (VFS)

The VFS is the kernel abstraction layer that lets a single set of syscalls
(`open`, `read`, `write`, `stat`, …) work across many concrete filesystems
(ext4, XFS, Btrfs, tmpfs, NFS, procfs, …). Concrete filesystems register a set
of operation tables; the VFS dispatches to them.

Four core in-memory objects:

| Object      | Represents                            | Key operations table         |
|-------------|---------------------------------------|------------------------------|
| superblock  | A mounted filesystem instance         | `super_operations`           |
| inode       | A file's metadata (one per file)      | `inode_operations`           |
| dentry      | A name-to-inode mapping (path lookup) | `dentry_operations`          |
| file        | An open file (per open, per process)  | `file_operations`            |

- **superblock** holds filesystem-wide metadata: block size, total/free blocks
  and inodes, the root dentry, and the `super_operations` (e.g. `sync_fs`,
  `statfs`, `alloc_inode`).
- **inode** is the on-disk/in-memory metadata of a file: type, permissions,
  owner, size, timestamps, link count, and pointers to data blocks. It does
  **not** contain the file name - names live in dentries. One inode can have
  many names (hard links).
- **dentry** (directory entry) caches one component of a path and points at an
  inode. Dentries form the in-memory tree used to accelerate path resolution;
  they are a cache, not an on-disk structure.
- **file** represents an open file description: current offset, access mode, and
  a pointer to the dentry/inode. Multiple `file` objects can point to the same
  inode.

### Relationship to file descriptors

A process's FD table (`fd` integer) points to a system-wide **open file
description** (struct file"), which points to a dentry, which points to an
inode. `dup`/`fork` share the same open file description (and thus the same
offset); a separate `open` of the same path creates a new description.

## 2. The page cache

Regular file reads and writes go through the **page cache**: the kernel caches
file pages in RAM keyed by `(inode, offset)` in an `address_space`. Benefits:

- Reads hit cache (no disk I/O) on repeat access; the kernel also does
  **read-ahead** for sequential patterns.
- Writes are **buffered**: `write()` marks pages *dirty* and returns; a
  writeback thread (`kworker`/Flusher, historically `pdflush`/`bdi`) later
  flushes them to disk. Durability therefore requires an explicit `fsync`.

`free -m` shows this as `buff/cache`. `buffers` is block-device/metadata
buffering; `cache` is page-cache file data (plus tmpfs/shmem). Reclaimable
cache is counted toward `available`.

## 3. dentry and inode caches (slab)

The VFS keeps hot dentries and inodes in slab caches (`dentry`, `inode_cache`,
plus per-fs inode caches like `ext4_inode_cache`). Inspect them:

``` console
$ sudo slabtop -o | head
Active / Total Objects (% used)    ： 1234567 / 1456789 （84.7%）
...
  OBJS ACTIVE USE OBJ SIZE SLABS OBJ/SLAB CACHE SIZE NAME
311040 298123 95% 0.19K  7776 40 62208K  dentry
198432 186021 90% 1,08K 13228 15 211648K ext_inode_cache
```

How to read it: `OBJS` total objects, `ACTIVE` In use, `OBJ SIZE` per-object
size, `CACHE SIZE` total memory. A huge `dentry`/`inode_cache` often means a
workload that stat's or opens enormous numbers of files (e.g. negative-dentry
buildup). You can also see raw counts in `/proc/slabinfo` and release these
caches with `echo 2 > /proc/sys/vm/drop_caches` (2 = slab, 1 = page cache,
3 = both; diagnostic only).

## 4. Journaling

A journal provides crash consistency by recording intended changes before
applying them, so after a crash the filesystem can replay or discard partial
operations instead of running a full `fsck`.

# ext4 journal modes (`data=` mount option)

| Mode               | Journals metadata | Journals data | Trade-off                         |
|--------------------|-------------------|---------------|-----------------------------------|
| `data=journal`     | Yes               | Yes           | Safest, slowest (data written 2x) |
| `data-ordered` (default) | Yes         | No, but data written *before metadata commit | Good consistency, good speed |
| `data=writeback`   | Yes               | No ordering   | Fastest, can expose stale date in files after a crash |

ext4 uses the JBD2 layer. `data=ordered` guarantees that file date reaches disk
before the metadata that references it commits, so you never see a file
pointing at someone else's old blocks.

### XFS

XFS journals **metadata only** (there is no date-journaling mode), It uses
delayed logging and is designed for high parallelism and large filesystems. For
data durability you rely on fsync. FS generally scales better than ext4 on
many-core, high-IOPS workloads; ext4 is simpler and often slightly faster for
small single-threaded cases.

## 5. Checking inode exhaustion

A filesystem can run out of **inodes** even with free space (many tiny f11es):

```console
$ df -i
Filesystem        Inodes   IUsed    IFree IUse% Mounted on
/dev/vda1       6553680  812345  5741255   13%   /
```

How to read it: `IUse% 100%` means you cannot create new files even if `df -h`
shows free bytes. ext4 fixes the inode count at `mkfs` time; XFS allocates
inodes dynamically, so it rarely hits this.

## 6. File descriptors and limits

- Per-process limit: `ulimit -n` (soft) / `ulimit -Hn` (hard); systemd
  `LimitNOFILE`.
- System-wide: `/proc/sys/fs/file-max`, current usage in
  `/proc/sys/fs/file-nr`.
- Inspect a process's FDs: `ls -l /proc/<pid>/fd` or `lsof -p <pid>`.

Running out of FDs surfaces as `EMFILE`/`ENFILE`; a common cause is leaked
sockets (see also `CLOSE_WAIT` buildup).

## 7. Durability semantics

- `write()` only fills the page cache; data can be lost on power failure until
  it is flushed.
- `fsync(fd)` flushes the file's dirty data **and** metadata needed to find it,
  and (on honest storage) issues a device cache flush (FLUSH/FUA). Returns only
  after data is durable.
- `fdatasync(fd)` flushes data + only the metadata required to read the data
  back (e.g. size), skipping non-essential metadata like `mtime`. Cheaper than
  `fsync`, but still a full device flush - this is exactly the ~7-8 ms cost seen
  in the Redis AOF `appendfsync always` case study.
- `sync()`/`syncfs()` flush everything (whole system / whole filesystem).
- **Durability pitfall:** after creating or renaming a file you must also
  `fsync` the **containing directory** to make the new name durable.
- `O_SYNC`/`O_DSYNC` make every `write` synchronous (implicit per-write flush).

### O_DIRECT

`open(..., O_DIRECT)` bypasses the page cache and DMAs data directly between
user buffers and the device. Used by databases that manage their own cache
(e.g. InnoDB with `O_DIRECT`) to avoid double-caching. Constraints and caveats:

- Buffer address, offset, and length must be aligned to the device/filesystem
  logical block size (use `posix_memalign`); misalignment returns `EINVAL`.
- It does **not** imply durability - you still need `fsync`/`O_DSYNC` for that.
- Bypassing read-ahead and cache can be *slower* for workloads that would have
  benefited from caching; it is a win only when the app caches better than the
  kernel can.

## Troubleshooting workflow

1. Metadata-heavy slowness (many small files, `ls`/`stat` storms): check
   `slabtop` dentry/inode caches and `df -i`.
2. "Disk full" but bytes free: `df -i` for inode exhaustion.
3. Data-loss-after-crash questions: audit `fsync`/directory-fsync usage and the
   journal mode (`data=`).
4. Double-caching / Inconsistent latency in a DB: consider `O_DIRECT` + app
   cache vs page cache.

## Tuning knobs and trade-offs

- `noatime`/`relatime` mount: avoid metadata writes on every read (big win for
  read-heavy workloads). `relatime` is the modern default.
- `data-writeback` (ext4): faster, weaker crash guarantees.
- `vm.ufs_cache_pressure`: lower values keep dentry/inode caches longer (good
  for stat-heavy workloads), higher reclaims them more aggressively.
- `O DIRECT` vs page cache: control vs convenience; measure.

## Likely interview questions

- Describe the four VFS objects and how an `fd` maps down to an inode.
- Why does an inode not store the file name? What makes hard links possible?
- Explain the three ext `data=` modes and what each protects against.
- Difference between `fsync` and `fdatasync`; why fsync the directory after
  creating a file?
- When does `O_DIRECT` help and when does it hurt? What alignment rules apply?
- How can a filesystem be "full" with free space, and how do you confirm it?

## References

- VFS overview: <https://docs.kernel.org/filesystems/vfs.html>
- ext4: <https://docs.kernel.org/filesystems/ext4/index.html>
- XFS: https://docs.kernel.org/filesystems/xfs/index.html>
- Page cache / writeback sysctls:
  https://docs.kernel.org/admin-guide/sysctl/vm.html>
- `fsync(2)` /`open(2)` (O_DIRECT):
  <https://man7.org/linux/man-pages/man2/fsync.2.html>,
  <https://man7.org/linux/man-pages/man2/open.2.html>
