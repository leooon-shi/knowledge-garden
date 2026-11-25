#l/usr/bin/envbash
wue binary_analysis_practice.sh
# binary_analysis_practice.sh
# A self-checking hands-on lab for the binary-analysis toolkit
#(companion to interview/binary_analysis_tools_study_guide.md).
MiniAsanpass.cpP
= test_pass.Il /mini_ase
It builds small sample binaries/libraries and runs guided exercises with questions + revealed answers, so you learn objdump/readelf/1dd/nm by DOING.
Usage:
-/binary_analysis_practice.sh ./binary_analysis_practice.sh <N>
./binary_analysis_practice.sh --quiz
# run all exercises
# run only exercise N (1..8)
# hide answers (print questions only)
set -u
Requires: g++, file, 1dd, readelf, objdump, nm, size, strings. (strace optional.)
WORK="$ (mktemp -d)"
trap rm -rf "$WORK"• EXIT
QUIZ-0
ONLY-"• for a in case
--quiz)
QUIZ-1
[0-9]*) ONLY="$a
esac
done
bold() { printf "\033[1m%s \033[0m\n° "$*"; }
rule()
{printf
%s\n' "--.........
have) { command -v "$1">/dev/null 2>81; }
run() {
printf
# run <cmd...>: echo the command, then its output
$ %s\n'
eval "$*"
2>81 | sed 's/^/
#q "question"
printf In
bold "Q: $*»
a "answer": hidden unde
+ I "SQUIZ" -eq e 1: then printf
}
•: printf •1033[32mA: %s\033[em\n° "g*"; fi
section) [rule; bold "s*"; rule; )
#
# Build sample artifacts: a shared library + a dynamic app + a static app.
build_samples) (
cat › "$wORK/libgreet.cpp" «'EOF•
#include ‹string›
#include ‹cstdio>
// Exported function (becomes a T' symbol in the •so) std:: string greet(const std:: string& who) {return "hello, " + who; } extern "C" void greet_c(const char* who) { std: printf("hello, %s\n", who); } EOF
cat › "$WORK/app.cpp" «'EOF•
#include ‹string›
#include ‹cstdio>
sto::string greet(const std:: stringå who);
int main() {
auto s - greet ("binary-analysis");
std: printf(%s\n", s.c_str()):
return (int)s.size() & e;
// provided by libgreet
// imported symbol (U)
// libc import
11 0
EOF
8t*-Std=C++17 -02 -fPIC -shared "SWORK/11bgreet,cpp" -o "$WORK/11bgreet.so" |1 return 1
g++ -std=C++17
g++
-02 "$WORK/app.cpp" -L"$WORK"
-  greet -Wl, -rpath, "SWORK" - "SwORK/app" || return 2
-  std=c++17 -02 -static
SwORk/app.cp"$wORk/11bgreet.cpp-0
"SWORK/app_static" 2>/dev/nu11 || \
g+* -std=c++17 -02 -static-libstdc++ -static-libgcc "$WORK/app,cpp" "SWORK/11bgreet.cpp" -0 "$wORK/app_static" 2>/dev/nul
x1() (
section
"Exercise 1- triage with 'file'"
q "What does 'file' tell you about app vs, app_static?" run "file $wORK/app"
I -f "SWORK/app_static" ] 8& run
"file $WORK/app_static"
a "app - dynamically linked PIE ELF64; app_static = statically 11nked (no interpreter, no.so deps). "file is always your
section
"Exercise 2 - direct vs. transitive dependencies"
a "Idd shows the TRANSITIVE closure; readelf -d shows only DIRECT (DT_NEEDED). Compare." run "Idd $woRk/app"
run "readelf -d $WORK/app | grep NEEDED"
a "ldd lists libgreet.so, libstdc++, libc, libm(+loader) - the full closure. readelf-d NEEDED lists only what app DIRECTLY a "SAFETY: 1dd runs the loader -> never 1dd an untrusted file; use readelf -d / objdump -p (parse-only)."
x3() {
section "Exercise 3 - the PATH that makes libgreet resolve" q "app was linked with -wl,-rpath. Find the embedded search path. " run "readelf -d SWORK/app| grep -E "RPATH|RUNPATH'"

q "Does Idd resolve libgreet.so because of it?" run "ldd $WoRk/app | grep greet"
a "RUNPATH points at the build dir, so the loader finds 1ibgreet.so without LD_LIBRARY_PATH. Remove the rpath and Idd woul
ex4() {
section "Exercise 4 - imported (U) v5. exported (T) symbols"
9 "Which symbols does app IMPORT (undefined, U)?"
run nm -D -C $WORK/app | grep ' U " I grep -i greet" q "Does libgreet. so EXPORT (T) that symbol?"
run "nm -D -C $WORK/11bgreet.so | grep -1 'greet" | grep ' T '*
a "app has 'U greet(std: istring const&) (needs it); libgreet.so has the matching 'T greet(...)" (provides it). Matching U
section "Exercise 5 - symbol versions & the glibc/libstdc++ floor"
9 "What versioned 1ibc/libstdc++ symbols does app require? (the ABI floor)" run "objdump -T $WORK/app | grep -1 -E 'GLIBC' | head -6" q "Highest GLIBC version required (your minimum libstdc++):" run objdump -T $WORk/app | grep -oE "GLIBCXX_[0-9.]+° | sort -v | tail -1"
a "The highest versioned symbol is your portability floor: a target with an older 1ibstc++/glibc will fail with *version
section "Exercise 6 - read the generated code (objdump -d)" q "Disassemble main; identify the prologue and a RIP-relative load (PIE)." run objdump -d -C $woRK/app | grep -A10 '‹main>:*"
a endbr64 (CET), push of callee-saved regs (prologue), and lea...%rip),...' RIP-relative addressing because it's a PIE.
ex70 {
section "Exercise 7 - static vs. dynamic footprint"
I -f "$wORK/app_static" ] || {echo " (static build unavailable in this environment; skipping)"; return; 3 q "Compare dependencies and size of dynamic vs. static." run "Idd swork/app_static II true" run "size $woRK/app; size $work/app_static"
a "Static: 'not a dynamic executable', no.so deps, but much larger text (libs baked in). Trade-off: portability/self-cont
section "Exercise 8 - static view vs. actual runtime loads"
9 "Idd PREDICTS loads from the file; strace shows what ACTUALLY gets opened." run "Idd SwORk/app | grep "Il.so' | head" if have strace; then moto co gethe pare to a gip open that ad can't predict). static via 01%
a (strace not installed) The point: 1dd/readelf are a STATIC prediction; strace -e openat shows the REAL runtime.so loa fi
main()
bold
"Binary Analysis Practice Lab"
echo
"Building sample artifacts (shared lib + dynamic app + static app)..."
if ! build_ samples; then echo "build failed (need g++)"; exit 1; fi echo
"Artifacts in: $WORK"
for t in file 1dd readelf objdump nm size strings; do have done
"St" Il echo " WARNING: 'St' not found - some exercises will be limited. "
if [ -n "SONLY" 1; then "ex$ONLY"; else for 1 in 1 2 3 4 5 6 7 8; do "ex$1"; done; f1 echo; rule bold
"Done. Re-run with --quiz to test yourself (answers hidden), or N to run one exercise."
main

