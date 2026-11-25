opogie x
5 MiniASanPa&
// Planted bugs for the mini address sanitizer. Usage: /prog <ok|overflow|uaf|partial>
#include <stdio.h>
#include <stdlib.h>
#include <string-h>
static int sum(const int *a, int n) ( int s - 0;
for (int 1 = e; i ‹ n; ++i) s += a[i]: return s
｝
int main(int argc, char **argv) {
const char *mode - arge > 12 argv[1] : "ok";
int *a = malloc(10 * sizeof (int));
for (int i - e; i ‹ 10; ++1) a[i] = i;
if (Istremp(mode, "ok")) {
printf("sum - %d\n", sum(a, 10));
} else if (Istromp(mode, "overflow")) {
printf("sum = %d\n", sum(a, 11));
} else if (Istramp(mode, "uaf")) {
free (a);
printf("a[3] - %d\n", a[3]);
return e;
} else if (!stramp(mode, "partial")) {
char *s = malloc(13);
s[12] = 'x';
s[23] = "y';
printf ("Xcxeln", s[12], s[13])®
free(s):
free(a);
return e;
// reads a[10]: one past the end
// read after free
// 13 bytes: last granule is partial
// OK: byte 12 is addressable
// bad: byte 13 is in the partial granule
// read both so -01 can't drop the stores