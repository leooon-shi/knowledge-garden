#l/usr/bin/envbash
# sanitizer_codegen_practice.sh
# Hands-on lab: what sanitizers CATCH, and what CODE the compiler emits to catch it.
# Companion to interview/sanitizers_and_codegen.md.
# Exercises:
1.    ASan
2.    ASan
- heap-buffer-overflow report
3. ASan
4.
-   codegen: the shadow-memory check inserted before a load
-   use-after-free (quarantine)
5.
6.
UBSan-signed-overflow report + codegen (overflow branch -> handler call
UBSan - trap mode: same check, no runtime library (ud2)
7. MSan
8.
TSan - data race report + codegen (tsan_read/_tsan_write per access)
LSan
- use of uninitialized memory (clang only)
9. Cost
-   memory leak at exit
-   code size and run time with and without ASan salizer codegen_pracucesn
Stop message_framing.cpp
Usage:
•/sanitizer_codegen_practice.sh
./sanitizer_codegen_practice.sh --quiz ./sanitizer_codegen_practice.sh <N>
# all exercises with explanations
# hide explanations
# one exercise (1..9)
# Requires: clang (x86-64 host; assembly greps assume x86-64). 11vm-symbolizer optional.
set -u
WORK=*$(mktemp
QUIZ-0; ONLY= for a in
-d"; trap rm -rf "$WORK"' EXIT
do case "$a" in --quiz) QUIZ-1;; [0-9]*) ONLY="$a";; esac; done
bold){ printf *1033[1m%s\033[0m\n' "$*"; }
grn()f
-
"SQUIZ"
rule(){ printf %s\n"
-eq] printf 1033[32m%s\033[0m\n° "g*"; }
q(){ printf "In
have){ command -v "$1" >/dev/null 2>81; } ind(){ sed 's/^/
/:}
-"; }
have clang 11 { echo "need clang"; exit 1; } CC-clang
export ASAN_OPTIONS-"detect_leaks-0: symbolize-1" # leaks only in ex 8 export UBSAN_OPTIONS-"print_stacktrace=1" export TSAN_OPTIONS-"halt_on_error-®"
# Run
a sanitized binary; retry with ASLR off if the sandbox rejects the shadow mapping.
runsan(){
local out; out-$("$@" 2>81) if printf %s'
"Sout" 1 grep -q
else printf •%s\n' "Sout"; fi
"unexpected memory mapping"; then setarch -R "$®" 2>81
# Print the body of one function from an x86-64 assembly file.
fn_asm() awk -v f-"$1:" $1-=f{p=1;next} p&&/^\s*|.cfi_endproc/(exit)p&&!/^\s*\.cfi|p2align|1oc|file)/(print} "$2"; )
bold "Using: $CC $ $CC --version | head -1 | sed 's/.*version /v/° }" echo "Work dir: $WORK"; echo
rule; bold "Exercise 1 - ASan: heap-buffer-overflow"; rule cat > "$WORK/ovf.c" «'EOF'
#include <stdlib.h>
int main(void)(
int *a = malloc(4 * sizeof(int));
a[4] = 42;
/* one past the end */
free(a);
return 0;
EOF
# -08 on purpose: at -01 clang deletes the whole malloc-›store-›free sequence as dead
# code, so there is no overflow left to catch. Sanitizers only see code that survives
# optimization.
$CC -g -00 -fsanitize-address -fno-omit-frame-pointer "$WORK/ovf.c" -0 "$WORK/ovf" q "What does ASan report, and where?"
runsan "$WORK/ovf" | grep -E "ERROR|WRITE of size|#0|located.* bytes" | head -4 | Ind grn
=>
'heap-buffer-overflow ... WRITE of size 4°, the faulting line (#0 main ovf.c:4), "
grn "
and 'e bytes after 16-byte region' - the redzone ASan placed after the allocation."
grn "
Without ASan this write silently corrupts the heap; with it, the process stops here."
grn
Built at -00 because at -01 the optimizer removes the dead malloc/store/free and"
grn •
the bug disappears from the binary; sanitizers check the code that survives."
}
rule; bold "Exercise 2 - ASan codegen: the shadow check before every load"; rule cat > "$WORK/load.c" <<'EOF'
int load(int *p){ return *p; }
EOF
$CC -01
-s "SWORK/load.c" -o "$WORK/plain.s"
$CC -01
-  S
-  fsanitize-address "$WORK/load.c" -0
"SWORK/asan,s"
q "Compare load() without and with -fsanitize-address. What was inserted?" echo"
--- plain
";
f_asm load "Swork/plain.s"| Ind
echo
ASan ---*:
f_asm load "$wORk/asan,s" | ind
grn
=> plain: one
*mov1 (%rdi), %eax'. ASan adds, before the load:"
grn
shadow = (addr > 3) + 0x7fff8000
(shra \$3 + the ex7fff8000 offset)"
grn
load the shadow byte; if non-zero, check the partial-granule case;"
grn
on a bad access, cal1
_asan_report_load4 (never returns)."
grn
1 shadow byte describes 8 application bytes. That compare+branch on every"
grn
memory access is where ASan's ~2xslowdown comes from."

eye_practice.sh
ex3()
rule; bold "Exercise 3 - ASan: use-after-free"; rule cat > "$WORK/uaf.c" «'EOF"
#include <stdlib.h>
int main(void) {
int *p= malloc(sizeof (int));
free(p);
return *p;
/* read after free */
}
EOF
$CC -g -01 -fsanitize-address -fo-omit-frame-pointer "$WORk/uaf.c" -o "$WORK/uaf" q "How does ASan know the memory was freed?"
runsan "$wORK/uaf" | grep -E "ERROR|READ of size|freed bylpreviously allocated" | head -4 | ind grn "
grn "
gen "
→ free() poisons the shadow of the chunk and keeps it in a QUARANTINE instead of" reusing it immediately, so the later read hits poisoned shadow. The report shows" three stacks: the bad read, the free, and the original malloc."
ex4()(
rule; bold "Exercise 4 - UBSan: signed overflow, report + codegen"; rule cat > "$WORK/ub.c" «<'EOF"
#include <limits.h>
#include <stdio.h>
int add(int a, int b)[ return a + b; }
int main(void){ printf(%d\n", add(INT_MAX, 1)); return e; } EOF
$CC
-g -01
$CC -01
-S
-fsanitize=undefined "$wORk/ub.c" -o "$WORK/ub"
$CC -01
"$WORK/ub.c" -0 "$wORk/ub_plain.s"
-  S
-  fsanitize-undefined "$wORk/ub.c" -0 "$WORK/ub_san.s"
q "What does UBSan report, and what code did it add to add()?" runsan "$wORK/ub" | grep -E "runtime error" echo "
1 head -2 | ind
--- add() plain ---";
echo"
f_asm add "$wORk/ub_plain.s" | ind
-- add() UBSan ---";
fn_asm add "$work/ub_san.s"
grn "= signed integer overflow: 2147483647 + 1 cannot be represented in type int'." grn "
grn "
Plain add() is a single lea/add. UBSan uses the CPU overflow flag: "addl' then"
grn "
"jo' to a slow path that calls _ubsan handle_add_overflow with a static" descriptor (file, line, type). The check costs one branch on the fast path."
ex5(){
rule; bold "Exercise 5 - UBSan trap mode: no runtime library"; rule $CC -01
-  S
-  fsanitize-signed-integer-overflow -fsanitize-trap=signed-integer-overflow |
"$WORK/ub.c" -0
*$WORK/ub_trap.s* 2>/dev/nul1 || (
cat > "$WORK/ub.c" «'EOF"
#include <limits.h›
#include <stdio.h›
int add (int a, int b){ return a + b; }
int main(void){ printf("%d\n", add(INT_MAX, 1)); return e; } EOF
$CC -01-5-fsanitize-signed-integer-overflow-fsanitize-trap=signed-integer-overflow\
"$wORK/ub.c" -o "$WORK/ub_trap.s"; }
q "With -fsanitize-trap, what replaces the handler call?" fn_asm add "$WORK/ub_trap.s" | ind
grn
→ jo now jumps t an undefined instruction (clang 18 emits 'ud1' with the check"
grn "
kind encoded in the operand; older compilers and GCC emit 'ud2') - SIGILL."
grn "
No diagnostic text, no runtime library to link. That's why trap mode is used in"
grn "
production hardening and kernels: the check stays, the reporting machinery goes."
rule; bold "Exercise 6 - TSan: data race, report + codegen"; rule cat › "SWORK/race.c" «<'EOF*
#include <pthread.h>
/* shared, unprotected */
int counter;
void *work(void *arg){ for (int i = 0; i < 1000; ++i) counter++; return arg; }
int main(void){
pthread_t t1,
t2;
pthread_create(&t1, e, work, 0); pthread_create(&t2, e, work,
0);
pthread_join(t1, 0); pthread_join(t2, 0);
return 0;
}
EOF
$CC -g -01 -fsanitize-thread
"SWORK/race,c"-o "$WORK/race"-pthread
$CC -01 -S -fsanitize-thread
"SWORK/race.c" - "$WORK/race_tsan, s"
q "What does TSan report, and what did it insert into work()?"
runsan "$WORK/race" | grep -E "WARNING|Write of size|Previous write|Location is global" | head -4 | ind echo
--- TSan runtime calls in work(): ---"
-
f_asm work "SWORk/race_tsan,s" | grep -E "
tsan_" | sort | unia -c | ang
grn
=>
•data race ... Write of size 4' with both threads' stacks and the global "counter'."
grn
Codegen: every shared load/store becomes a call (__tsan_read4/_tsan_write4), plus"
grn "
tsan_func_entry/exit for stack traces. The runtime keeps a vector clock per"
grn "
thread and shadow state per word; that's why TSan costs ~5-15x time and ~5-10x memory."
ex7()
rule; bold "Exercise 7 - MSan: use of uninitialized memory (clang only)"; rule cat >
"$WORK/uninit.c" «<'EOF'
#include <stdio.h>
#include <stdlib.h>

int main int arge, charargv)
int *x = malloc(sizeof (int));
if (*x> argc) puts ("taken");
free (x); return 0;
raming. cpp
/* never initialized */ /* branch on garbage */
EOF
if $CC -g -01 -fsanitize-memory -fno-omit-frame-pointer "$WORK/uninit.c" -o "$WORK/uninit" 2>/dev/nu11; then q "Where does Man stop: at the read, or at the branch?" runsan "$woRk/uninit" | grep -E "WARNING|#0" | head -2 | ind grn
grn
grn
•use-of-uninitialized-value' at the BRANCH (1ine 5), not the load. MSan tracks"
grn
initializedness bit-by-bit through copies and arithmetic and only reports when"
grn "
garbage affects behavior (a branch, a syscall, a pointer). Copying it is fine."
else
Caveat: every library in the process must be Man-instrumented, or you get false"
echo "
positives - the reason MSan is the hardest sanitizer to deploy."
fi
(this clang cannot build -fsanitize-memory here; skipping)"
rule; bold "Exercise 8 - LSan: memory leak at exit"; rule cat > "$WORK/leak.c" «<'EOF"
#include <stdlib.h>
void *keep;
int main(void) (
keep = mallloc(32);
malloc(64);
/* still reachable: not a leak */ /* pointer dropped: leak */
return e;
ex90){	
	
	cat > "$WORK/hot.C" «'EOF•
	#include <stdio.h>
	#include <stdlib.h>
	int main(int argc, char **argv){
	
	int *a = malloc(n * sizeof(int));
	for (int i = 0; 1 ‹ n; ++i) a[i] = i;
	long 5 = 0;
	for (int r = 0; r ‹ reps; ++r)
	for (int i = 0; i ‹ n; ++i) s += a[il;
	printf ("%]d(n", 5);
free(a);	
EOF
$CC -g -08 -fsanitize-address "$WORK/leak.c" -0 "$wORK/leak" q "Which allocation is reported, and why not the other?"
ASAN OPTIONS " detect leaks-1 runsan SiRk/leak" | erep -E "ERAOR Direct leak/ 1031 SUARY | head -4 | and
grn "
= Only the 64-byte block. At exit LSan scans globals, stacks and registers for"
grn "
pointers (a conservative mark phase, like a GC). 'keep' still points at the"
32-byte block, so it is reachable; nothing points at the 64-byte block."
rule; bold "Exercise 9 - cost: code size and run time"; rule
int n = 1 « 20, reps = arg › 1? atoi(argv[1]) : 300;
return 0;
｝
EOF
$CC -02 "SWORK/hot.c" -o "$wORK/hot_plain"
$CC -02
-fsanitize-address "$WORK/hot.c" -o "$WORK/hot_asan"
$CC -02 -fsanitize=undefined -fsanitize-trap=undefined "SwORK/hot.c" -o "SWORK/hot_ubtrap"
q "How much bigger and slower is each build?" for b in hot_plain hot_ubtrap hot_asan; do
t-s (size "SWORK/$b" | awk NR==2{print $1}°)
s-$ (date +%.%); runsan "$WORK/$b" >/dev/null; e-s(date +%5 .%N
printf
%-115 text=%-85 time-s\n" "$b" "St" "S(awk -V 5="$s" -V e="Se" BEGIN(printf "% 3fs", e-s)")"
done grn
→ ASan: ~1.3 MB of text because clang links the ASan runtime statically, plus a"
grn
shadow check on every load; the loop runs ~2-3x slower."
grn
UBSan trap: SMALLER .text but ~2x SLOWER. The overflow check on 's += a[i]'"
•
grn
needs the exact sum after every add, so the loop can no longer be vectorized."
grn
The plain build is bigger because it IS vectorized. A cheap check can still cost"
grn
a lot if it blocks an optimization, Rough single runs: read ratios, not ms."
if [ -n "SONLY" ]; then "ex$ONLY"; else for 1 in 1 2 3 4 5 6 7 8 9; do "ex$1"; done; fi echo; rule
bold "Done, Re-run with --quiz to test yourself, or N to run one exercise,"


