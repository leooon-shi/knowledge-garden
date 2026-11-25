Il masan_rt. - runtime for the mini address sanitizer.
// Companion to MiniASanPass.cpp and interview/sanitizer_development.md
//
// Responsibilities (the same split real ASan uses):
1.    Shadow memory: 1 shadow byte per 8 application bytes, reserved at startup.
2.    Allocator: malloc/free replacements that add redzones and quarantine freed
！！
blocks, and poison/unpoison shadow accordingly.
3. Check entry point called by instrumented code:
masan_check.
4. Reporting: describe the bad access with its source location, then abort.
// This file must NOT be built with -fpass-plugin (it would instrument itself).
// Simplifications vs real ASan: only heap memory is tracked (stack/globals are
// unpoisoned, so never reported); the allocator is a bump allocator on top of
// mmap; quarantine never recycles memory.
#define
_GNU_SOURCE
#include <stdarg.h> #include <stddef.h>
#include < stdint.h> #include <stdio.h>
#include <stdlib.h>
#include ‹string-h> #include <sys/mman.h>
#include <unistd.h>
11 ---- shadow memory
// Shadow byte values for an 8-byte granule:
all 8 bytes addressable
//
1..7
only the first k bytes addressable (allocation ends mid-granule)
exfa
heap redzone
11
exfd
freed (in quarantine)
#define SHADOW_SCALE
3
#define SHADOW_OFFSET
ex7fff8000ULL
Il same mapping as x86-64 ASan
#define POISON_REDZONE exfa
#define POISON FREED
exfd
#define REDZONE
16
// bytes on each side of a block
static int gready;
static const char **g_sites; static unsigned gnsites;
static inline uint8_t *shadow_of (uintptr_t a) {
return (uint8_t *)(a > SHADOW_SCALE) + SHADOW_OFFSET);
// The heap lives in a fixed region so we only need shadow for that range.
/ Shadow for the rest of the address space is never touched. #define HEAP_BASE Bx600000000000ULL
#define HEAP_SIZE (1ULL «< 32)
11 4 GiB of address space, lazily backed
static uintptr_t g heap_top = HEAP_BASE;
static void die(const char *fmt, ...) {
va_list ap; a_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end (ap);
_exit (1);
attribute_((constructor())) static void masan_init(void) {
// Reserve the heap and its shadow. MAP_NORESERVE: address space only;
// pages are backed on first touch, so a huge reservation costs nothing.
void *h - mmap((void *)HEAP_BASE, HEAP_SIZE, PROT _READ | PROT _WRITE,
MAP_ PRIVATE | MAP ANONYMOUS | MAP_FIXED _NOREPLACE | MAP_NORESERVE, -1, 0);
if (h - MAP FAILED) die("masan: cannot reserve heap\n");
uintptr_t 5_10 = (uintptrt)shadow_of (HEAP_BASE) & ~BxfffULL;
uintptr_t s_hi - ((uintptr_t) shadow_of (HEAP_BASE + HEAP_SIZE) + exfff) & ~OxffFULL;
void *5 =
mmap( (void *)s_lo, s_hi - s_10, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE | MAP_NORESERVE, -1, 8);
if (s - MAP_FAILED) die("masan: cannot reserve shadow\n");
// Fresh anonymous memory is zero = "addressable". Poison the whole heap
// until malloc hands pieces out.
memset( (void *)s_10, POISON_REDZONE, 5_h1 - 5_10);
g_ready • 1;
static void poison(intptr_t a, size_t n, uinta_t v) {
I1 a, n multiples of 8
memset (shadow_of (a), v, n» SHADOW_SCALE);
｝
static void unpoison(uintptr_t a, size_t n) {
memset（shadow_of （a），0, n 2> SHADOM_SCALE）；
if (n & 7) *shadow_of (a + (n & ~7ULL)) = n & 7;
11 a multiple of 8
// partial last granule
// ---- allocator
// Layout: [16B left redzone | header... ][user bytes][pad][16B right redzone]
// The header (requested size) sits inside the left redzone.
typedef struct { size_t size; size_t magic; } hdr_t;
#define MAGIC 8x6d6173616e5f6f6bULL
static, void *masan_alloc(size_t n) {
size_t user = (n + 7) & ~7ULL;
size_t total = REDZONE + user + REDZONE;
uintptr_t bik - _atomic_fetch_add(&g_heap_top,
total,ATOMIC_RELAXED):
if (blk + total > HEAP_BASE + HEAP_SIZE) return NULL;
uintptr_t p = blk + REDZONE;

hdr_t *h- (hdr_t *)(P - sizeof(hdr_t));
h-›size = n; h->magic = MAGIC;
poison(blk, REDZONE, POISON_REDZONE):
unpoison (p, n):
poison(p + user, REDZONE, POISON_REDZONE):
return (void *)p;
static int is_ours(void *p)
uintptr_t a = (uintptr_t)p;
return a > HEAP_BASE && a < HEAP_BASE + HEAP_SIZE;
void *malloc(size_t n) ( return masan_alloc(n? n : 1); )
void *calloc(size_t a, size_t b) {
size_t n = a * b; void *p = masan_alloc(n ? n: 1):
if (p)
memset (p, e, n);
/ memset is not instrumented (libc), shadow says oK
return p;
void free(void *p) {
if (lp) return; if (lis_ours(p)) return;
// pre-init allocations etc.
har_t *h= (hdr t *)((uintptr_t)p - sizeof(hdr_t)):
if (*shadow_of((uintptr_t)p) - POISON_FREED)
die("-=masan== ERROR: double-free of %p\n", p);
if (h->magic I= MAGIC)
die("=-masan== ERROR: free of invalid pointer %p\n", p);
// Quarantine: never reuse. Poisoning as FREED makes later use detectable.
poison((intptr_t)p, (h->size + 7) &~7ULL, POISON_FREED);
}
void realloc(void *p, size_t n) {
if (lp) return malloc(n);
har_t *h = (hdr_t *)((uintptr_t)p - sizeof(hdr_t));
void *q = malloc(n);
if (q) { memcpy(q, p, h-›size < n ? h-›size : n); free(p); }
return q;
}
11
-- reporting
void
_masan_register_sitesconst char **sites, unsigned n) {
/ One module in this lab; real runtimes keep a list per module.
E_sites = sites; g nsites = n;
static void report(uintptr_t a, uint64_t size, int is write, unsigned site, uint8_t sv) {
const char *kind - sv - POISON FREED? "heap-use-after-free" :
"heap-buffer-overflow";
fprintf (stderr,
*==masan== ERROR: %s on address %#1x\n", kind, funsigned long)a):
fprintf (stderr,
- %s of size %lu at %s\n", is write ? "WRITE"
"READ",
(unsigned long)size, site ‹ g_nsites ? g_sites[site] : "‹unknown›");
1/ Describe the nearest block: scan left for the header magic.
for (uintptr_t p = (a & ~7ULL); p > HEAP_BASE + REDZONE && p> a - 4096; p -= 8) {
hdr_t *h = (hdr_t *)(P - sizeof(hdr_t));
if (*shadow_of(p - 8) - POISON_REDZONE && h-›magic == MAGIC) {
long off = (long)(a - p);
if (off >= (long)h->size) fprintf(stderr,
address is %ld bytes after a %zu-byte block at *#Ix\n",
off - (long)h->size, h-›size, (unsigned long)p);
else
fprintf(stderr, " address is %ld bytes inside a %zu-byte block at *#Ix\n",
off, h-›size, (unsigned long)p);
break;
_exit (1);
// ---- the check called before every instrumented load/store •••••. void
masan_check(void addr, uint64_t size, int is_write, unsigned site) (
uintptr_t a = (uintptr_t)addr;
if (Ig ready |l lis_ours(addr)) return;
1/ only heap memory is tracked
// Check every granule the access touches,
for (uintptr_t x = a; x ‹ a + size; x = (x | 7) + 1) { uint&_t sv - *shadow_of(x);
if (sV == e) continue;
// fully addressable granule
// Partial granule: bytes [0, sv) are addressable. The access covers bytes
// [x&7, last] of this granule; it's fine iff its last byte is below sv.
uintptr_t gend - (x | 7) + 1;
uintptr_t last - (a + size < gend ? a + size : gend) - 1; if (Sv < 8 && (last & 7) < sv) continue; reporta, size, is write, site, sv);

