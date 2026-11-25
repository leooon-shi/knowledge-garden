bash
# lto_pgo_practice.sh
# A hands-on lab to SEE the effects of LTO and PGO (companion to
# interview/Ito_and_pgo.md). It builds the SAME program several ways and
# compares binary size, cross-module inlining, dead-code elimination, and
# (optionally) run time, using the binary-analysis tools from the previous
# topic (nm / readelf / size).
# Builds compared:
1.    baseline
2.    LTO
3.    PGO
4.    PGO+LTO
(-02, separate TUs, no cross-TU opt)
(-02 -flto: cross-module inlining + whole-program DCE)
(-02 instrument -> train -> -fprofile-use)
(both together - the max-performance recipe)
Usage:
•/ito_pgo_practice.sh
./1to_pgo_practice.sh --quiz
# run everything,
show comparisons
# hide the explanatory answers
# Requires: a C/C++ compiler (clang++ preferred for clean PGO; falls back to g++),
nm, readelf, size, awk. Pure CPU workload; no external input needed.
set -u
WORK-"$ (mktemp -d) "
trap
"rm -rf "$WORK"• EXIT
QUIZ-e
for a in "se"; do [ "Sa" = "--quiz" ] 88 QUIZ-1; done
bold) printf 1033[1m%s \033[0m\n° "g*"; }
grn()
[ SQUIZ" -eq e ] & printf •\033[32m%s\033[em\n° "**"; }
rule(){ printf %s\n' "-=
have(){ command -v "$1" >/dev/null 2>81; }
# Pick a compiler.
clang++ gives the cleanest GO (11vm-profdata); else g++.
CXX=*"; PGO_KIND-**
if have clang++ & have 11vm-profdata; then CXX-"clang++"; PGO_KIND-"clang"; elif have g+; then CXX-"g++"; PGO_KIND-"gcc"; else echo "need clang++ or g++"; exit 1; fi bold "Using compiler: $CXX (PGO flavor: $PGO_KIND)™ echo "Work dir: $WORK" echo
"; }
* Program: split across TWO translation units so LTO has a cross-TU boundary
# to optimize across. 'hot) is called in a tight loop; 'never_called() is
# dead (whole-program DCE should remove it under LTO).
cold) is rare.
cat › "$woRK/math.h" «'EOF•
#pragma once int
hot (int x);
// defined in math.cpp - LTO can inline across TU
int
cold(int x);
int
EOF
never_called(int x); // dead at whole-program scope
cat >
"$wORK/math.cpp" «'EOF•
#include
"math.h"
// Small hot function in a DIFFERENT TU than its caller: only LTO can inline it.
int hot(int x) {return (x * 1103515245 + 12345) & 0x7fffffff; } int cold(int x) {// rarely taken path
long 5 = 0; for (int i - 0; 1 < 1600; +i) s+= (long)x * 1; return (int)s;
int never_called(int x) {// unreachable from main -> whole-program DCE target long 5 - 1; for (int 1 - 1; 1 < 100000; ++i) 5 - (s * x + i) & Oxffffff; return (int)s;
}
EOF
cat > "$wORK/main.cpp" «<'EOF
#include "math.h"
#include ‹cstdio> #include ‹cstdlib>
int main(int argc, char** argv) {
int iters - (arg › 1)? atoi(argv[1]) : 60000000;
int seed - 1, acc = 0;
for (int 1 - 0; i < iters; ++i) {
seed = hot(seed);
1/ HOT: tight loop, cross-TU call
if ((seed & exffff) == 6)
1/ taken ~1/65536 -> cold path
acc += cold(seed);
std:: printf(*%d %d\n", seed, acc);
return 0;
}
EOF
xx){ "$CXX" -std=c++17
"$0"; }
rule; bold "BUILD 1 - baseline (-02, separate TUs, no cross-TU optimization)"; rule CXx -02 -C
"SwORK/math.cpp* -0
"$wORK/math.o"
CXX -02
- SWORK/main.pp-o"SWORK/main.o"
cxx -02
"$wORK/main.o" "$WORK/math.o" -o "SWORK/app_base"
echo "built app base"
rule; bold "BUILD 2 - LTO (-02 -flto: cross-module inlining + whole-program DCE)"; rule cxx -02
-flto -c"swORk/math.cpp" -o "SWORK/math_1to.o"
xx -02 -flto -c "SWORK/main.cpp" -0
"SWORK/main_1to.o™



wo_pgo_practice.sh
echo "built app_lto"
Exx-02 -flto "Swork/main_Ito.o* "SwORK/math_1to.o* -o "$WORK/app_Ito"
Smultithre: L
rule; bold "BUILD 3 - PGO (instrument -> train -> optimize)": rule
if [ "$PGO_KIND" = "clang" ]; then
echo " training (running instrumented binary)..."
cxx -02 -fprofile-generate="$wORK/pgo" "$WORK/main.cpp" "$WORK/math.cpp" -o "SWORK/app_instr
*$WORK/app_instr™ 20000000>/dev/nu11
11vm-profdata merge -o "$WORK/app.profdata" "$WORK/pgo"/*.profraw
echo "
xx -02 -fprofile-use="$wORK/app.profdata" "$WORK/main.cpp" "$WORK/math.cpp" -o "$WORK/app_pgo"
built app_pgo"
rule; bold "BUILD 4 - PGO + LTO (the max-performance recipe)"; rule
echo " built app_pgolto"
xx-02 -fito-fprofile-use-"$wORk/app.profdata "SWORK/main.cpp "$WORK/math.cpp" -o "$WORK/app_pgolto"
else
fi echo
# GCC flavor: -fprofile-generate writes • gcda next to objects
cxx -02 -fprofile-generate "$WORK/main.cpp" "$WORK/math.cpp" -o "$WORK/app_instr echo "
training (running instrumented binary)..."
(cd "$WORK" 8& "$wORK/app_instr20000000 >/dev/null)
x-02 -fprofile-use -fprofile-correction "$WORK/main.cpp" "$N
Il { echo "
rule; bold "BUILD 4 - PGO + LTO"; rule cxx-02-flto -fprofile-use -fprofile-correction "$wORk/main.cpp" "SWORk/math.cpp" -0 "$WORK/apppgolto" 2>/dev/nul1 |
Il cp "$woRK/app_lto" "$WORK/app_pgolto
# ANALYSIS 1
rogram dead-code elimination (LTO removes never_called)
rule; bold ANALYSIS 1 - did LTO eliminate the dead function 'never_called"?"; rule
check_dead(){ # $1 = binary
}
if nm -C "$1" 2›/dev/null | grep -q 'never called'; then echo "present"; else echo "REMOVED"; fi
printf " printf "
%-145 never_called(): %s\n" "baseline" "$(check_dead "$WORK/app_base""
grn
"$(check_dead "$wORk/app_1to")"
grn "
= Baseline keeps never_called() (per-TU compilation can't prove it's unreachable)."
LTO sees the WHOLE program, proves it's never called, and strips it (whole-program DCE)."
# ANALYSIS 2 - cross-module inlining (does "hot' survive as a call target?)
rule; bold ANALYSIS 2 - cross-module inlining of hot' (defined in another TU)"; rule hot_sym){ nm -C "$1" 2›/dev/null | grep -c
• T .*hot(' ; }
printf
printf "
%-14s 'hot defined-symbol count: %s\n" "baseline" "$(hot_sym "$WORK/app_base")"
%-14s "hot' defined-symbol count: %s\n" "LTO"
grn
$(hot_sym "$wORK/app_Ito")"
= Under LTO, hot() can be inlined into main across the TU boundary, so the"
grn
out-of-line copy is often removed/internalized - baseline must keep it as a real call."
# ANALYSIS 3 - binary size (LTO DCE shrinks; PGO layout may change size)
rule; bold "ANALYSIS 3 - binary size & section footprint (size)"; rule for b in app_base app_lto app_pgo app_pgolto; do
-f"$WORK/$b" ] && { printf "
%-12s " "$b"; size "$WORK/$b" | awk 'NR--2{printf "text-%s data-%s bss-%s total-s \n",s1
done grn
grn
= LTo typically shrinks .text (dead code removed, inlined duplicates merged)."
PGO may slightly grow/shift text due to hot/cold splitting + block reordering."
# ANALYSIS 4- hot/cold section splitting (PGO) via readelf sections
rule;
bold "ANALYSIS 4 - PGO hot/cold text splitting (look for .text.hot/.text.unlikely)"; rule
for b in app_base app_pgo app_pgolto; do [ -f"$WORK/$b" ] || continue
secs-$(readelf -5 "SWORK/$b" 2›/dev/nu11 | grep -oE 1.text\.(hot |unlikely|cold|startup)[^ ]* | sort -u | tr "In* • ")
printf•
%-125 hot/cold text sections: %s\n" "$b" "${secs: -<none>}"
done grn
grn
grn
→ PGO/compiler may split functions into .text.hot and .text.unlikely so the" linker places hot code together (better I-cache & branch prediction)."
(Exact section names vary by compiler/version; absence doesn't mean no reordering.)*
# ANALYSIS 5 - run
time comparison (optional, rough)
rule;
bold ANALYSIS 5 - rough run-time comparison (same workload)"; rule
timeit(){ # $1 binary
"$1" ] 11
{echo "n/a"; return; }
local start end
start-$(date +%.%N); "$1" 60000000 >/dev/null; end-$(date +%.%N) awk -v 5="$start" -v e="$end" 'BEGIN(printf "%.3f s", e-s}'
for b in app_base app_lto app_pgo app_pgolto; do [-f"$WORK/$b" ] && printf"
%-125 %s\n" "$b" "$(timelt "SWORK/$b")"
done grn
grn
grn
= Expect app_lto ‹ app_base (cross-TU inlining of hot)): PGO adds layout wins;"
PO+LTO usually best. Numbers are rough (single run, shared machine) - the POINT is the direction and the mechanism, not the exact milliseconds."
echo; rule bold
"Done.
Re-run with --quiz to hide the explanations and reason about the output yourself." bold "key tools used: nm (symbols/DCE/inlining), size (footprint), readelf -s (hot/cold layout)."

