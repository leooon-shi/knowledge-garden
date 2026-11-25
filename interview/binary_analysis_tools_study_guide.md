<!-- binary_analysis_tools_study guide.md -->

# Binary Analysis Tools: A Study Guide (objdump, readelf, ldd, nm, and friends)

See also: `linker_relocations_plt_got md` (how relocations, PLT/GOT, the loader, and DWARF
work).

A hands-on guide to the ELF binary-analysis toolkit - **what each tool is for, when to
reach for it, and how to use them to analyze dependencies, symbols, and layout**, Pairs
with the verified command/output walkthrough in binary_analysis_hands_on. md. Companion
in style to the compiler study guide.

## The mental model first: what is an ELF binary?

On Linux, executables and shared 1ibrartes are **ELF** (Executable and Linkable Format)
files. An ELF has:

- an **ELF header** (magde 7f 454c 46°, elass 32/64, endianness, type, machine, entry
  point),
- **program headers** (segments - how the loader maps the file into memory at runtime),
- **section headers** (named sections - `.text` code, `.data`/`.bss` data, `.rodata`
  constants, `.symtab`/`.dynsym` symbols, `.dynamic` dynamic-1inking info, `.plt`/`.got`
  for lazy binding, debug sections),
- a **dynamic section** listing shared-library dependencies (`DT_NEEDED`), the symbol
  tables, and relocation info.

The analysis tools are just different *views* into this structure. Understanding the three
questions they answer - **what is this file? what does it depend on? what symbols does it
import/export?** - tells you which tool to pick.

## The toolkit at a glance

| Tool	                | Layer             | Primary question it answers |
| --------------------- | ----------------- | ---------------------------------------------------------------------------------------- |
| **`file`**            | summary           | What kind of file is this? (ELF type, arch, static/dynamic,stripped?)                    |
| **`ldd`**             | runtime deps      | Which shared libraries will the loader pull in (recursively), and where do they resolve? |
| **`readelf`**         | ELF structure     | Authoritative, raw view of ELF headers/sections/dynamic/symbols/versions                 |
| **`objdump`**         | ELF + disassembly | Sections, symbols, and **disassembly** of machine code                                   |
| **`nm`**              | symbols           | List symbols (defined/undefined) in an object/Library                                    |
| **`srings`**          | content           | Printable strings embedded in the binary                                                 |
| **`size`**            | layout            | Section size summary (text/data/bss)                                                     |
| **`strace`/`ltrace`   | dynamic (runtime) | Which libs/syscalls are *actually* loaded/called at run time                             |	

Rule of thumb: **`readelf` for the authoritative ELF structure, `objdump` when you also
want disassembly, `ldd` for the runtime dependency closure, `nm`/`objdump -T` for
symbol-level dependency debugging.**

______________________________________________________________________

## 1. "file - the 5-second triage

Always start here. One line tells you: ELF or not, 32/64-bit, architecture, **dynamically
vs, statically linked**, PIE or not, the **interpreter** (dynamic loader), BuildID, and
**stripped or not**.

```bash 
file ./prog
# ELF 64-bit LSB pie executable, x86-64, ...dynamically linked,
# interpreter /1ib64/1d-linux-x86-64.50.2, BuildID[sha1]-..., not stripped
```

**When:** before anything else - it sets your expectations (a statically-linked binary has
no `ldd` deps; a stripped one has no symbol names for `nm`).

# 2, `ldd` - the runtime dependency closure

Lists the shared librartes the dynamic loader will map, **recursively** (transitive
closure), and the path each resolves to.

```bash
ldd ./prog
#   linux-vdso.so.1 (...)                         # kernel-provided, not a real file
#   libstdc++.50.6 => /lib/x86_64-linux-gnu/libstdc++.s0.6 (ex...)
#   libc.50.6 -> /lib/x86_64-linux-gnu/libc.so.6 (ex...)
#   /lib64/1d-linux-x86-64.50.2 (...)             # the loader itself
```

**When to use:**

- Diagnosing **"error while loading shared 1ibrardes: X not found"* –ßßßßß `ldd` shows
  `X => not found`.
- Verifying which *version/path* of a library w111 actually be picked up (catche
  `LD_LIBRARY_PATH` surprises, wrong-arch libs).
- Checking the ful1 transitive dependency set before shipping/containerizing.

**Critical caveats:**

- ***`ldd` runs the loader on the file** (historically could execute code via the loader).
  **Never run `ldd` on an untrusted binary** - use `objdump -p` / `readelf -d` Instead,
  which only *parse" the file.
- It shows the **transitive** closure (deps of deps), unlike the direct `DT_NEEDED` list.
- Shows runtime *resolution* - the actual lib chosen depends on `LD_LIBRARY PATH`,
  RPATH/RUNPATH, and the loader cache (`ldconfig`), so `ldd` output can differ between
  machines.

**Safe alternative for direct deps only:**

```bash
objdump -p ./prog | grep NEEDED        # or: readelf -d ./prog | grep NEEDED
```

## 3. `readelf` - the authoritative ELF structure view

Parses ELF without executing anything. The reference tool when you need the *exact* truth.

```bash
readelf -h prog     # ELF header (class, type DYN/EXEC, machine, entry point)
readelf -d prog     # DYNAMIC section: NEEDED 1ibs, RPATH/UNPATH, SONAME, init/fini
readelf -l prog     # program headers (segments the loader maps)
readelf -S prog     # section headers
readelf -s prog     # symbol table (•symtab + •dynsym), ful
readelf --dyn-syms prog   # just dynamic symbols
readelf -V prog     # symbol VERSIONING (GLIBC_2.x / GLIBCXX_3.4.x version needs)
```

**When to use:**

- **Direct dependencies** (just this binary's `DT_NEEDED`, not transitive):
  `readelf -d | grep NEEDED`.
- **Symbol-version / "GLIBC_2.XX not found" errors** - `readelf -V` and `--dyn-syms` show
  which **versioned** symbols the binary needs (the #1 cause of "built on newer distro,
  won't run on older" failures).
- Checking **RPATH/RUNPATH** (embedded library search paths - a security and portability
  concern): `readelf -d | grep -E 'RPATH|RUNPATH'`.
- Confirming **PIE/ASLR** (`Type: DYN` = PIE), stripped-ness, entry point.

`readelf` vs `objdump`: both read ELF, but `readelf` is ELF-specialized and gives the most
faithful raw structure; `objdump` is part of binutils' BFD and shines at **disassembly** 
and a more "linker's-eye" view.

## 4. "objdump' - sections, symbols, and disassembly

```bash
objdump -f prog               # file header summary (format, arch, start addr)
objdump -h prog               # section headers (sizes, VMAs, flags)
objdump -p prog               # program headers + dynamic section (NEEDED, etc.)
objdump -T prog               # DYNAMIC symbol table (imports/exports with versions)
objdump -t prog               # full symbol table
objdump -d prog               # disassemble executable sections
objdump -d -C prog            # ...with C++ names DEMANGLED
objdump -s prog               # disassembly INTERLEAVED with source (needs -g)
objdump -R prog               # dynamic relocations (GOT/PLT entries to resolve)
```

**When to use:**

- **"which external functions does this import, and at what symbol version?"** -
  `objdump -T` (e.g., `__printf_chk` from `GLIBC_2.3.4`). This is dependency analysis at
  the *symbol* level, finer than `ldd`s library level.
- **Reverse engineering / understanding codegen** - `objdump -d -C` to read the generated
  assembly (ties back to the compiler study: watch instruction selection/register
  allocation in action).
- **Debugging** a crash address - disassemble around the faulting PC.
- `objdump -S` to see your source next to its machine code (compile with `-g`).

## 5. "nm" - symbol-level dependency debugging

Lists symbols with a type code; the key ones:

- **`T`** - defined in `.text` (a function this file *provides*; upper =
  global/exported, lower = local).
- **`U`** - **undefined** = a symbol this flle *imports* and expects a library to provide.
- **`D`/`B`'** - defined data (`.data` /`.bss`).
- **`w`** - weak symbol.

```bash
nm -D -C prog | grep ' U '    # imported (undefined) symbols, demangled -> what it NEEDS
nm -D -c lib.so | grep ' T '  # exported symbols -> what a library PROVIDES
```

**When to use - the classic "undefined symbol at runtime/link time" hunt:**

1. `nm -D -C app | grep ' U '` → the symbol it needs (e.8. `foo(int)@SOME_VERSION`).
2. `nm-D -C libwhatever.so | grep foo` → check whether the library actually *exports* it
   (type `T`).
3. Mismatch (symbol missing, or wrong version) explains the 11nk/doad fellure. This
   matching of **U (needs) against T (provides)** across binary + librarles is exactly
   what the dynamic linker does, and `nm` lets you do it by hand.

## 6. Supporting tools

- **`strings prog`**  printable strings (find embedded messages, versions, paths, secrets
  accidentally baked in).
- **'size prog** - `text/data/bss` byte summary (quick code/date footprint).
- **`strace ./prog`** - the syscalls actually made, incl. the `openat` of each `.so` at
  startup → the *real* runtime library loads (vs. `ldd`s static prediction).
- **ltrace ./prog'** - library function* calls at runtine.
- **`ldconfig -p`** - what's in the loader's library cache (where `ldd` resolves from).

______________________________________________________________________

## Dependency analysis: a decision tree

**"Why won't this binary run / link?"**

1. `file prog` - is it even the right arch / dynamically 11nked?
2. `ldd prog` - any ` => not found`? → a missing library. Fix with install /
   `LD_LIBRARY_PATH` / RPATH. *(Untrusted file? use `readelf -d | grep NEEDED` instead.)*
3. Library present but **"undefined symbol"** at load? → `nm-D -C prog | grep ' U '` to
   find the symbol, then `nm -D -C the_lib.so | grep sym` to see if the lib provides it.
   Often an ABI mismatch (wrong lib version).
4. \*\*"version `GLIBC_2.XX' not found`**? → `readelf -V prog`/`objdump -T prog\` to see
   the required symbol versions; you built against a newer libc than the target has.
   Rebuild on/against the older target, or statically 1ink, or use a compatibility shim.
5. **wrong library picked up?** → check `readelf-d | grep RUNPATH`, `LD_LIBRARY_PATH`,
   and `ldconfig -p`; confirm with `strace -e openat ./prog`.

## The runtime-vs-static-view distinction (key insight)

- **Static view** (parse the f1le): `readelf`, `objdump`, `n` - safe on untrusted files,
  show what the binary "declares.
- **Runtime view** (involve the loader/execution): `ldd` (runs the loader),
  `strace`/`ltrace` (run the program) - show what *actually* happens, including
  environment effects. They can disagree (env vars, RUNPATH, dlopen'd librarles that `ldd`
  won't show because they're loaded on demand at runtime).

## Staff/Principal-level framing

- **These tools are the lens onto the ABI contract.** A binary is a set of *promises*
  (exports, type T) and *requirements* (imports, type U, with symbol versions).
  Linking/loading is matching requirements to promises. Mastering
  `nm -D`/`objdump -T`/`readelf -V` means you can debug any symbol/ABI mismatch - the
  hardest class of deployment failures.
- **Symbol versioning (GLIBC_2.x/GLIBCXX_3.4.x) is the #1 portability landmine.** "Works
  on my machine,`version not found` on the server" is almost always a versioned-symbol
  mismatch from building on a newer toolchain; `readelf -V` / `objdump -T` diagnose it,
  and the fix is building against the oldest target ABI (or static linking / containers).
- **Prefer parse-only tools on untrusted binaries.** `ldd` can execute code via the
  loader; `readelf -d`/`objdump -p` are safe. This matters in security/forensics contexts.
- ***`ldd` shows transitive + resolved; `D_NEEDED` shows direct + declared.** Know which
  you need: shipping a container wants the transitive closure (`ldd`); debugging a
  specific missing dep wants the direct list (`readelf -d`).
- **These tie directly to the compiler/linker pipeline** - `.plt`/`.got`, relocations
  (`objdump -R`), and lazy binding are the runtime continuation of code generation;
  reading them connects "how the compiler emitted calls" to "how the loader resolves
  them."

## Suggested exercises (with a real binary)

Two ways to practice:

**(a) Self-checking lab (recommended):** run `interview/code/binary_analysis_practice.sh`,
which builds a shared library + a dynamic app + a static app and walks you through 8
guided, verified exercises (triage, direct-vs-transitive deps, PATH, U-vs-T symbols,
symbol-version floor, disassembly, static-vs-dynamic, static-vs-runtime view).

``` bash
cd interview/code
./binary_analysis_practice.sh            # all exercises with answers
./binary_analysis_practice.sh --quiz     # answers hidden - test yourself
./binary_analysis_practice.sh 5          # just exercise 5
```

**(b) Manual, on any binary:** build any dynamically-linked program (e.g., the
mini-compiler: `g+ -std-c++20 -02 mini_compiler.cpp -0 minic`), then:

1. `file minic` - identify type/arch/PIE/stripped.
2. `ldd minic` vs. `readelf -d minic | grep NEEDED` - compare transitive vs. direct deps;
   explain the difference.
3. `objdump -T minic | grep GLIBC` - list the libc symbol versions it requires; find the
   *highest* one (that's your minimum-glibc floor).
4. `nm -D -C minic | grep ' U '` - list imported C++ symbols; match a few to
   `1ibstdc++.so.6` with `nm -D -C /lib/x86_64-linux-gnu/libstdc++.so.6 | grep ...`
5. `objdump -d -C minic | grep - A15 '<main>:'` - read the first instructions of `main`.
6. Comple with `-static`, re-run `file` and `ldd` - observe "not a dynamic executable"
   and no deps; compare `size` before/after.
7. `strace -e openat /minic 2>&1 | grep '\.so'` - see the *actual* `.so` flles opened at
   startup; compare to `ldd`.

See `binary_analysis_hands_on.md` for the verified output of these commands.

## Interview Q&A

**Q: You have an unknown ELF binary. What's your first command and what does it tell
you?** A: `file ./prog` - the 5-second triage. One 1ine tells you ELF or not, 32/64-bit,
architecture, dynamically vs. statically 11nked, PIE or not, the interpreter (dynamic
loader), BulldID, and stripped or not. It sets expectations for everything else: a
statically-linked binary has no "ldd deps, and a stripped one has no symbol names for
`nm`. *Follow-up:* What does `Type: DYN` mean in `readelf -h`? It's a PIE
(position-independent executable), so ASLR applies - not a shared library in this context.
*Red flag:* Running disassembly or `ldd` before knowing whether it's even the right
architecture.

**Q: `ldd` and `readelf -d | grep NEEDED` disagree on a binary's dependencies. Who's
right?** A: Both - they answer different questions. `readelf-d` (or `objdump -p`) lists
the *direct* `DT_NEEDED` entries the binary itself declares; `ldd` runs the loader and
shows the full *transitive* closure (deps of deps) with resolved paths. In the
mini-compiler, `libm` shows up under `ldd` but not in `DT_NEEDED` because it's pulled in
transitively via libstact. Shipping container wants the transitive set: debugging a
specific missing dep wants the direct list. *Follow-up:* Why avoid `ldd` on an untrusted
binary? `ldd` invokes the dynamic loader on the file and could execute code; use the
parse-only `readelf -d`/`objdump -p` instead. *Red flag:* Treating `ldd` output as the
authoritative direct-dependency list, or running it on a suspicious binary.

\*\*Q: A binary fails with "version
`GLIBC_2.XX' not found" on an older server. How do you diagnose and fix it?** A: It's a versioned-symbol mismatch - you built against a newer libc than the target has. Use `objdump
-T prog | grep GLIBC | sort -u` (or `readelf
-V`) to list required symbol versions and find the highest one, which is your minimum-glibe floor; check the target's `libc` with `strings
.../libc.so.6 | grep
GLIBC_2.XX`. Fix by rebuilding on/against the oldest ABI you must support, statically linking, or shipping a compatible libc via RUNPATH/container. *Follow-up:* Same failure but for `GLIBCXX_3.4.31` - what changes? Same procedure on 1ibstdc+ a common fix is `-static-libstdc++\`
or building on the older toolchain. *Red flag:* "Just upgrade glibc on production" as the
first answer, or not knowing which tool surfaces symbol versions.

**Q: A program links but dies at load with "undefined symbol." Walk me through the hunt
with `nm`.** A: `nm-D -C app | grep ' U '` lists undefined (imported) symbols - what the
binary needs - demangled. Then `nm -D -C the_lib.so | grep sym` checks whether the library
actually exports it (type `T` - provided in `.text`). A missing symbol or a version
mismatch between the `U` need and the `T` provide explains the failure. Matching `U`
(needs) against `T` (provides) across the binary and its libraries is exactly what the
dynamic linker does. *Follow-up:* What do the lowercase vs uppercase type codes mean?
Uppercase - global/external (e.g.,`T` exported function), lowercase = local (e.g-, `t`
local function). *Red flag:* Confusing `U` (undefined/imported) with "defined here," or
forgetting `-D` so you look at `.symtab` instead of dynamic symbols.

**O: When do you reach for `objdump` instead of `readelf`?** A: When you also want
disassembly or a linker's-eye view. `readelf` is ELF-specialized and gives the most
faithful raw structure (headers, dynamic section, symbol versions); `objdump`
(binutils/BFD) shines at disassembly - `objdump -d -C` for demangled assembly,
objdump -S to interleave source (needs `-g`), `objdump -R` for dynamic relocations
(GOT/PLT), and `objdump -T` for versioned dynamic symbols. For pure structure questions,
prefer `readelf`. *Follow-up:* why might you disassemble around a crash address? To read
the faulting instruction and its context when all you have is PC from core dump or
1og. *Red flag;* Using `objdump` for authoritative ELF structure when `readelf` is the
specialized, more faithful tool.

**Q: Static-view tools and runtime-view tools can disagree. Give an example and why it
matters.** A: Static tools (`readelf`, `objdump`, `nm`) parse the file and show what it
*declares*; runtime tools (`ldd` runs the loader, `strace`/`ltrace` run the program) show
what *actualLy* happens. They disagree when environment matters - `LD_LIBRARY_PATH`,
RPATH/RUNPATH, `ldconfig` cache can change which library resolves, and `dlopen`'d
libraries won't appear in `ldd` because they're loaded on demand.
`strace -e openat ./prog` shows the real `.so` loads. It matters for "works here, not
there" bugs and for safety (prefer parse-only tools on untrusted files). *Follow-up:* Why
can `strace` reveal a dependency `ldd` missed entirely? A `dlopen`'d plugin is loaded at
runtime, not declared in `D_NEEDED`, so only an execution trace sees it. *Red flag:*
Assuming `ldd` captures every library program will ever load.

**Q: Staff-level; why call these tools "the lens onto the ABI contract"?** A: A binary is
a set of promises (exports, type `T`) and requirements (imports, type `U`, with symbol
versions); linking and loading is matching requirements to promises. Mastering `nm -D`,
`objdump -T`, and `readelf -V` lets you debug any symbol/ABI mismatch - the hardest class
of deployment failures, of which versioned-symbol mismatches (GLIBC_2.x/GLIBCXX_3.4.) are
the #1 portability landmine, These also connect compile-time to runtime: `.plt`/`.got`,
relocations (`objdump -R`), and lazy binding are the loaden's continuation of what the
compiler's backend emitted, *Follow-up:* What's the one-11ne fix philosophy for the
versioning landmine? Build against the oldest target ABI you must support (on statie-1ink
/ containerize). *Red fLag:* Viewing these as unrelated utilities rather than
complementary views of one ABI matching problem.
