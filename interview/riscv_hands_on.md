<!-- interview/risc_hands_on.md -->

# RISC-V: Hands-On Practice Lab

A runnable lab to **see** RISC-VISA features in real generated code, companion to
`riscv_cpu_architecture.md`. The script `interview/code/riscv_practice.sh` uses **clang as
a cross-compiler** (clang is natively multi-target) to emit RISC-V assembly/objects from
C, then reads them with `llvm-objdump`/`size` to make each ISA concept concrete. No RISC-V
hardware needed.

```bash
cd interview/code
./riscu_practice.sh          # all exercises with explanations
./riscv_practice.sh --quiz   # hide explanations - reason about the assembly yourself
./riscv_practice.sh 4        # one exercise (1..7)
```

**Verified environment:** clang 18 (riscv32/riscv64 targets) + llvm-obJdump on x86-64
Linux. No GCC cross-toolchain or QMU required for exercises 1-6; exercise 7 runs the
binary if ``qemu-user` is present (degrades gracefully otherwise). Output below is
actual.

Why clang works without a cross-toolchain: a compiler's **backend** can target any
architecture it supports (`clang --target-riscv64 -march=rv64gc`). You don't need a RISC-V
machine to *generate and read* RISC-V code - only to *run* it (which is what QEMU
emulates). This directly reinforces the "ISA ≠ implementationl/compiler backend is a
clean RISC-V target" point from the write-up.

## Verified results

### Ex 1 - ABI registers & load/store design

```asm
add:
    addw a0, a0, a1
    ret
```

Arguments arrive in `a0, a1`; the result goes back in a (RISC-V integer calling
convention: `a0- a7` for args/returns). The op is pure **register-to-register** - a
load/store ISA touches memory only via explicit `lw/sw/ld/sd`, never inside arithmetic.

### Ex 2 - `x0`, the hardwired-zero register

```asm
zero test:
seqz a0, a1
# set-if-equal-to-zero
ret
```

`seqz` compares against the zero register (`x0`, ABI name `zero`), which reads e and
discards writes. `x0` lets the compiler synthesize idioms (`mv rd,rs` = `addi rd,rs,0`,
`nop` = `addi x0,x0,0`, `seqz`/`snez`) instead of needing dedicated instructions.

## Ex 3 - C (compressed) extension: density

```
rv64gc (with C):
rv64g
(no C):
*text = 58 bytes
*text = 88 bytes
(~34% larger without C)
000000000
e: 2d 9d
2: 82 80
add a0, a1, a1 ret
<- 2 bytes = a COMPRESSED instruction
<- 2 bytes
```

The C extension adds 16-bit encodings of common instructions. In the disassembly,
compressed instructions are **2 bytes** vs 4 for full 32-bit encodings - directly visible,
and it cuts `.text` by a third here (I-cache/embedded footprint win).

### Ex 4 - M extension (the modular model made vivid)

```asm
# with M (rv64gs):
mul;
mulw
a0, a1, al
ret
* ONE hardware instruction
* without M (rv64i); mul;addi sp, sp, -15 sdra, 8(sp)...
cli_01013
... ret
• SOFTWARE multiply routine - no hw multiply in base ISAl
```

This is the single most instructive exercise: the **same c** complles to a one-instruction
`mulw` when the **M** extension is present, but to a **library call (`__muldi3`)** when
the target is bare `rv64i`, The modular base + extensions" model is right there in the codegen - the compiler emits different code based on which extensions the target advertises.

### Ex 5 - A extension: atomics

```asm
atomic_inc:
li    a1, 1
amoadd.w.agr1 at, a1, (a0)
ret
#atomic read-modify-write, acquiretrelease ordered
```

`atomic_fetch_add` lowers to an **AMO** (`amoadd.w`) with `aqrl` (acquiretrelease)
ordering bits. The A extension also provides **L/S** (load-reserved/store-conditional)
for CAS-style lock-free loops and fence' for ordering. The .agri" suffix is RISC-V's
**weak memory model (RVMO)** in action - ordering is explicit, so correct acquire/release
is mandatory (the portability trap from the multithreading discussion: x86's strong TSO
can hide missing-fence bugs that surface on RISC-V).

### Ex 6 - RV64 VS RV32

```asm
* RV64:
* RV32:
addw
add
a0,
a0,
a1
a1

(32-bit add + sign-extend into a 64-bit reg for "int') (register Is 32-bit, so plain add)
```

Same base ISA (I), different XLEN (register width). RV64 needs the `W` ("word", 32-bit)
form for `int` arithmetic in 64-bit registers; RV32 uses plain `add`. The RV321/rv64i
split, observable in one line.

## Ex 7 - running it (QEMU, optional)

Without QEMU installed the lab explains the workflow:

> To run RISC-V binaries on a non-RISC-V host: install `qemu-user` (`qemu-riscv64`) + a
> RISC-V sysroot, then `qemu-user` /rvapp". Emulation is the standard dev workflow.

With `gemu-riscv64`+ across sysroot, the labstatically links a RISC-V binary and runs
it under user-mode emulation, printing its output - demonstrating
develop/test-without-hardware.

## What each tool proved
- ***clang --target=riscv64-march=...*** - cross-codegen; the -march string (rv64gcl
  /rv64g/rv64i/rv32gc) selects the extension set and *changes the emitted
  instructions*.
- ***llvm-objdump-d*** -disassembly showing instruction bytes (2 vs 4 → compressed vs
  full) and mnemonics.
- **size'**-•.text footprint, quantifying the C-extension density win.
- ***`qemu-user`*** - user-mode emulation to actually run RISC-V code on x86.

## Setting up a RISC-V cross-toolchain and QEMU

The lab above only needs **clang** (any modern clang cross-compiles to RISC-V -
clang--print-targets | grep risc). To Link full binaries* and *run* them you
additionally want a RISC-V **sysroot/libc**, a **cross GCC** (optional, an alternative to
clang), and **EMU** (to execute RIS-V on a non-RISC-V host). Two flavors of target
matter:

- ***riscv64-linux-gnus* (Linux glibc target) → run with **gemu-user** (`qemu-riscv64`),
  the easy path for application code.
- **risc64-unknown-elf ** (bare-metal/newlib) → run with **qemu-system-riscv64**
  (full-system emulation), for firmware/embedded.

### Linux

**Debianl/Ubuntu**(apt):

```bash
sudo apt update
# Cross GCC + glibc sysroot for Linux userspace targets:
sudo apt install gcc-riscv64-linux-gnu g++-riscv64-linux-gnu
* QEMU: user-mode (run a single ELF) and system-mode (boot a kernel): sudo apt install emu-user `qemu-user`-static qemu-system-misc
# binutils + llvm tools you already likely have:
sudo apt install binutils-riscv64-linux-gnu llvm clang
# (optional) bare-metal/newlib toolchain:
sude apt install BCc-P'iscv64-unknown-elf
```

Verify:

```bash
riscv64-linux-gnu-gcc --version
gemu-riscv64 - -version
clang --target-riscv64-14nux-gnu-march=rv64gc-02 hello,c-0 hello • clang + the apt sysroot gemu-rlscv64 ./hello
# run it emulated
```

**Fedoral/RHEL** (dnf):

```bash
Sudo ant install gcc-risc4-linux-gnu binutils-riscv64-linux-gnu|
```

**Arch** (pacmanl/AUR):

```bash
sudo pacman-S risc4-linux-gnu-gcc `qemu-user` `qemu-emulators-full` clang llvm
# (newlib bare-metal via AUR: riscv64-unknown-elf-gcc)
```

**From source (any distro) - the official toolchain:**

```bash
git clone https://github.com/riscv-collab/riscv-gnu-toolchain
cd riscv-gnu-toolchain
# Linux/glibc toolchain (multilib optional):
/configure --prefix-/opt/riscv --enable-multilib make linux
# or "make' for the newlib bare-metal toolchain
export PATH-/opt/riscv/bin:$PATH
```

**transparent execution of RISC-V ELFs via binft_misc** (so you can run •/hello*
directly, no `qemu-user`" prefix): installing `qemu-user`-static" registers a binfmt
handler on most distros - then the kernel Invokes QEMU automatically.

### macos

macos has no native RISC-V toolchain packages, so use **Homebrew**:

```bash
* QEMU (both user and system emulation): brew install qemu
* A RISC-VGCCcross-toolchain (bare-metal/newlib, the common Homebrew tap): brew tap riscv-software-src/riscv brew install riscv-tools
*LLV/clang(clang already cross-compiles; brew's llvm gives llvm-objdump etc.):
* provides riscv64-unknown-elf-gcc, binutils, etc.
brew install llvm
```

Notes/caveats on macos:

- #`qemu-user` (single-ELF emulation) is Linux-only** - on macos you cannot
*gemu-riscv64 /elf a "Linux* RISC-V binary directly. Options:
- Use ***qemu-system-riscv64** (full-system) to boot a RISC-V Linux image, or
- Do Linux-userspace RISC-V work inside a **Linux VM/Docker/OrbStack/Lima** container on
  the Mac (then follow the Linux steps above - this is the most common macos workflow).
- For **bare-metal** development, `risc64-unknown-elf-gcc` +
  `qemu-system-riscv64 -machine virt -nographic -bios ...` works natively on macos.
- Apple-Silicon vs Intel Macs: Homebrew handles the host arch; QEMU emulates RISC-V
  regardless of whether the Mac is arm64 or x86_64.

Verify (macos, bare-metal):

```bash
riscv64-unknown-elf-gcc -march=rv64gc -mabi-1p64d hello.c -o hello.elf gewu-system-riscv64 virt -nographic -bios none -kernel hello.elf
# (needs a minimal runtime)
```

### Quick capability matrix

| Goal macos
| Toolchain
| Runner
| Linux |
- -0000
| Generate/read RISC-V asm
| **clang** (no sysroot needed)
|-
yes
| yes
| Link + run a Linux userspace ELF | risc64-linux-gnu-ce or clang+syroot | **gemu-riscv64** (user)
Linux VM/Docker |
| Bare-metall/firmware
| riscv64-unknown-elf-gcc
| **qemu-system-riscv64*»
yes
| yes
| yes
| Transparent /elf run
| any
| `qemu-user`-static + binfut | yes

### Sanity-check script (Linux)
```bash
cat › hello.c &<'EOF'
|
#include <stdio.h>
int main(){ printf("hello from RISC-V (RV64GC) \n"); return 0; } EOF
* Option A: clang + the apt-installed glibc sysroot
clang --target-riscv64-linux-gnu --sysroot=/usr/riscv64-linux-gnu\
- march-rv64gc -02 hello,c -o hello
* Option B: the cross GCC (simplest) riscv64-linux-gnu-gse -march-rv64gc -02 - static hello.c -0 hello
# -> hello from RISC-V (RV64GC)
-static sidesteps dynamic-loader/sysroot path issues under •gemu-usen' for dynamic binaries pass `qemu-user` -L /usr/riscv64-linux-gnu ./hello' so OEMU finds the RISC-V
*1d.so /libs.
### Host-verified status (this environment)
The setup was exercised on this host (Ubuntu 24.04, x86-64) after installing the cross-toolchains, **Verified working: *
- "riscv64-linux-gnu-gcc*l/-g++* **13.3.0**, "riscv64-unknown-elf-gcc" **13.2.0**,
*riscv64-linux-gnu-(objdump,readelf)" (binutils 2.42), and the glibc sysroot at
*/usr/riscv64-linux-gnu/.
**Cross-compile + static link** of a Linux RV64GC binary:
$ riscv64-linux-gnu-gcc -march-rv64gc -mabi-1p64d -02 -static hello.c -o hello_rv64
$ file hello rv64
ELF 64-bit LSB executable, UCB RISC-V, RV, double-float ABI, statically linked, •.. for GNU/Linux


$ riscv64-linux-gnu-readelf -h hello_rv64 | grep -E "Machine|Flags®
Machine: RISC-V
Flags:
0x5, RVC, double-float ABI
- **Disassembly** with the RISC-V binutils shows real RV64GC incl. compressed (2-byte) instructions and the ABI prologue:
$ riscv64-linux-gnu-objdump -d hello_rv64
10494: li41
addi sp,sp,-16
10496: 00043517
auipc a0, 0x43
1049e: e406
sd
ra, 8(sp)
104a0: 343000ef
jal
10fe2 ‹_10 puts>
104aa: 8082
ret
＃＜main>：
* 2-byte compressed
* save return addr(ra)
**Bare-metal newlib** toolchain complles freestanding code (loop shows "mulw" from M,
•blez/bne compare-and-branch, compressed "li"):
3 riscv64-unknown-elf-gcc-march-rv64ge -mabl-1p64d -02 -e freestanding.e -0 f5.0 • 0K
Note: riscv64-unknown-elf-gcc hello.c* with *#include ‹stdio.hs* **fails** by design - bare-metal/newlib needs explicit startup + syscal1 stubs (*-nostartfiles, a "write implementation, or a board BSP); it's not a hosted environment. Use riscv64-linux-gnu-gcc" for ordinary libc programs.
**Not yet installed here:**
QEMU ('`qemu-user`*/"qemu-system-riscv64`). So on this host
you can currently **cross-compile,
link, and inspect** RISC-V binaries, but **running**
them needs sudo apt install `qemu-user` emu-user-static qemu-system-misc" first; then
• genu-risev64 /hello_rv64" executes the static binary (the "-static bulld needs no -L sysroot flag).
#* Suggested extensions
1.    ***--quiz first** - predict the registers, whether "mul" is one instruction, and the rv64gc-vs-rv64g size ordering before revealing.
2.    **Add '-march-rv64gcv'* (vector) and a simple loop that sums an array; inspect whether the compiler auto-vectorizes to v* instructions.
3.    **Compare branch lowering:** write an "if/else and see how RISC-V uses beq/bne/blt" (compare-and-branch in one instruction, unlike x86's flags + jcc).
4.    **Install `qemu-user`" + a sysroot** and actually run sum to(100)` emulated.
5.    **Disassemble the no-M "mul'** fully ("llvm-objdump -d`) and trace the _muldi3* call \+ stack frame (addi sp,...,
"sd ra,...") - the standard RISC-V prologue/epilogue.
6. **Try "-00" vs *-02*** and watch the register allocator spill to the stack at •-06`.
The lab turns the write-up's claims into observable facts: ABI registers, "x0, the compressed-instruction density win, the extension model (M present vs. absent → hardware op vs. library call), weak-memory atomics with explicit ordering, and the RV32/RV64 split.
## Interview Q&A
**Q: The lab generates RISC-V code on an x86 host with no cross-toolchain for exercises 1-6. How, and what point does that prove?** A: Clang is natively multi-target, so
*clang --target-riscv64 -march=rv64gc* makes its backend emit RISC-V assembly/objects; you read them with "llvm-objdump"/size". You only need a RISC-V machine (or QEMU) to *run* the code, not to generate and inspect it. This reinforces "ISA + implementation" and that a compiler backend is a clean RISC-V code generator independent of host hardware.
*FoLLow-up:* When do you actually need QEMU? To *execute* a RISC-V binary on a non-RISC-V
host - *`qemu-user` ./app* (user-mode) for Linux ELFS,
qemu-system-riscv64" for
bare-metal/firmware. *Red flag:* Believing you need RISC-V hardware or a full
cross-toolchain just to see the generated instructions.
**0: Ex 4 is called the most instructive. What does the same "mul" C compile to with and without the M extension?** A: With M (rv64gc®) it's a single hardware instruction,
* mulw a0, a1, a0. Without M (rv64i) there is no hardware multiply, so the compiler emits a *software* routine call - call muldi3" wrapped in a stack-frame prologue/epilogue (addi sp,..., "sd ra,..."). The modular "base + extensions" model is visible directly in codegen: the compiler emits different code based on which extensions
*-march" advertises, *FolLow-up:* Why is there suddenly a stack frame in the no-M version?
Because it makes a function call ("muldi3"), so it must save the return address 'ra and realign the stack, *Red flag:* Assuming multiply is always a single instruction on any RISC-V target,
**Q: Ex 3 showed "rv64gc at 58 bytes of • text vs "rv64g' at 88. What's happening at the instruction level?** A: The C (compressed) extension adds 16-bit encodings of conmon instructions, In the disassembly compressed instructions are 2 bytes (e.g.. addaaddres) versus 4 for full encodings, cutting text
by about a third here - an I-cache and embedded-footprint win. The ISA is the same base; C just gives denser encodings of frequent ops. "FolLow-up:* How do you spot a compressed instruction in *llvm-objdump" output? Its byte count is 2, not 4, *Red flag;* Thinking C adds new operations rather than shorter encodings of existing ones.
**Q: In Ex 5, "_atomic_fetch_add lowered to amoadd.w.aqrl'.Decode that memonic and its significance,**A: amoadd.wis an atomic memory operation (read-modify-write) on a
32-bit word from the A extension; the
•aqrl suffix sets both acquire and release
ordering. That suffix is RISC-V's weak memory model (RVWMO) made explicit - ordering is encoded per-instruction, so correct acquire/release is mandatory. The A extension also provides LR/SC for CAS-style lock-free loops and fence for standalone ordering.
*FolLow-up: * Why does this connect to a portability trap? x86's strong TSO can hide a missing fence that an RVWMO target would expose, so x86-only-tested lock-free code can break on RISC-V. *Red fLag:* Reading • aqrl" as decoration rather than explicit emory-ordering semantics.
**Q: Ex 6 shows RV64 emitting `add`" where RV32 emits `add`" for the same `int` addition.
Why the w 3** A: It's the same base ISA (I), different XLEN. On RV64 registers are
64-bit, so 32-bit `int` arithmetic uses the "w" ("word") form that operates on the low 32
bits and sign-extends into the 64-bit register. On RV32 the register *is* 32 bits, so a

plain add suffices. One line of assembly captures the RV32I/RV64I split. *FolLow-up:* What would a 64-bit "long' add use on RV64} Plain `add`" ('addd isn't needed - `add`" is already XLEN-wide), with the "w` forms reserved for 32-bit operations. *Red flag:
Thinking add means different ISA rather than width form of the same base.
**Q: on this host QEM wasn't installed. What could and couldn't you do, and how would you enable running?** A: You can cross-compile, link (including
•-static ), and inspect RISC-V
binaries- risc64-linux-gnu-gcc" and the newlib bare-netal toolchain are present, and
•filel/readelfl/objdump* confirm RV64GC with compressed instructions and the ABI prologue. What you can't do is run* them without QEMU. Installing
gemu-user/`qemu-user`-static" lets •qemu-risev64 ./hel1o_rv64" execute i statie binary; E static build needs no -L* sysroot flag, while dynamic binaries need
*qemu-risc64 -L/usr/riscv64-linux-gnu ./hello, *FolLow-up!* Why does
•riscv64-unknown-elf-gcc hello.c' with •<stdio.h›* fali by design? Bare-metal/newiib is freestanding -no hosted libe/startup-soit it needs*-nostartfiles plus syscel1 stubs or SP; use riscu64-linux-gnu-gcc for ordinary libc programs. *Red flags* Expecting E bare-metal newlib toolchain to link hosted printf program out of the box.