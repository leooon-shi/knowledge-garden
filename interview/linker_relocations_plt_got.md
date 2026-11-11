<!-- interview/linker_relocations_plt_got.md -->

# Linking: Symbols, Relocations, PLT/GOT, Dynamic Loading, and Debug Info

The mechanism behind what `binary_analysis_tools_study guide.md` and
`binary analysis hands_on.md` *show* with `readelf`/`objdump`/`nm`, and what
`lto_and_pgo.md` changes. ELF / x86-64 Linux unless noted.

## 1. What the linker does

1. **Symbol resolution** - match every undefined symbol reference to exactly one
   definition.
2. **sasection merging and layout** - combine `.text`, `.data`, `.rodata`, `.bss`, ... from
   all inputs into output segments.
3. **Relocation** - patch addresses into the code/date now that final (or load-relative)
   addresses are known.

Compiler output (`.O`) has placeholder addresses plus a **relocation table** that tells
the linker "patch the 4 bytes at offset X with (address of symbol S + addend - place)".

## 2. Symbols

`nm`/`readelf -s` columns encode binding and type:

- **Binding:** `LOCAL` (flle-static), `GLOBAL`, `WEAK`.
- **Visibility:** `default` (exported, preemptible in shared objects), `hidden` (global
  inside the DSO, not exported), `protected` (exported but not preemptible).
- `nm` letters: `T/t` text, `D/d` data, `B/b` bss, `R/r` rodata, `U` undefined, `W/w`
  weak, `V` weak object; uppercase = global.

## Resolution rules (static linking)

- One strong definition + any number of weak ones -> strong wins. Multiple strong ->
  multiple definition" error. Only weak -> first one wins.
- Undefined weak references resolve to 0 (`if (&optional_hook) optional_hook();`).
- **Archives (`.a`) are scanned in order, once:** the linker pulls in an archive member
  only if it resolves a currently-undefined symbol. So `-lfoo` must come *after* the
  objects that use it, and circular dependencies need `--start-group ... --end-group` (or
  repeating the library).
- **COMMON symbols** (uninitialized globals in C) - GCC 10+ defaults to `-fno-common`,
  turning old "tentative definition" duplicates into errors.

### ODR and C++

- `inline` functions, templates, and `inline` variables are emitted in every TU that uses
  them, in **COMDAT groups** (`.text. _Z...` in a group); the linker keeps one copy.
- **ODR violations are usually not diagnosed:** two different definitions of the same
  inline function / class across TUs -> the linker silently picks one. Detection: ASan
  `detect_odr_violation`, `-Wodr` with LTO, gold/lld `--detect-odr-violations`.
- Name mangling (`_ZN3foo3barEi`) encodes namespace, class, parameters; demangle with
  `c++filt` or `nm -C`.

## 3. Relocations

Inspect with `readelf -r file.o` or `objdump -dr file.o`.

| Relocation (x86-64)               | Meaning                          | Typical source                                   |
| --------------------------------- | -------------------------------- | ------------------------------------------------ |
| `R_X86_64_PC32`                   | S + A - P (32-bit PC-relative)   | call/jmp/lea to a local or hidden symbol         |
| `R_X86_64_PLT32`                  | L + A - P, via PLT if needed     | `call foo` to a possibly-external function       |
| `R_X86_64_GOTPCREL(X)`            | G + GOT + A - P                  | load an external variable's address from the GOT |
| `R_X86_64_64`                     | S + A (absolute 64-bit)          | pointers in data (`void* p- &x;`)                |
| `R_X86_64_RELATIVE`               | B + A (dynamic)                  | pointers in PIE/DSO data, fixed by the loader    |
| `R_X86_64_GLOB_DAT` / `JUMP_SLOT` | Dynamic: fill GOT / PLT GOT slot | resolved by `ld.so`                              |
| `R_X86_64_TPOFF32`, `DTPMOD64`…   | TLS models                       | thread-local variables                           |

(S = symbol value, A = addend, P = place being patched, L = PLT entry, G = GOT offset, B =
load base.)

- `mcmodel=small` (default) assumes coder+data within ±2 GiB, which is why most
  relocations are 32-bit PC-relative. "relocation truncated to fit" means something is out
  of range (huge state arrays) -> `-mcmodel=medium/large`.
- **Static binaries** have no dynamic relocations (except static-PIE self-relocation);
  **PIE/DSOs** carry `.rela.dyn` and `.rela.plt`.
- `--emit-relocs` keeps static relocations in the output - required by BOLT.

## 4. Position-independent code, GOT, and PLT

A shared 11brary is loaded at an unknown address, and its code pages must be shareable
(read-only), so code can't be patched per process, Solution: **put all per-process
addresses in a data table, and reach that table PC-relatively."

- **GOT (Global Offset Table)** - array of addresses, one slot per external symbol, filled
  by the dynamic loader. Code does mov foo@GOTPCREL(%rip), %rax* then dereferences.
- **PLT (Procedure Linkage Table)**- small stubs for calling external functions:
  ```
  foo@plt:  jmp *foo@GOTPLT(%rip)   ; initially points back to the next instruction
            push $index
            jmp .plt0                ; -> _dl_runtime_resolve
  ```
  **Lazy binding:** the first call goes through the resolver, which looks up `foo`, writes
  its address into the GOT slot, and jumps there. Later calls take one indirect jump.
- **Immediate binding / hardening:** `-Wl,-z,now` resolves everything at load
  (`LD_BIND_NOW=1` at runtime); combined with `-wl,-z,relro` ("full RELRO") the GOT is
  made read-only after relocation, blocking GOT-overwrite exploits. Most distros default
  to PIE + full RELRO.
- **`-fno-plt`**- call through the GOT directly (`call *foo@GOTPCREL(%rip)`), skipping
  the PLT stub; needs immediate binding.
- **`-PIC` vs `-fPIE`:** both position-independent; `-fPIE` may assume its own symbols
  aren't preempted (direct PC-relative access), `-fIC` must assume default-visibility
  symbols can be interposed.

### Symbol interposition - the hidden cost

In a DSO, any default-visibility global can be overridden by an earlier-loaded definition
(`LD_PRELOAD`, executable). So the compiler must call even *its own* exported functions
through the PLT and can't inline them across that boundary.

- `-fvisibility-hidden` + explicit `__attribute__((visibi11ty("default")))` exports:
  smaller dynamic symbol table, faster loading, more inlining.
- `-fno-semantic-interposition` (GCC; Clang default) lets the compiler assume no
  interposition.
- `-Wl,-Bsymbolic(-functions)` binds intra-1ibrary references at link time.
- Interposition is also a feature: `LD_PRELOAD` malloc replacements (jemalloc/tcmalloc),
  sanitizer interceptors, and `faketime` rely on it.

## 5. The dynamic loader ("ld.so')

- The kernel maps the executable and the interpreter named in `PT_INTERP`
  (`/lib64/ld-linux-x86-64.so.2`), then jumps to the loader.
- Loader reads `DT_NEEDED` entries and searches: **`DT_RPATH`** (only if no RUNPATH) ->
  `LD_LIBRARY PATH` -> **`DT_RUNPATH`** (applies to direct dependencies only) ->
  `/etc/ld.so.cache` -> default dirs. `$ORIGIN` = directory of the object (relocatable
  installs).
- Applies relocations, runs `.preinit_array` / `.init_array` constructors (dependencies
  first), then `main`.
- Debug: `LD_DEBUG=libs,bindings,symbols ./prog`; `ldd` (runs the loader - don't use on
  untrusted binaries; use `readelf -d` / `objdump -p` instead); `patchelf --set-rpath` to
  edit.
- **Symbol versioning:** `GLIBC_2.34`, `GLIBCXX_3_4_30` in `.gnu.version_r`. "version
  \`GLIBCXX_3.4.30' not found" = built against a newer libstdct++ than the runtime has.
  Build on the oldest target, or ship/static-link libstdc++.
- `dopen`/`dlsym` libraries aren't visible to `ldd`. `RTLD_LOCAL` vs `RTLD_GLOBAL`
  decides whether their symbols join the global scope (a source of duplicate-singleton
  bugs in plugin systems).
- **Static initialization order fiasco:** order of global constructors *across TUs*
  is unspecified. Use function-local statics (thread-safe since C++11) or `constinit`.

## 6. Linker features you get asked about

- **Garbage collection:** `-ffunction-sections -fata-sections -Wl,--gc-sections` drops
  unreferenced sections. `print-gc-sections` to see what went.
- **Identical code folding:** `-wl,--icf=all` (gold/lld) merges identical functions -
  changes function-pointer identity; `--icf-safe` avoids address-taken functions.
- **Linker scripts:** control memory layout (`MEMORY`, `SECTIONS`) - essential in
  embedded/kernel work; `-Wl, -Map=out.map` explains what landed where.
- **Linkers:** GNU ld (bfd), gold (deprecated in binutils 2.44), **lld** (fast, LLVM),
  **mold** (fastest, parallel). Link time often dominates incremental C++ builds.
- **LTO:** the "objects" contain IR; the linker plugin hands them back to the compiler for
  whole-program optimization (`lto_and pgo.md`). With LTO, visibility and `--gc-sections`
  decisions become optimization inputs.
- **Static vs dynamic linking:** static = no loader/interposition cost, self-contained,
  bigger, no security updates via shared libs, glibc static has NSS/`dlopen` caveats (musl
  is friendlier). Dynamic = shared pages, updatable, slower startup, versioning issues.

## 7. Debug information (DWARF)

- `-g` emits `debug_info` (types, variables, scopes as DIEs), `debug_line` (address ->
  file:line table), `.debug_frame`/`.eh_frame` (CFI for unwinding),
  `.debug_loclists`/`.debug_rnglists` (variable locations and code ranges over PC ranges).
  `.debug_str`.
- **`<optimized out>`**; at `-O2` a variable may live only in a register for part of its
  range, or be eliminated entirely; the location list has no entry for the current PC.
  Mitigation: `-Og`, `-fvar-tracking-assignments` (GCC default at -02 -g), or inspect the
  computation.
- **Inlined frames** are described by `DW_TAG_inlined_subroutine`; good
  debuggers/profilers show them as virtual frames.
- `eh_frame` is **not** debug info - it's needed for C++ exceptions and is kept in
  stripped binaries, Frame-pointer-based unwinding (`-fo-omit-frame-pointer`, now default
  in Fedora/Ubuntu builds) makes profilers cheaper and more reliable than DWARF unwinding.
- **Size control:** `-gsplit-dwarf` (`.dwo` files, faster 1inks), `-gz` (compressed
  sections), `objcopy --only-keep-debug` + `--add-gnu-debuglink`, `strip --strip-debug`.
- **Build ID** (`.note.gnu.build-id`) identifies the exact binary; **debuginfod** servers
  fetch matching debug info by build ID (gdb, perf, valgrind support it),
- Tools: `readelf --debug-dump=info|line`, `llvmm-dwarfdump`,
  `addr2line -e prog -f -i 0x401136`, `eu-stack`.

## Likely interview questions

- *"walk through what happens on the first call to `printf` in a dynamically linked
  program."* - PLT stub -> GOT slot points back -> resolver -> GOT patched -> Later calls
  one indirect jump.
- *"Why can't a shared Ltbrary's code be patched with absolute addresses?"* - Code pages
  are shared and read-only; position-independence via GOT.
- *"Why does Link order matter with static libraries?"*
- *"What is full RELRO and why?"*
- *Why is `-fvisibility=hidden` a performance win?"* - No interposition -> direct calls,
  inlining, smaller dynsym, faster load.
- *"`D_RATH` vs `DT_RUNPATH`?"* - Search order relative to `LD_LIBRARY_PATH`; RUNPATH
  not transitive.
- *"What is an ODR violation and why doesn't the linker catch it?"*
- *"Why does gdb say `<optimized out>`?"*
- *"Weak symbol use case?"* - Optional hooks, overridable defaults (`operator new`,
  `malloc` replacements).

## Synthesis

The linker resolves symbols (strong/weak/COMDAT rules, order-sensitive archive scanning,
mostly-undiagnosed ODR, lays out sections, and applies **relocations** recorded by the
compiler. Position-independent code moves all per-process addresses into the **GOT**, and
external calls go through **PLT** stubs that lazily bind via `ld.so` unless `-z now` with
RELRO making the GOT read-only). Default visibility in DOs implies **interposition**,
which blocks inlining and forces PLT calls - `-fvisibility=hidden` and
`-fno-semantic-interposition` recover it. The loader follows `DT_NEEDED` and RPATH/RUNPATH
search rules and checks **symbol versions**; most "works on my machine" binary issues live
here. **DWARF** maps machine code back to source (line tables, location lists, inlined
subroutines), and its interaction with optimization explains `<optimized out>`.
