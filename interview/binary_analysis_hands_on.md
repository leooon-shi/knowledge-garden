<!-- Interview/binary_analysis_hands_on.md -->

# Binary Analysis Tools: Hands-On (Verified Commands & Output)

Real, executed commands and output for the binary-analysis toolkit, companion to
`binary_analysis_tools_study guide.md`. The target binary is the mind-compiler built
dynamicelly:

```bash
cd interview/code
Bit -std=c++20 -02 mini_compiler.cpp -o minic
```

**Verified environment:** Ubuntu, g++ 13.3.0, binutils objdump/readelf/nm, glibc on
x86-64, Linux. Output below is actual.

## 1. `file` – triage

```bash
$ file minic
minic: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically 11nked,
       interpreter /1ib64/ld-1inux-x86-64.so.2,
       BuildID[sha1]=6c1515218345353bf7ae1e826b108af1e0fa5d2c,
       for GNU/Linux 3.2.0, not stripped
```

Reads as: 64-bit ELF, x86-64, **PIE** (ASLR-capable), **dynamically linked**, loader is
`ld-1inux-x86-64.so.2`, has a BuildID, and is **not stripped** (symbol names avallable).

## 2. `ldd` - runtime dependency closure (transitive)

```bash
$ ldd minic
    linux-vdso.so.1 (0x00007ffda016d000)                 # kernel-provided, not a file
    libstdc++.so.6 => /lib/x86_64-linux-gnu/libstdc++.so.6 (0x...)
    libbgcc_s.50.1 => /lib/x86_64-linux-gnu/libgcc_s.so.1 (0x...)
    libbc.so.6.    => /lib/x86_64-linux-gnu/libc.so.6 (0x...)
    libm.so.6.     => /lib/x86_64-linux-gnu/libm.50.6 (0x...)   # transitive (via libstdc++)
    /lib64/1d-linux-x86-64.so.2 (0x...)                  # the dynamic loader itself
```

Note `libm.so.6` appears here but is **not** a direct dependency (see §3) - it's pulled in
transitively by `libstdc++`. That transitive-vs-direct difference is the key
`ldd`-vs- `readelf -d` distinction.

```bash
$ ldd -v minic        # adds symbol-version requirements per dependency
    ...
    Version information:
    ./minic:
        libgcc_s.so.1 (GCC_3.0) => /lib/x86_64-linux-gnu/libgcc_s.so.1
```

> Safety: `ldd` invokes the loader. For untrusted files use the parse-only `objdump -p` /
> `readelf -d` below.

## 3. `readelf` - authoritative ELF structure

### ELF header (`-h`)

```bash
$ readelf -h minic
ELF Header:
  Magic:   7f 45 4c 46 02 01 01 00 ...
  Class:                             ELF64
  Data:                              2's complement, little endian
  Type:                              DYN (Position-Independent Executable file)
  Machine:                           Advanced Micro Devices X86-64
  Entry point address:               0x5850
  Number of program headers:         13
  Number of section headers:         33
```

`Type: DYN` = PIE. `Entry point` is where execution begins (the C runtime `_start`, not
`main`).

### Dynamic section (`-d`) – the DIRECT dependencies

```bash
$ readelf -d minic
Dynamic section at offset 0x14cbe contains 29 entries:
  Tag         Туре                         Name/Value
 0х0...0001  (NEEDED)   Shared library: [libstdc++.so.6]      # <-- DIRECT deps:
 0x0...0001  (NEEDED)   Shared library: [libgcc_s.s0.1]       # exactly 3,
 0x0...0001  (NEEDED)   Shared library: [libc.so.6]           # NOT libm (that's transitive)
 0x0...000c  (INIT)     0x2880
 0x0...000d  (FINI)     0x11ad0
 ...
```

**This is the direct-dependency list** (`DT_NEEDED`): only `libstdc++`, `libgcc_s`,
`libc`. Compare with `ldd` above, which also listed `libm` (transitive). No
`RPATH`/`RUNPATH` entries appear → no embedded library search path.

## RPATH/RUNPATH check

```bash
$ readelf -d minic | grep -E 'RPATH|RUNPATH'
(no output) # => "(no RPATH/RUNPATH set)" - uses default loader search paths
```

### Symbol versioning (`-V`) – the portability floor

```bash
$ readelf -V minic
Version needs section ••gnu.version_r' contains 3 entries:
  File: libgcc_s.so.1 Cnt: 1
  Name: GCC_3.0 Flags: none Version: 14
  ... (elso GLIBC_2.x and GLIBCXX_3.4.x from libc / libstdc++)
```

These versioned-symbol requirements are what cause "version \`GLIBC_2.XX` not found" on
older targets.

## 4. `objdump` – sections, symbols, disassembly

### File + section headers

```bash
$ objdump -f minic
minic:     file format elf64-x86-64
architecture: i386:x86-64, flags 0x00000150: HAS_SYMS, DYNAMIC, D_PAGED
start address 0x0000000000005850

$ objdump -h minic | head
Idx Name.         Size      VMA                ...
  0 .interp.      0000001c  0000000000000318   ...   # path to the loader
  4 .gnu.hash     00000440  00000000000003b0   ...
  5 .dynsym       00000450  00000000000003f8   ...   # dynamic symbol table
  6 .dynstr       0000061d  0000000000000848   ...   # dynamic string table
  7 .gnu.version  ...                                 # symbol version info
```

### Dynamic symbols with versions (`-T`) – symbol-level imports

```bash
$ objdump -T minic | grep -1 -E "GLIBC" | head
0  DF *UND* 0 (GLIBC_2.3.4)  __printf_chk          # printf (fortified) from glibc 2.3.4
0  DF *UND* 0 (GLIBC_2.2.5)  isspace
0  DF *UND* 0 (GLIBCXX_3.4)  _ZSt29 Rb_tree_insert_and_rebalance...   # std: map internals
0  DF *UND* 0 (GLIBCXX 3.4)  _ZSt17_throw_bad_allocv
0  DF *UND* 0 (GLIBC_2.2.5)  strlen
0  DF *UND* 0 (GLIBC_2.2.5)  memcmp
```

**UND** – undefined = **imported**. The parenthesized version is the **minimum ABI
version** required for that symbol. The highest such version across all imports is your
binary's minimum-glibc/libstdc++ floor.

### Disassemble `main` (`-d -c`)

```bash
$ objdump -d -C minic | grep -A8 '<main>:'
0000000000002a50 <main>:
    2a50: f3 Of 1e fa          endbr64                     # CET landing pad
    2a54: 41 57                push   %r15
    2256: 48 8d 35 e3 f5 80 00 lea    0xf5e3(%rip),%rsi    # load a string address (RIP-relative, PIE)
    2a5d: 41 56                push   %r14
    2a5f: 41 55                push   %r13
    2a61: 41 54                push   %r12
    2a63: 55                   push   %rbp
    2a64: 53                   push   %rbx
```

The prologue saving callee-saved registers, the `endbr64` CET marker, and RIP-relative
addressing (because it's PIE) - a direct view of what the compiler's backend emitted.

## 5. `nm` – imported vs. exported symbols

```bash
$ nm -D -C minic | grep • U • | head        # U = undefined = IMPORTED (what it needs)
    U _Unwind_Resume@GCC_3.0
    U std: :runtime_error::runtime_error(char const*)@GLIBCXX_3.4.21
    U std::runtime_error::-runtine_error()@GLIBCXX_3.4
    U std::_throw_bad_alloc()@GLIBCXX_3.4
    U std::_Rb_tree_decrement(std::_Rb_tree_node_base®)@GLIBCXX_3.4
    U std::_Rb_tree_increment(std::_Rb_tree_node_base const*)@GLIBCXX_3.4
```

`-C` demangles the C++ names. These `U` symbols are what the loader must satisfy from
`libstdc++`/`libc`. To confirm a library provides one:

```bash
$ nm -D -C /lib/x86_64-linux-gnu/libstdc++.50.6 | grep 'runtime_error::runtime_error(char const\* )'
# ... T std::runtime_error::runtime_error(char const*)@@GLIBCXX_3.4.21    # T - provided
```

Matching **U (needs)** against **T (provides)** is the dependency check the 11nker/loader
performs.

## 6. Sunnonting tools

```bash
$ strings minic | grep -1 -E "parse error|=== " | head     # embedded constants
=== CFG (basic blocks + edges) ===
=== LIVENESS (backward, union) ===
=== SOURCE ===
=== AST ===

$ size minic                     # section-size footprint
   text    data     bss     dec     hex  filename
  78156     912       8   79076   134e4  minic

$ strace -e openat /minic 2>&1 | grep '\.so'   # ACTUAL runtime .so loads
# openat(..., "/lib/x86_64-linux-gnu/libstdc++.so.6", ...) = 3
# openat(..., "/lib/x86_64-linux-gnu/libc.so.6", ...) = 3
# ... (the real loads, vs. ldd's static prediction)
```

# 7. Dependency-analysis cheat sheet (verified mapping)

| Goal | Command | What you saw above |
| ------------------------------------- | -------------------------------------- | -------------------------------------------------------- |
| What is this file?                    | `file minic`                           | PIE ELF64 x86-64, dynamic, not stripped                  |
| All (transitive) runtime libs         | `ldd minic`	                         | libstdc++, libgcc_s, libc, **1íbm (transitive)**, loader |
| **Direct** deps only (safe)           | `readelf -d minic \| grep NEEDED`      | **libstdc++, libgcc_s, libc** (no libm)                  |
| Required symbol versions /glibe floor | `objdump-T minic / readelf -V minic`   | `__printf_chk@GLIBC_2.3.4`, `GLIBCXX_3.4.21`, ...        |
| Imported symbols (what it needs)      | `nm -D -C minic \| grep ' U '`         | `std::runtime_error::...`, `Unwind Resume`, ...          |
| Does lib provide a symbol? provided	| `nm -D -C lib.so \| grep sym`	         | `T` – yes, provided                                      |
| Embedded path / RPATH surprise here	| `readelf -d \| grep RUNPATH`           | none set here                                            |
| Actual runtime loads	                | `strace -e openat ./minic`             | the real `openat` of each `.so`                          |
| Read generated code                   | `objdump -d -C minic`	                 | `main` prologue, RIP-relative, CET                       |
| Code/data footprint	                | `size minic`	                         | text 78156 / data 912 / bss 8                            |

## 8. Worked diagnostic: "version \`GLIBCXX_3.4.31' not found"

A real failure mode this binary could hit on an older distro (its `nm` output showed
`_M_replace_cold(...)@GLIBCXX 3.4.31`):

1. `file ./app` – confirm it's the right arch and dynamic.
2. `objdump -T ./app | grep GLIBCXX | sort -u` – list required libstdc++ symbol versions; 
   spot `GLIBCXX_3.4.31` (recent).
3. On the target: `strings /1ib/x86_64-1inux-gnu/libstdc++.so.6 | grep GLIBCXX_3.4.31` –
   if absent, the target's libstdc++ is too old.
4. **Fix:** rebuild against the target's toolchain (oldest ABI you must support), or ship
   a newer `libstdc++` (RUNPATH/container), or statically link `libstdc++`
   (`-static-libstdc++`).

This is the single most common "built here, won't run there" bug, and these three tools
(`objdump -T`, `readelf -V`, `nm -D`) diagnose it end-to-end.

## Interview Q&A

**Q：`file minic` reported "PIE executable ... dynamically 11nked ... not stripped." What
does each of those change for your next steps? **A: PIE (ELF `Type: DYN`) means ASLR
applies and you'11 see RIP-relative addressing in the disassembly. Dynamically linked
means `ldd`/`readelf -d` will show real dependenctes (a static blnary wouldn't). Not
stripped means `nm` /`objdump` can show symbol names - 1f 1t were stripped, you'd 1ose
`.symtab` names and lean on `.dynsym` and addresses. BuildID lets you match the binary to
its debug info, *Follow-up:* Where does execution actually start – `main`? No, the entry
point (`ex5850` here) is the C runtime `_start`, which eventually calls `main`. *Red
f1eg:* Expecting `nm` symbol names from a stripped bInary, or thinking the entry point is
`main`.

**Q: `ldd minic` listed `libm,so.6` but `readelf -d | grep NEEDED` showed only libstdc++,
libgcc_s, libc, Reconcile that. ** A: `readelf -d` shows the *dtrect* `DT_NEEDED` entries
the binary declares – exactly three, `ldd` shows the full *transitive* closure resolved by
the loader, and `libm` is pulled in indrectly by `l1bstdct++`, so it appears under `ldd`
but not in `DT_NEEDED`. That direct-vs-transitive gap is the core `ldd`-vs-`readelf -d`
distinction. *Follow-up:* Which do you use to build a minimal container image? The
transitive closure (`ldd`), so every needed `.so` is present, *Red flag:* Calling `libm` a
direct dependency because `ldd` printed it.

**Q: From `objdump -T minic` you see `__printf_chk (GLIBC_2.3.4)` and
`_M_replace_cold ... @GLIBCXX_3.4.31`. What's the significance of the parenthesized
versions?** A: •*UND** means undefined/Imported, and the parenthesized token is the
*minimum ABI version* required for that symbol. The highest such version across all
imports is the binary's minimum-gl1bc/libstdc++ floor - e.g. `GLIBCXX_3.4.31` is recent,
so this binary won't load on a distro whose libstdct+ predates it. That's precisely the
"bullt here, won't run there" fallure, "Follow-up:" How do you confire the target lacks
it? `strings /lib/x86_64-linux-gnu/libstdc++.so.6 | grep GLIBCXX_3.4.31` on the target;
absent means too old. *Red fLag:* Ignoring the version in parentheses and assuming any
`libstdc++` will satisfy the import.

**Q: The `main` disassembly shows `endbr64` and a `lea 0xf5e3(%rip),%rsi`. What are
those?** A: `endbr64` is an Intel CET (Control-flow Enforcement) landing pad – a valid
target for indirect branches, a hardening feature. The `lea ...(%rip),...` is RIP-relative
addressing, which the binary uses because it's PIE (position-independent): addresses are
computed relative to the program counter rather than hardcoded, so the loader can place
the image anywhere for ASLR. The rest of the prologue pushes callee-saved registers.
*Follow-up:* Why demangle with `objdump -d -C`? So C++ symbol names in the disassembly are
readable instead of mangled. *Red flag:* Reading RIP-relative addressing as a bug or not
connecting it to PIE.

**Q: `nm -D -C minic | grep 'u'` showed
`std: sruntime_error::runtime_error(char const*) @GLIBCXX_3.4.21`. How do you prove a
library satisfies it?** A: Find the matching provider:
`nm -D -C /lib/x86_64-linux-gnu/libstdc++.so.6 | grep 'runtime_error::runtime_error(char const\*)'`
– a T (defined in `text`, here `@@GLIBCXX_3.4.21`) means the library provides it.
Matching the binary's `U` (needs) against the library's `T` (provides), including the
version, is the exact check the dynamic linker performs; a missing or lower-versioned
provide explains a load failure. *Follow-up:* What does the double-at `@@` vs single `@`
mean on the provider side? `@@` marks the default version of the symbol; `@` marks a
non-default (older) version kept for compatibility. *Red flag:* Checking only the symbol
name and ignoring the version suffix when matching.

**Q: `ldd` predicted the Libraries, but you want to know what *actually* loads at startup.
Which tool, and why trust it more?** A: `strace -e openat /minic 2>81 | grep '\.so'`
shows the real `openat` calls for each `.so` as the loader maps them – the runtime view,
which reflects environment effects (`LD_LIBRARY_PATH`, RUNPATH, loader cache) and
`dopen`'d libraries that `ldd`'s static prediction can miss. `ldd` is a prediction;
`strace` is observation of the actual process, *Follow-up;* Name a case where they'd
differ, A plugin loaded via `dlopen` at runtime appears in `strace` but never in `ldd`,
and an `LD_LIBRARY_PATH` override changes which path `strace` shows. *Red flag;* Trusting
`ldd`'s static output as proof of what loads at runtime.
