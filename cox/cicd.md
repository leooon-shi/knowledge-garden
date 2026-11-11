
# Testing & CI/CD
How Cox's test suite is built, discovered, and run - from the Googletest assertions inside each binary up to a CI matrix - and the build-configuration and hygiene decisions that keep all of it reproducible.
## GoogleTest vs CTest vs CI (the layering)
These are complementary layers, not alternatives. Each sees a different scope.
**GoogleTest** is the framework *inside* each binary. It provides TEST(),
"EXPECT_EQ, fixtures, and the assertion machinery, and compiles into every executable. Its scope ends at the process boundary: sni_test" knows about its own cases and nothing else.
**CTest** is the runner that sits outside*, orchestrating those binaries. It knows nothing about C++ or assertions. To CTest a test is just "a command that exits e for pass, non-zero for fail" - it could be running a shell script and would not care. What Test adds in a CI context, none of which Googletest can do because GoogleTest cannot see beyond its own process:
-   **One entry point.** "ctest instead of a hand-written loop over
*   build/bin/*_test. CI invokes one command regardless of how many binaries
exist.
**Cross-binary parallelism.** ctest -j8 runs different executables concurrently. GoogleTest can only parallelize within one binary, and not by default.
- **Per-test timeouts.** A hung test gets killed and reported. This matters here because several tests bind real sockets and spawn responders, which can hang rather than fail.
*  *Machine-readable results** that CI systems parse into pass/fail annotations, so the dashboard shows which test broke instead of a raw log.
*  *Selection and retry.** ctest -R Snitest to filter, --rerun-failed to retry only failures,
--repeat until-fail:10 to hunt flakiness - valuable for
the socket-based tests.
- **Non-GoogleTest things in the same run.**A later shell-based integration test or linter check runs alongside the unit tests.
**CI** (GitHub Actions) is the outermost layer: it provisions a clean machine, installs dependencies, and invokes 'ctest across the build matrix.
The layering, concretely for Cox:
‘’’
CI (Actions)
->
ctest
orchestration
runner
－>
36 binaries processes
-＞

gest cases inside each assertions

‘’’

Test code does not change at all. Test is purely additive registration in
•tests/cMakeLists.txt. gtest_discover_tests) is the adapter that lets ctest see *into* each binary, so its scheduling, filtering, and timeouts operate on individual cases rather than whole processes. Without it crest still works; it just treats each binary as one opaque unit.
## CTest registration
The root MakeLists. txt enables Test, and tests/cMakeLists.txt replaces hand-written "add_executable / target_link_libraries pairs one per test binary) with a single helper:
**cmake
include(GoogleTest)
#Timeouts are safety nets against a hung test (several tests bind real sockets
# or spawn helper processes), not performance targets.
set(COX_TEST_TIMEOUT 120)
set(cox_TEST_TIMEOUT_SLOW 300)
# cox_add_test <name> LIBS <Iibs.. [OPAQUE] [SLOW])
function(cox_add_test name)
make_parse_argumentsARG "OPAQUE;SLOW" •" "LIBS" ${ARGN})
add_executable(${name} ${name}. cc)
target_link_libraries(${name) PRIVATE $(ARG_LIBS) gtest_main gmock pthread)
if (ARG_SLOW)
set timeout ${COx_TEST_TIMEOUT_SLOW})
else()
set (timeout ${COX_TEST_TIMEOUT))
endif()
I
if (ARG_OPAQUE)
add test (NAME ${name} COMMAND ${name})
set_tests_properties(${name} PROPERTIES TIMEOUT ${timeout})
else()
gtest_discover_tests(${name}
PROPERTIES TIMEOUT ${timeout}
DISCOVERY_TIMEOUT 30
endif()
endfunction()
...
### Two registration modes
-   ** cox_add_test(name LIBS ...) ** - uses gtest discover_tests) , which registers each •TEST() case individually. CTest can then schedule, filter, and time out at case granularity. This is the default and expands the 36 test binaries into 177 CTest entries.
-   ** cox_add_test(name LIBS ... OPAQUE) ** - registers one CTest entry for the whole binary via 'add_test(), with no build-time discovery. Used where build-time discovery is undesirable: tests that shell out to the 'openssl CLI to build certs/PKI or start a local responder, or that bind fixed ports. Those must not be executed during the build. The TLS/OCSP/HTTPS tests and
*   doc_snippets_test are registered this way.
*SLOW selects the longer timeout for tests whose runtime is dominated by real
I/0 or sink timing.
### Isolating vendored code from the warning policy
googletest/ is third-party and must not inherit cox's warning flags: " -werror on code you do not control means a compiler or dependency upgrade can break the build for reasons unrelated to your own sources. Directory "COMPILE_OPTIONS" are captured by a subdirectory when it is added, so clear them *before*
*  add_subdirectory) and restore afterwards:
*  **cmake
enable_testing()
get_directory_property(COX_DIR_OPTIONS COMPILE OPTIONS) set_directory_properties(PROPERTIES COMPILE_OPTIONS "")
add_subdirectory (googletest)
set_directory properties(PROPERTIES COMPILE_OPTIONS "$(COX_DIR_OPTIONS}")
add_subdirectory(tests)
Result: googletest builds without "-Werror, while Cox's own code and tests keep the full policy. (Setting the property on the subdirectory *after*
"add_subdirectory does not work - the options are already captured.)
##* Payoff
ctest -j8 runs the whole suite in roughly 8 seconds, versus minutes for the sequential bash loop it replaces - and gives per-case timeouts, filtering, and machine-readable results for free.
## MakePresets as the single source of build config
The compiler/sanitizer matrix belongs in "CakePresets. json, not in CI YAML.
The CI configuration then becomes a thin loop over preset names: the same commands run locally and in CI, and the choice of CI system stays reversible.
Each preset gets its own binary directory (build/${presetName}) so configurations never clobber each other and stay incremental. This is a deliberate improvement over "build.sh', which does rm -rf in the build dir and so forces a full rebuild on every invocation. Keep "build.sh' as a wrapper that delegates to presets rather than replacing it.
***json
｛
"version": 3,
"makeMinimumRequired": { "major": 3, "minor": 21, "patch": 0 },
"configurePresets": [
"name": "base",
"hidden": true,
"binaryDir": "${sourceDir)/build/$(presetName)",
"cacheVariables": {
"BUILD TESTS": "ON",

‘’’

Two schema notes that bite in practice:
## GoogleTest vs CTest vs CI
-comment is
**not** a valid preset field; make's preset schema is strict.
Use "description" (or "displayName") for human-readable notes.
- Sanitizer test presets run fewer parallel jobs (jobs: 4 instead of 8).
Instrumented builds are slower and serialize better, so fewer jobs avoids timeout flakiness.
Typical use:
•** bash cmake --preset gcc-release
cmake --build --preset gcc-release -j8
ctest --preset goc-release
弁##
GCC is not redundant with Clang
The two compilers report different warnings, so building both is worth the time.
A real format-string bug in "timestamp.cc" was caught only by GCC. GCC Release additionally enables -D_FORTIFY_SOURCE, which makes ignored write() / read () return values a -Werror=unused-result failure - surfacing unchecked I/O in test harnesses that Clang passed silently.
## Sanitizer & coverage presets
Sanitizers are the highest-value testing tool for this codebase: connection lifetimes are shared_ptr-managed, work is marshalled across threads with
'runInLoop®
the async log sink hands buffers to a writer thread, and raw
•sockaddr
casts are common. A passing test proves behavior, **not** the absence
of a data race or a use-after-free - only these tools do.
I
The build exposes a single "COX_SANITIZE option
(OFF | address | thread | undefined | leak'). It is single-valued by design:
ASan and TSan runtimes conflict and cannot be combined in one build.
• cmake
# COX_SANITIZE=address|thread|undefined |leak/OFF
set(COX_SANITIZE "OFF" CACHE STRING
"Enable a sanitizer: OFF, address, thread, undefined, leak")
set_propertyCACHE COX_SANITIZE PROPERTY STRINGS
OFF address thread undefined leak)
##GoogleTest vs Test
if (NOT COX_SANITIZE STREQUAL "OFF")
set(COX_SAN_FLAGS
-   fsanitize=${COx_SANITIZE)
-  fno-omit-frame-pointer
-  fno-optimize-sibling-calls
-  g
# usable stack traces
# tail calls do not eat frames
if (COX_SANITIZE STREQUAL "address")
# Catch use-after-scope too; cheap and finds real bugs.
list(APPEND COX_SAN_FLAGS - fsanitize-address-use-after-scope)
elseif (COX_SANITIZE STREQUAL "undefined")
# Make UBSan failures fatal; by default it only warns, easy to miss in CI.
list(APPEND COX_SAN_FLAGS -fno-sanitize-recover=all)
endif()
add_compile_options${COX_SAN_FLAGS))
add_link_options-fsanitize-${COX_SANITIZE})
message(STATUS "sanitizer enabled: ${COX_SANITIZE)")
endif()
Both ASan and TSan build clean and immediately found real problems, which is the point of having them:
-   **TSan** reported data races across the suite. Many are the classic fd-reuse pattern - ::close() in "~EventLoop" racing another thread that touches a recycled fd number - largely in test harnesses that destroy loops, from worker threads. Separating genuine library races from harness artifacts is its own triage task (see "troubleshooting/tsan-data-raceq.md').
-  **ASan/Leaksanitizer** reported no memory errors
(no use-after-free, no
overflow) but genuine leaks. For example, "HttpclientTest.GetAgainstCoxserver leaks a TcpConnection" (and its buffers) allocated in
Tcpclient:: newconnection - a real gap in the client teardown path. The HttpClient / ConnectionPool / Dnsclient leaks likely share this one root cause.
Because the sanitizer builds currently report known findings, treat the 'asan’
and tsan presets as informational jobs until the findings are triaged and a suppressions file covers anything genuinely benign; then wire them into CI as blocking.
**Coverage** is the gap that remains: nothing measures which branches the 177 tests actually exercise, so claims about suite thoroughness are unverified.
•--coverage plus Icov is cheap and replaces an assertion of thoroughness with a number.
# The CI workflow
Github Actions is the fit here. The build matrix is the four main configurations (' clang-debug, "clang-release, gcc-debug, gcc-release") plus the sanitizer presets, and because the matrix lives in "CMakePresets.json" the workflow is a thin loop: configure, build, and 'ctest" each preset name. A clean runner installs OpenSSL / c-ares per run.
GitHub does not host Jenkins - Jenkins is self-hosted software that Github can only trigger via webhook or polling. Jenkins earns its keep only for special hardware, internal networks, or licensed tools, none of which apply here, so a hosted Actions matrix is the simpler choice. Because the workflow contains no build knowledge, a "Jenkinsfile could invoke the same three commands unchanged.
### workflow shape
Four jobs, each a thin driver over preset names:
-   ** build-and-test*** - the matrix "[clang-debug, clang-release, gcc-debug, gcc-release], blocking. "fail-fast: false" so one failing configuration does not cancel the others- seeing *which* configurations break is the whole point of the matrix. on failure it uploads build/‹preset>/Testing/ as an artifact so the crest logs are available without re-running.
-   ***examples*** - compiles the examples/benchmarks (single job, no matrix).Examples are excluded from the default build, so they rot silently; compiling them in CI keeps them honest without running the benchmarks, whose numbers are meaningless on a shared runner.
**"sanitizers ** - matrix "[asan, tsan, ubsan], "continue-on-error: true (non-blocking) while the ASan/LSan and TSan findings are untriaged. Running them keeps the findings visible and stops *new* ones hiding; flip
"continue-on-error" off once the backlog is clean so regressions then fail the
build. Sanitizer logs are uploaded if: always) .
- ***format*** - a clang-format --dry-run --Werror check over Cox's own sources, also continue-on-error: true (see below).
Two environment details the workflow must get right:
- "actions/checkout uses "submodules: recursive because googletest is a git submodule (confirmed in
*  gitmodules"), not vendored source.
-   The dependency install adds "libc-ares-dev as well as "libssl-dev, so CI compiles in the async DNS backend (COX_HAVE_CARES") and exercises
*CaresResolver rather than only the ThreadPool fallback.
### build.sh as a preset wrapper
"build.sh is kept as a convenience wrapper that maps flags onto a preset name and delegates, rather than duplicating build knowledge. The mapping:
-   *--asan / --tsan* / '--ubsan* → the matching sanitizer preset (Debug + clang).
-   "--build-examples* → the 'examples preset.
-   otherwise '--gcc / --clang x "--debug /--release* → '${compiler}-${type}.
-   •--test* appends a ctest --preset .. run; "--clean removes only *this* preset's "build/<preset›/ directory.
This is a deliberate improvement over the old build. sh
, which did rm -rf in a
single shared build dir on every invocation and so forced a full rebuild each time.
All four main presets build and pass:
clang-debug					100% tests passed, o tests failed out of 177				
clang-release					100% tests passed, e tests failed out of 175				
gcc-debug					100% tests passed, o tests failed out of 177				
gcc-release					100% tests passed, e tests failed out of 175				
The 177-VS-175 difference between Debug and Release is intentional, not a discovery bug: two "EventLoopTest" cases are guarded by "#if defined(DEBUG) and

### Apply the warning policy, and enforce it
The project defined a careful warning list (*-Wall -Wextra -werror
-wold-style-cast -Wshadow -Woverloaded-virtual -Wpointer-arith -write-strings*) but the line that applied it was commented out, so the only warning flags actually reaching the compiler were "-wno-unknown-warning-option and a misspelled "-wpendantic. The typo survived precisely because
*-Wno-unknown-warning-option suppressed the complaint - so pedantic warnings were on under GCC (which spelled it correctly) and silently off under clang, the compiler "build.sh" actually uses.
The fix applies the list via "add_compile_options", puts "-werror behind a
•COX
WERROR option (default ON) so the policy cannot silently rot again, and corrects the typo. The result is zero warnings under both clang and GCC, in both Debug and Release, with tests and examples built.
Fixing the newly-enforced warnings surfaced real issues beyond dead code (unused members, redundant captures): a format-string bug in "timestamp.cc" (%061d' applied to a "uint64_t* , plus a fixed-size buffer GCC could not prove
large enough) that only GCC had been reporting, and two silently-dropped
*log() calls on DNS-failure and malformed-request (400) paths that were wired up rather than deleted.
•-Wold-style-cast" also fired on casts *inside OpensSL's own macros*, which expand C-style casts at the call site. Those were suppressed narrowly with a
*  #pragma around the three affected call sites rather than by weakening the flag, so the policy still applies to cox's own code:
*  **cpp
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-wold-style-cast"
// ..• OpenssL macro that expands a C-style cast I* #pragma GCC diagnostic pop
Flags that are *not* warning settings - -march=native (non-portable binaries),
*   -rdynamic*
-  stdlib=libc++ (ABI choice) - were deliberately left out of the
warning policy; they belong in a toolchain file or per-target configuration.
### Check I/O return values in tests

	
	size_t sent = 0;
	while (sent ‹ len) {
	
	if (n ‹= 0) return false;
	sent += static_cast<size_t>(n);
	

### Check I/O return values in tests
Under GCC Release's "-D_FORTIFY_SOURCE, ignoring the result of write() / read() / system() is a hard error. A dropped result is genuinely wrong even in a test: a failed setup write makes the surrounding assertions meaningless rather than failing loudly. A shared tests/test util.h makes the intent explicit at each call site:
•** cpp
namespace test _util {
// Write the whole buffer. Returns true only if every byte was accepted.
inline bool sendall(int fd, const void* data, size_t len) {
const char* p = static_cast<const char*> (data);
size_t n = ::write(fd, p + sent, len - sent);
return true;
inline bool sendall(int fd, const std: string&s) {
return sendall fd, s.data(), s.size());
// Read whatever is currently available. Used to drain, not to assert content.
inline ssize_t drainSome(int fd, char* buf, size_t len) {
return ::read(fd, buf, len);
// Run a shell command, returning its exit status f°== success.
inline int rund(const std:: string& cmd) {
return std:: systemcmd.c_str());
// namespace test_util
The convention that falls out:

-   Setup writes that *must* succeed are asserted: "ASSERT_TRUE(sendA11(...)) .
-   Drain/teardown reads tolerate having nothing to read and use drainSome() ; where a value is deliberately discarded, say so (° (void)n;*) rather than letting it look accidental."openssl'-CLI invocations go through runcd(); the real check is the subsequent probe that the generated cert/PKI actually appeared, so the status is explicitly (void) -discarded with a comment pointing at that probe.
### Repo housekeeping
**•gitignore'.** A root，
•gitignore keeps build/ from showing as untracked.
Because presets put each configuration in its own subdirectory (build/clang-debug, "build/tsan, -), ignore the whole "build/" tree rather than named dirs. Also cover sanitizer/coverage artifacts (* gda, * gcno"
•gCOV
, *•profraw
* profdata), editor droppings, and
" CMakeUserPresets json (the tracked file is "CakePresets. json"; the "User" variant is per-developer by convention).
A stray ~6 MB "cox_ev_leftovers. json at the repo root turned out to be a saved AI-assistant conversation transcript, not project data, and was never tracked by git. It was added to gitignore rather than deleted - not the author's file to remove.
**•clang-format'.** A root •clang-format makes the README's Google-C++-Style claim mechanically checkable (clang-format --dry-run --Werror ) instead of merely asserted. "BasedonStyle: Google with a few deliberate settings:
- **One intentional deviation: "Indentwidth: 4"**, not Google's 2. Measured against the existing code ('net/, http/, tls/, "logging/'), which uses
4-space indent throughout; imposing 2 would reformat essentially every file and bury real history in noise. Accessmodifieroffset: -4" and
• ColumnLimit: 80" likewise match the code as written.
IncludeBlocks: Preserve / SortIncludes: CaseSensitive. Cox headers are found via 'target_include_directories, so a quoted " "buffer.h" is a *project* header, not a sibling-relative one; blindly re-sorting it into the system-header block would be wrong, so include *blocks* are preserved and only sorted within a block.
-   "PointerAlignment: Left, DerivepointerAlignment: false to pin int* p' /
*   const T& ras used throughout.

**Caveat:** "clang-format" was not available in the authoring environment, so conformance of the existing code to this " clang-format" is *unverified* - the config was written to match the code *as measured*, not proven against it. The CI format job therefore starts "continue-on-error: true: its first real run surfaces the actual diff, after which the flag can be dropped or the tree reformatted once so style regressions fail the build.
I