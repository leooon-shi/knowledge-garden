// lockcheck_rt.c - runtime for the lock-held checker.
11
// State: per thread, the set of mutexes it currently holds (a small array in
// thread-local storage; real code rarely nests more than a few locks).
11
// Interceptors: our pthread_mutex_lock/unlock/trylock override libc's (the
// executable's definitions win symbol resolution) and forward to the real ones
// found with dlsym(RTLD_NEXT, •••). This also catches locks taken by
// uninstrumented code, e.g. a library calling pthread_mutex_lock for us.
// Check:_lockcheck_access(mu, is write, site) - inserted by the pass before
// every access to a GUARDED_BY field - reports if mu" is not in the set.
Policy knobs (environment):
1/
LOCKCHECK_HALT-1
abort on the first report (default: report and continue)
//
LOCKCHECK_READS= only check writes (default: check reads and writes)
I/ Must NoT be compiled with the lockcheck plugin. #define
GNU_SOURCE
#include <difcn.h>
#include <pthread.h>
#include <stdio. h>
#include <stdlib.h>
#include <string.h> #include <unistd.h>
#define MAX HELD 16
static
thread void *t_held[MAX_HELD]:
static
thread int t_nheld;
typedef int (*lock_fn) (pthread_mutex_t *);
static lock_fn real_lock, real_unlock, real_trylock;
static int g halt, g_check_reads - 1; static unsigned long reports;
_attribute_ ((constructor)) static void lockcheck_init(void) {
real_lock
- (lock_fn)dsym(RTLD_NEXT,
"pthread_mutex lock"):
real_unlock = (lock_fn)d1sym(RTLD_NEXT, "pthread mutex_unlock");
real_trylock - (lock_fn)d1sym(RTLD_NEXT,
"thread mutex_trylock");
const char *h = geten("LOCKCHECK_HALT"), *r - getenv("LOCKCHECK_READS");
g halt = h && *h == '1;
& check_reads - ! (r &8 *r == '0°):
static void held_add(void *mu) {
if (t_nheld < MAX_HELD) t_held[t_nheld++] = mu;
}
static void held remove(void *mu) {
for (int i = t_held - 1; 1 › 0; --1)
if (t_held[i] == mu) { t_held[i] = t_held[--t_nheld]; return; }
}
static int held(void *mu) {
for (int i = e; i < t nheld; ++i) if (t_held[i] =- mu) return 1;
return 0;
}
11 ---- interceptors
int pthread mutex_lock(pthread_mutex_t *m) {
if (breal_lock) lockcheck_init();
int rc = real_lock(m);
if (rc = 6) held_add (m);
return rs;
int pthread_mutex_trylock(pthread_mutex_t *m) {
if (Ireal_trylock) lockcheck_init();
int rc = real_trylock(m;
if (rc = B) held_add (m);
return rc;
int pthread_mutex_unlock(pthread_mutex_t *m) {
if (freal_unlock) lockcheck_init(); held_remove(m);
return real_unlock(m);
---- the check -----.
void
_lockcheck_access (void *mu, int is write, const char *site) (
if (lis write 8& Ig_check_reads) return; if (held(mu)) return;
_atomic_add_fetch&g_reports, 1,
ATOMIC_RELAXED);
fprintf (stderr,
"=-lockcheck=- %s of guarded field without its mutex (p) at %s\n",
is write ? "WRITE" : "READ", mu, site);
if (g_halt) _exit(66);
attribute_((destructor)) static void lockcheck_f1n1(vold) {
if (g_reports)
fprintf(stderr, "--lockcheck= SUMMARY: %lu unguarded access(es) \n", _reports);