/ Planted bugs for the lock-held checker. Usage: /bank <ok| unguarded-write| unguarded-read wrong-lock| leaked #include <pthread.h> #include <stdio.h> #include <string.h> #include "lockcheck.h"
thread mutext gmu
- PTHREAD_MUTEX_INITIALIZER;
pthread mutex_t gother - PTHREAD_MUTEX_INITIALIZER;
struct Account ( int id;
not guarded: free to touch
long balance GUARDED_BY(g_mu): // must hold g_mu struct Account acct - (1, 100):
static void deposit(long n) {
pthread_mutex_lock(&g mu);
acct. balance += n;
pthread_mutex_unlock(&g_mu);
// OK: lock held
static void deposit_unguarded (long n)
acct. balance += n;
// BUG: read + write without g_mu
static long peek(void) ( return acct.balance;
// BUG: read without g_mu
}
static void deposit wrong_lock(long n) {
pthread_mutex_lock(&gother);
acct.balance += n;
// BUG: holds a lock, but not g_mu
pthread_mutex_unlock(&g_other);
static void *worker(void *arg) {
(void)arg;
for (int i = e; i < 1000; ++i) deposit(1);
return NULL;
int main(int arg, char **argv) {
const char *mode = argc > 1 ? argv[1] : "ok";
acct.id = 7;
// unguarded field: never reported
if (Istrcmp(mode, "ok")) {
for (int i = e; 1 ‹ 4; ++i) pthread_create(&t[i], NULL, worker, NULL): for (int 1 = 0; 1 ‹ 4; ++i) pthread_join(t[i], NULL): pthread_mutex_lock(&g_mu);
printf("balance - %ld\n", acct.balance);
pthread_mutex_unlock(&g_mu);
} else if (Istremp(mode,
"unguarded-write")) {
deposit_unguarded (5);
} else if (Istramp(mode,
"unguarded-read")) {
printf("peek = %1d\n", peek());
} else if (Istramp(mode,
"wrong-lock")) {
deposit_wrong_lock(5);
} else if (Istremp(mode,
"leaked-ref")) {
1/ Known false negative: the address escapes the annotation.
long *p = &acct.balance;
// annotation wraps the address here...
pthread_mutex_lock(&g_mu);
pthread_mutex_unlock(&g_mu);
*p = e;
...but this store goes
through p, unguarded and unchecked
printf("leaked-ref: wrote through escaped pointer\n");
return e;