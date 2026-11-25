#1/usr/bin/env bash
# run_tests.sh-build and test both sanitizers from interview/sanitizer_development.md.
#
-/run_tests.sh
KEEP=1 ./run_tests.sh
build + run every test, print PASS/FAIL per test keep the build directory for inspection
#
# Requires: clang/clang++, opt and FileCheck from the SAME LLVM version as clang
# (plugins are tied to one LLVM version), 11vm-config, pthreads.
set -u
HERE-"$ (cd "$(dirname "$0")"& pwd)"
B="$ (mktemp -d)"
[ "$ [KEEP: -0}" = 1 ] && echo "build dir: $B" || trap 'rm -rf "$B"* EXIT
LLVM_CONFIG-$(pick 11vm-config 11vm-config-18)
LLVM_BIN-S (SLLVMCONFIG --bindir)
OPT-$(pick "$LLVM_BIN/opt" opt)
FILECHECK-$(pick "$LLVM_BIN/FileCheck" FileCheck FileCheck-18)
[ -n "$FILECHECK" ] I| { echo "need Filecheck"; exit 1; }
# LLVM headers as -isystem: they don't build clean under -Wextra.
LLVM_CXFLAGS-"$($LLVM_CONFIG --cxxflags | sed 's/-I/-isystem /g')"
CLANG_V-$(clang --version | grep -oE version [0-9]+* | grep -oE °[0-9]+°)
LLVM_V-$($LLVM_CONFIG --version | cut -d. -f1)
[ "SCLANG_V" - "$LLVM_V" ] I| echo "WARNING: clang SCLANG_V vs 11vm-config $LLVM_V - plugin may not load"
pass=0; fail-0
ok()
printf
1033[32mPASS\033[0m %s\n• "$1"; pass-$((pass+1)); }
bad()
printf •
1033[31mFAIL\033[0m %s \n° "$1"; I -n "$(2:-)" ] && printf %s\n° "$2" | sed s/^/
/°; fail-$( (fail+
# expect ‹name›
‹expected-exit> ‹stderr-regex-or-empty> <cmd...>
expect (){
local name-$1 want=$2 re-$3; shift 3 local out; out-$("$©" 2>81); local rc-$?
if [ "$rc" I- "$want" ]; then bad "$name (exit $rc, want $want)" "Sout"; return; fi
-n "Sre" ] & ! printf %s° "$out" | grep -qE "Sre"; then bad "$name (output didn't match /$re/)" "Sout"; return; fi
"Sre" ] && printf %s "Sout" | grep -qE "==(masan| lockcheck) =="; then bad "$name (unexpected report) "Sout"; retu
echo "=- build =_"
clang++ -shared -fPIC $LLVM_CXXFLAGS -Wall -Wextra -Werror
"$HERE/mini_asan/MiniASanPass.cpp" -0 "$B/MiniASanPass.so" && ok "build MiniASanPass.5o" || bad "build MiniASanPass.so"
clang++ -shared -fPIC $LLVM_CXFLAGS -Wall -Wextra -Werror 1
"$HERE/lockcheck/LockCheckPass.cpp" -0 "$B/LockCheckPass.so" && ok "build LockCheckPass.so" || bad "build LockCheckPass.so"
clang -g
-01 -Wall -Werror -c
"$HERE/mini_asan/masan_rt.c"
-• "$B/masan_rt.o" && ok "build masan_rt.o" || bad "build masan
clang -g
-01 -Wall -Werror -c
"$HERE/lockcheck/lockcheck_rt.c" -0 "$B/lockcheck_rt.o" & ok "build lockcheck_rt.o" || bad "bu
echo "- MiniASan: pass output (FileCheck) ="
"SOPT" -load-pass-plugin=$B/MiniASanPass.5o" -passes-mini-asan -S "$HERE/mini_asan/test_pass.11" -0 "$B/masan.11" |
&& "$FILECHECK"
"$HERE/mini_asan/test_pass.11" ‹ "$B/masan.11" |
&& "$FILECHECK" --check-prefix-CTOR
"$HERE/mini_asan/test_pass.11" < "$B/masan.11" |
&& ok "instrumentation + site table" || bad "MiniASan FileCheck"
echo "== MiniASan: end to end, default (early) placement ==" for 0 in 00 01 02; do
&& clang "$B/p.o"
expect
"-$0 ok"
expect "-$0 overflow" 1 "heap-buffer-overflow.*|e bytes after a 40-byte" "$B/masan_$0" overflow expect
"-SO uaf"
"heap-use-after-free"
"$B/masan_$0" uaf
expect
"-$0 partial"
"WRITE of size 1 at ."test_prog.c:28"
"$B/masan_$0" partial
done
echo
= MiniASan: late placement misses bugs the optimizer removed (expected) --*
MINI_ASAN_LATE=1 clang -g -02 -fpass-plugin="SB/MiniASanPass.so" -C
"$HERE/mini_asan/test_prog.c" -0 "$B/p.o" \
&& clang "$B/p.o" "$B/masan_rt.o" -o "$B/masan_late"
expect "late -02 overflow is NOT reported" • ** "$B/masan_late" overflow echo "== LockCheck: pass output (FileCheck) =="
"SOPT" -load-pass-plugin="$B/LockCheckPass, so" -passes=lockcheck -S "$HERE/lockcheck/test_pass.11" | | "$FILECHECK" "SHERE/1ockcheck/test_pass.11" && ok "instrumentation" || bad "LockCheck FileCheck"
echo
LockCheck: unresolved mutex is a compile error =="
expect "GUARDED_BY (nonexistent_mu) rejected" 1 "mutex 'nonexistent_mu".*not a global"
clang -fpass-plugin="$B/LockCheckPass so" -I"SHERE/lockcheck"-c "SHERE/1ockcheck/bad_annotation.c" -o "$B/bad.o"
echo "= LockCheck: end to end ==" for 0 in 08 01 02; do clang -g -$0 -fpass-plugin="$B/LockCheckPass.so" -I"SHERE/lockcheck" - C
"SHERE/lockcheck/bank.c"
-o "$B/bank.o"
&& clang "$B/bank.o" "$B/lockcheck_rt.o" -0
"$B/bank_$0" -pthread -1d1 || ( bad "compile bank
-$0"; continue; }
expect
"-$0 ok (4 threads, locked)"
"$B/bank_$0" ok
expect
"-$O unguarded-write"
• "WRITE of guarded field .*bank.c:23"
expect
• "READ of guarded field. *bank.c:27"
"$B/bank_$0"
unguarded-write
"$B/bank_$0" unguarded-read
expect
-$o wrong-lock"
• "WRITE of guarded field. .*bank.c: 32"
"$B/bank_$0" wrong-lock
expect
"-$O leaked-ref (known miss)" e
"$B/bank_$0" leaked-ref
done
expect, "LOCKCHECK_HALT-1 aborts with 66" 66
"WRITE|READ" env LOCKCHECK_HALT-1"$B/bank_01" unguarded-write
expect "LOCKCHECK_READS= skips reads"
env LOCKCHECK_ READS- "$B/bank_01" unguarded-read
echo; echo "= $pass passed, $fail failed =-"
[ "$fail" = e ]
