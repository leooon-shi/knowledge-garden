<!-- interview/riscv_cpu_architecture.md -->

# RISC-V CPU Architecture

**Status note:** the base ISA and ratified extensions (I, M, A, F, D, C, and now V) are
stable; some extensions and profiles are newer. Flagged where it matters.

## What it is and why it matters

RISC-V ("risk-five") is an **open, royalty-free Instruction Set Architecture (ISA)** from
UC Berkeley (2010), governed by RISC-V International. Unlike proprietary x86 (Intel/AMD)
and licensed ARM, **anyone can implement a RISC-V core without royalties or a license** -
the single most consequential fact. It's a specification*, not a chip; many vendors
implement it (SiFive, Andes, Alibaba T-Head, academic cores).

Design philosophy: textbook **RISC** modernized - a small, clean, f1xed-size instruction
set easy to implement, pipeline, and extend, explicitly structured as a **frozen base +
optional extensions** so software stays stable as the ISA grows.

## Core ISA design principles

- **Load/store architecture** - only load/store touch memory; arithmetic is
  register-to-register (contrast x86 memory operands).
- **Fixed 32-bit base instructions** (+ 16-bit **C** compressed extension for density) →
  simple decode vs. x86's variable 1-15 byte instructions.
- **Small base** - RV32I/RV64I is ~40 instructions; everything else is a modular
  extension.
- **Base variants by register width:** **RV32I** (32-bit), **RV64I** (64-bit, mainstream
  for app processors), **RV128I** (rare/forward-looking); **RV32E** (embedded, 16 regs)
  for tiny MCUs.
- **Regularity** - orthogonal encoding, few addressing modes, no implicit side effects →
  straightforward decoder/pipeline (deliberate contrast to CISC accretion).

## Registers

- **32 general-purpose integer registers** `x0`-`x31`, XLEN-wide. **`x0` hardwired to
  zero** (reads 0, writes discarded) - eliminates many special instructions (`nop` -
  `addi x0,x0,,0`, `mv` = `addi rd,rs,0`).
- **`pc`** program counter (not in the GPR file).
- **ABI names** overlay numbers: `ra` (x1 return addr), `sp`(x2), `gp`, `tp`,
  `a0-a7`(args/returns), `t0-t6`(caller-saved temps), `s0-s11` (callee-saved). Calling
  convention maps onto these.
- With **F/D**: separate bank of 32 FP registers `f0`-`f31`.

## Modular extension model (the defining feature)

***Base + extensions**,* named by letters; a core advertises e.g. **`RV64GC`**:

- **I** base integer (mandatory) • **M** mul/div • **A** atomics (LR/SC + AMOs) • **F**
  single-float • **D** double-float • **C** compressed 16-bit.
- **G** = **IMAFD** (+Zicsr/Zifencei) = "general-purpose." So **RV64G** - standard
  Linux-capable app profile.
- Newer/notable: **V** vector (scalable, big for ML/HPC), **B** bit-manip, **H**
  hypervisor, **zb*/Zk*\*\* sub-extensions (bitmanip/crypto), many "Z" extensions.

Modularity → a tiny MCU ships `RV32IMC`, a Linux server ships `RV64GC`+V - *same base,
different capabilities*. **profiles** standardize extension bundles so software has stable
targets (taming fragmentation). **RVA23 was ratified in October 2024** and is the current
application-processor target: it makes **V (vector 1.0) mandatory**, along with bitmanip
(Zba/Zbb/Zbs), Zicond, Zfa, Zcb, and others, plus hypervisor (H) in the RVA23564 variant.
RVA22 (2022) was the prior baseline; Ubuntu 25.10+ targets RVA23.

## Privilege levels (system architecture)

- **M-mode (Machine)** - highest, mandatory, runs firmware (OpenSBI); the only mode a
  minimal MCU needs.
- **S-mode (Supervisor)** - OS kernel (Linux); page-based virtual memory (Sv39/Sv48/Sv57).
- **U-mode (User)** - applications.
- **(H) Hypervisor** extension augments S-mode for virtualization.
- **CSRs (Control/Status Registers)** hold system state (trap vectors `mtvec`/`stvec`,
  `mcause`,`mepc`, status, interrupts) **SBI (Supervisor Binary Interface)** is the
  standard S-mode → M-mode firmware call interface (BIOS/firmware-ABI analog).

## Memory model & atomics

- **A extension:****R/SC** (Load-reserved/store-conditional - lock-free primitive) and
  **AMOs** (`amoadd`, `amoswap`,...).
- Formally specified **weak memory model (RVWMO)** with explicit **`fence`** instructions.
  Like ARM (unlike x86's strong **TSO**), RISC-V reorders - so lock-free code must get
  acquire/release right or it breaks on RISC-V even if it worked" on x86 (ties to the
  multithreading/memory-ordering discussions).

## Comparison

| Aspect        | RISC-V                   | ARM                       | ×86-64                       |
| ------------- | ------------------------ | ------------------------- | ---------------------------- |
| Licensing     | **Open, royalty-free**   | Licensed (fee + royalty)  | Proprietary                  |
| Type          | RISC, load/store         | RISC, Load/store          | CISC (RISC-like µops Inside) |
| Instr size    | Fixed 32-bit (+16-bit C) | Fixed 32-blt (AArch64)    | Variable 1-15 bytes          |
| ISA structure | Small base + modular ext | Larger fixed + extensions | Large accreted               |
| Memory model  | Weak (RVWMO) + fences    | Weak + fences             | Strong (TSO)                 |
| GPRs          | 32, x0=zero              | 31 (AArch64)              | 16                           |
| Ecosystem     | Growing                  | Very mature               | Very mature                  |

## Benefits & challenges (honest)

**Benefits：**

- **No royalties/licensing** → low barrier to custom silicon; big for startups, academia,
  sovereign-chip efforts (reducing ARM/x86 dependence).
- **Customizability** - reserved encoding space for **custom extensions** (ML/DSP
  accelerators) atop a standard base + standard toolchain.
- **Clean modern design** - no legacy baggage; easy to teach/implement/formally verify.
- **Modularity** - one base scales from 32-bit MCU to 64-bit vector server.

**Challenges:**

- **Fragmentation risk** - different extension sets → non-interchangeable binaries;
**profiles (RVA20/22/23)** tame this.
- **Ecosystem maturity** - GCC/LLVM support is good now, but distros and high-performance
  app cores lag x86/ARM; 11brary tuning catching up.
- **Performance**-leading RISC-V appcoresimproving fast but historically behind top
  ARM/x86; gap closing, strongest in embedded/accelerator niches.

## Staff / Principal-level framing

- **The open ISA is the strategic point, not the microarchitecture.** The design is clean
  but not radically novel vs. ARM; the *disruptive* property is being royalty-free,
  changing custom-silicon economics and lock-in. Reason about adoption through that lens.
- **ISA → implementation.** RISC-V specifies *what* instructions do, not *how fast*; the
  same ISA spans tiny in-order MCUs and wide OoO superscalar cores. "RISC-V is slow" is
  a misconception-speed is microarchitecture (pipelining, OoO, caches, branch prediction),
  orthogonal to the ISA.
- **Modular/custom extensions are double-edged** - enable domain-specific acceleration
  (the ML/edge draw) but create the fragmentation profiles must contain. Keeping the
  ecosystem coherent while allowing customization is the hard problem.
- **Ties to compiler/backend material** - a RISC-V backend is a *clean*
  instruction-selection target (regular, orthogonal); custom extensions mean
  compiler-writers add intrinsics/patterns to LLVM/GCC; load/store + 32 regs + x0=zero
  make codegen and register allocation comparatively straightforward.
- **Weak memory ordering is a portability trap** - code validated only on x86 (strong TSO)
  can hide missing-fence bugs that surface on RISC-V/ARM. RISC-V's growth makes correct
  `std::memory_order` non-optional.

## Interview 0&A

**Q: What is the single most consequential fact about RISC-V, and why is it more important
than the microarchitecture?** A: It's an open, royalty-free ISA - anyone can implement a
RISC-V core without a license or royalties. That's the disruptive property because the
design itself is clean but not radically novel versus ARM; what changes the game is
silicon economics and the end of lock-in, which is why it matters for startups, academia,
and sovereign-chip efforts. You reason about adoption through the open-standard lens, not
through any architectural trick. *Follow-up:* So is "RISC-V is slow" a fair statement? No
- ISA * implementation; speed is a microarchitecture question (pipelining, OoO, caches,
branch prediction), orthogonal to the ISA, and the same ISA spans tiny in-order Mous to
wide OoO cores. *Red flag:* Praising RISC-V primarily for novel instructions rather than
its licensing/economics.
  
**Q: Explain the "base + extensions" model. What does `RV64GC` decode to?* A: RISC-V
freezes a small base integer ISA (RV321/RV64I, ~40 instructions) and layers optional
lettered extensions: M (mul/div), A (atomics), F/D (single/double float), c (compressed
16-bit), V (vector), etc. `G` is shorthand for IMAFD plus Zisr/Zifencei - "general
purpose" - so `RV64GC` is the 64-bit general-purpose, compressed, Linux-capable
application profile. A tiny MCU might ship `RV32IMC` instead: same base, different
capabilities. *Follow-up:* Why freeze the base? So software stays stable as the ISA grows
- the base won't change out from under existing binaries. *Red flag:* Thinking the
extensions are vendor-proprietary forks rather than standardized modular pieces.

**Q: What is RVAZ3, when was it ratified, and why does it matter?** A: RVA23 is the
current application-processor profile, ratified in October 2024. It matters because it
makes the V (vector 1.0) extension *mandatory*, along with bitmanip (Zba/Zbb/Zbs), Zicond,
Zfa, Zcb, and others, plus hypervisor (H) in the RVA23564 variant. Profiles exist to tame
fragmentation by giving software stable extension bundles to target; RVA22 (2022) was the
prior baseline, and Ubuntu 25.10+ targets RVA23. *Follow-up:* What problem do profiles
solve? Fragmentation - arbitrary extension combinations make non-interchangeable binaries,
so profiles standardize what an application core must provide. *Red flag:* Claiming vector
support is optional on a modern RVA23 application core, or misdating the ratification.

**Q: `x0` is hardwired to zero. What does that buy the ISA?** A: Reads return and writes
are discarded, which lets the compiler synthesize common idioms without dedicated
instructions: `nop` = `addi x0,x0,0`, `mv rd,rs` = `addi rd,rs,0`, and comparisons like
`seqz`/`snez` against zero, It's a small design choice that removes the need for several
special-case instructions, keeping the encoding regular, "Follow-up:* How many GPRS are
there and what's the embedded variant? 32 Integer registers `x0`-`x31` (XLEN-wide); RV32E
halves that to 16 for tiny MCUs, *Red flag;" Treating `x0` as a normal writable register.

**Q: RISC-V uses a weak memory model, What's the portability trap for code only tested on
x86?** A: RISC-V's RMO 15a formally specified weak* model that reorders memory
operations and needs explicit `fence` instructions (11ke ARM, unlike x86's strong TSO).
Lock-free or atomic code that "worked" on x86 can hide missing-fence or wrong
`memory_order` bugs that only surface on RISC-V/ARM, because x86's strong ordering masked
them, RISC-V's growth makes getting acquire/release correct non-optional, *Follow-up:*
What does the A extension provide for this? L/SC(Load-reserved/store-conditional) for
lock-free CAS 1oops and AMOs (`amoadd`, `amoswap`), with explicit ordering bits, *Red
flag:* Assuming x86-validated concurrent code is automatically correct on RISC-V.

**Q: Describe the privilege levels and the role of SBI,** A: Three main modes: M-mode
(Machine, highest, mandatory, runs firmware 11ke OpenSBI - the only mode a minimal MCU
needs), S-mode (Supervisor, the 05 kernel with page-based virtual memory Sv39/Sv48/Sv57),
and U-mode (User, applications); the H extension augments S-mode for virtualization.
System state lives in CRs (trap vectors `mtvec`/`stvec`, `mcause`, `mepc`,
status/interrupt registers). SBI (Supervisor Binary Interface) is the standard call
interface from S-mode to M-mode firmware - the firmware-ABI analog of a BIOS call.
*Follow-up:* Why might an embedded core only implement M-mode? A minimal MCU with no OS or
virtual memory needs only the mandatory machine mode. *Red flag:* Confusing CRS with
general-purpose registers, or not knowing SBI is the S→M firmware interface.

**Q: Why is RISC-V considered a clean compiler-backend target, and what's the double-edged
part?** A: Load/store architecture, 32 orthogonal registers with `x0`=zero, fixed-size
regular encoding, and few addressing modes make instruction selection and register
allocation comparatively straightforward. The double-edged part is custom extensions:
reserved encoding space lets vendors add domain-specific (ML/DSP) instructions atop the
standard base, which compiler writers expose via LLV/GCCintrinsics and patterns - great
for acceleration, but it fuels the fragmentation that profiles must contain. *Follow-up:*
How does a compiler decide to emit a vector or custom-extension instruction? Based on the
-march string advertising which extensions the target supports. *Red flag:* Claiming
custom extensions are free lunch with no ecosystem/fragmentation cost.

## Synthesis

RISC-V is an **open, royalty-free, modular RISC ISA** (RV32/RV64/RV128; mainstream
**RV64GC**) built on a **small frozen base integer set + optional lettered extensions**
(M, A, F/D, C, V, ...), with a classic **load/store, fixed-32-bit-instruction,
32-register* design where **`x0` is hardwired zero**. Its system layer defines **M/S/U
privilege modes**, **CSRs**, trap handling, and the **SBI** firmware interface, with a
formal **weak memory model (RVO)** needing explicit `fence`s plus LR/SC+ AMO atomics
from the **A** extension. Its defining property is being an **open standard** -
royalty-free implementation and reserved space for **custom extensions** - disrupting
silicon economics and lock-in more than any microarchitectural novelty; the trade-offs are
**fragmentation** (managed by #*profiles RVAx**) and an **ecosystem/performance gap** vs.
x86/ARM that is closing fastest in embedded/accelerator niches. Principal view: **ISA ≠
implementation** (RISC-V's speed is a microarchitecture question), the open+modular model
is both its superpower and its fragmentation risk, and it connects directly to compiler
backends (a clean codegen target) and the concurrency/memory-model concerns (weak ordering
makes correct fences non-optional - a portability trap for x86-only-tested code).
