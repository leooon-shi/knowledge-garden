#l/usr/bin/envbash
# riscv_practice.sh
# A hands-on lab to SEE RISC-V assembly and ISA features, companion to
# interview/riscv_cpu_architecture.md.
#
# It uses clang as a cross-compiler (clang is natively multi-target) to emit
*
# RISC-V (RV64) code from C, then reads it to observe:
1. The ABI registers (a0, a1, sp, ra, ...) and load/store RISC design
#
2. xe (the hardwired-zero register) in generated code
#
1.    The C (compressed) extension: 16-bit instructions + code-density win
2.    The M extension (mul/div) appearing only when enabled
#
1.    The A extension atomics (1r/sc, amo*) for synchronization
2.    RV64 vs RV32 differences
3.    (optional) run it under QEMU if available
# Usage:
./riscv_practice.sh
./riscv_practice.sh --quiz
•/riscv_practice.sh <N>
# all exercises with explanations
# hide explanations
# one exercise (1..7)
* Requires: clang with risc targets (clang --print-targets | grep riscv).
set -u
11vm-objdump optional (nicer disassembly); qemu-riscv64 optional (to run).
WORK-"$ (mktemp -d)"; trap "rm -rf "SWORK"* EXIT QUIZ-0; ONLY-**• for a in
"$0"; do case "$a" in --quiz) QUIZ-1;; [0-9]*) ONLY="$a";; esac; done
bold) printf 1033[1m%s \033[em\n° "g*"; )
grn()
rule(){ printf '%sln' "-------
I "SQUIZ" -eg 18 printf 1033[32m%s \033[0m\n* "g**: 3
have) command -v "$1" >/dev/null 2>81; } 9( printf "In
•; bold "Q: $*"; }
have clang I| { echo "need clang"; exit 1; }
clang --print-targets 2›/dev/null | grep -q riscv64 || { echo "this clang lacks riscv64 target"; exit 1; } CC=clang
OBJDUMP=""; have 11vm-objdump && OBJDUMP="11vm-objdump"
echo "Work dir: $WORK"; echo
sample C
cat >
"$WORK/demo.c" «'EOF•
int
add (int a, int b)
int
mul (int a, int b)
int
zero_test int a)
int
sum_ to(int n) {
int s = 0;
for (int i = 1; i ‹= n; +i) s += i; return s;
{ return a + b; }
// trivial: ABI regs
{ return a * b; }
// needs M extension
{ return a = 0? 1
: 0; } // may use xe
Il a loop
int
EOF
atomic_inc (int" p)
{return atomic_fetch_add(p, 1, ATOMIC_SEQ_CST); } // A ext
asm{# asm <march› <outfile>
"$CC"
--target-riscv64 -march-"$1" -02 -S "$WORK/demo.c" -0 "$2" 2›/dev/nu11
}
show_fn(){# show_n <asmfile› <label›: print a function body from assembly
awk -v f-"$1:" '$0 ~ "A"f {p=1} píprint "
"$0} p && $0 ~ /ret|jr[ \t]+ra/ {exit}• "$2" 2›/dev/nu11
ex1() {
rule; bold "Exercise 1 - RISC-V assembly, ABI registers & load/store design"; rule asm rv64gc "$WORK/gc.s"
q What registers carry the two int arguments and the return value of add(a,b)?"
show_fn add "SWORK/gc.s"
grn
→ Args in ab,a; result in a (the RISC-V integer calling convention: a0-a7)."
grn *
"addu a,al, al' then 'ret'. Pure register-to-register arithmetic - load/store ISA:"
grn "
memory is only touched by explicit Iw/sw/ld/sd, never by arithmetic ops."
ex2() {
rule; bold "Exercise 2 - xe, the hardwired-zero register"; rule asm rv64gc "$WORK/gc.s"
q "Where does
"zero' / xe show up? (look for 'zero' or '11 …e' idioms)"
show_fn zero_test "$WORK/gc.s"
echo
--- all uses of the zero register in the file: --."
grep -nE
\bzero\b|, Is*zero' "$wORk/gc.s" | head -5 | sed 's/~/
grn
=> xe (ABI name 'zero') reads as e and discards writes. The compiler uses it as a"
grn "
constant-zero source and to synthesize idioms (e.g.,
"mv rd,rs' - 'addi rd,rs,0'."
grn "
'seqz' I'snez' comparisons against zero), It removes the need for many special ops,"
ex3() {
rule; bold "Exercise 3 - the C (compressed) extension: density win"; rule asm rv64gc "$wORk/gc.s"; asm rv64g "SWORK/g-s"
"$C" --target=riscv64 -march-rv64gc -02 - c
"$WORK/demo.c" -0
"SWORK/demo.c"
"SwORk/gc.o" 2>/dev/nu11
"$CC*
--target=riscv64 -march=rv64g
-02 -c
-0
"SWORK/g.o"
2>/dev/nu11
q "How much smaller is text with the C extension (rv64gc) vs. without (rv64g)?" tgc-$(size "$WORK/gc.o" 2>/dev/nu11 | awk 'NR=-2{print $1)')
tg=$ (size
"SWORK/g.o"
2›/dev/nu11 | awk "NR--2(print $1)')
printf »
rv64gc (with C):
text = %s bytes\n" "$(tgc: -?)"
printf
rv64g
(no C) :
.text - %s bytes\n" "${tg:-?)"

if [ -n "$OBJDUMP"
echo
"
1: then
fi
--- add( disassembled (note 2-byte compressed instrs under rv64gc) ---*
"SOBJDUMP™ -d "$WORK/g.o" 2>/dev/null | awk/<add>:/(p-1) p(print "
grn "
grn
grn "
"$0} p&8/ret/(exit)'
= The C extension adds 16-bit encodings of common instructions, cutting code size" (~25-37% here). In the disassembly, compressed instrs show 2 bytes (e.g.,
'2d 9d')"
vs 4 bytes for full 32-bit encodings. Density helps I-cache/embedded footprint."
ex4() {
rule; bold "Exercise 4 - the M extension (mul/div only when enabled)": rule asm rv64gc
"$WORK/gc.s"
asm rv64i
"$WORK/i.s"
# has M (G includes M)
# base only, NO M
q "Does mula, b) become a single "mul" instruction, or a library/software call?"
echo
echo
--- with M (rv64gc): ---"; show fn mul "$WORK/gc.s"
grn
... without M (rv641): --"; show fn mul "$WORK/1.5"
grn
→ With M, 'mul al,a, al" is one hardware instruction. Without M, there is NO multiply"
grn
instruction in the base ISA, so the compiler emits a call to a software routine"
grn
(mulsi3/ muldi3) or shifts/adds. This is the modular-extension model made visible:" the SAME C compiles differently depending on which extensions the target advertises."
rule; bold "Exercise 5 - the A extension: atomics (Ir/sc, amo*)"; rule asm rv64gc "$WORK/gc.s"
# A included in G
q "How is atomic_fetch_add lowered on RISC-V? (look for amoadd / Ir / sc)" echo
--- atomic ops anywhere in the file: --_"
grep -nE *\b(amo[a-z]+|Ir\.|sc\.|fence)\b° "$wORk/gc.s" | head -5 | sed s/~/
gn "
= The A extension provides AMOs ('amoadd.w') for RMW and LR/SC
grn
(Load-reserved/store-conditional) for lock-free CAS-style loops, plus 'fence' for"
grn "
ordering. RISC-V's weak memory model (RVWMO) means correct fences are mandatory-"
grn "
the portability trap from the multithreading discussion."
"$CC"	--target=riscv64 -march=rv64gc -02 -S "$WORK/demo.c" -0
	
"$CC"	--target=riscv32 -march=rv32gc -02 -S "$WORK/demo.c" -0 "$WORK/32.s" 2>/dev/nu11
9	"What changes between RV64 and RV32 for 'add'? (instruction mnemonics / word size)"
echo	--- RV64 add: ---"; show_fn add "$WORK/64.s"
echo "	--- RV32 add: ---*; show_fn add "$WORK/32.s"
	grn " = XLEN differs (64 vs 32 bit registers). RV64 uses 'addw' (32-bit add, sign-extend)"
grn "	for "int'; RV32 uses plain
	
rule; bold "Exercise 6 - RV64 vs RV32"; rule
"$WORK/64.s" 2›/dev/nu11
grn "
'add' since the register IS 32-bit. Same base ISA (I),"
different register width - the RV32I/RV64I split."
ex7() {
rule; bold "Exercise 7 - run it (QEMU, optional)"; rule if have qemu-riscv64 || have qemu-riscv64-static; then cat › "$WORK/main.c" «'EOF"
#include <stdio.h>
int sum_to(int n)(int s=0;for(int i=1;i<=n;++i)s+=i;return s;}
int main){printf(" sum_to (108)-%d\n", sum_to (108)); return e;}
EOF
if "$CC" --target=riscv64-linux-gnu -march=rv64gc -02
-static "$wORK/main.c" -0 "$WORK/rvapp" 2>/dev/null; then
9
"Run the RISC-V binary under QEMU user-mode emulation:" local emu; gemu-$(command -v qemu-riscv64 || command
-v qemu-riscv64-static)
run_out-$("$qemu" "$WORK/rvapp" 2>81 || echo "(qemu run failed - may need sysroot) ") printf "
%s \n" "Srun_out"
grn "
=> The RISC-V ELF runs via QEMU translating RV64 instructions to the host. This is"
Ern "
how you develop/test RISC-V software without physical RISC-V hardware."
else
echo •
grn
grn
(could not statically link a RISC-V binary here - needs a risc sysroot/libc)" => clang can CODEGEN RISC-V, but linking a full binary needs a RISC-V libc/sysroot."
With a cross-sysroot + qemu-riscv64 you'd run it emulated on this x86 host."
fi else
echo "
grn
grn
(qemu-riscv64 not installed; skipping execution) "
→ To run RISC-V binaries on a non-RISC-V host: install qemu-user (qemu-riscv64) and a"
RISC-V sysroot, then 'qemu-riscv64 /rvapp'. Emulation is the standard dev workflow. "
fi
if [ -n "SONLY" ]; then "ex$ONLY"; else for 1 in 1 2 3 4 5 6 7; do "ex$1"; done; fi echo; rule
bold "Done. Re-run with --quiz to self-test, or N for one exercise."
bold "Tools: clang (cross-compile), 11vm-objdump (disassemble), size (density), qemu (run)."
