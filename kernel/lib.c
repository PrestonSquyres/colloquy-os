#include "lib.h"
void *memcpy(void *d, const void *s, usize n) { u8 *a = d; const u8 *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c, usize n) { u8 *a = d; while (n--) *a++ = (u8)c; return d; }
int memcmp(const void *x, const void *y, usize n) {
    const u8 *a = x, *b = y;
    for (; n; n--, a++, b++) if (*a != *b) return *a - *b;
    return 0;
}
usize strlen(const char *s) { usize n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return (u8)*a - (u8)*b; }
int strncmp(const char *a, const char *b, usize n) {
    for (; n; n--, a++, b++) { if (*a != *b) return (u8)*a - (u8)*b; if (!*a) return 0; }
    return 0;
}
void strlcpy(char *d, const char *s, usize n) { usize i = 0; if (!n) return; for (; i + 1 < n && s[i]; i++) d[i] = s[i]; d[i] = 0; }
u64 parse_u64(const char *s, int *ok) {
    u64 v = 0; *ok = 0;
    if (!*s) return 0;
    for (; *s; s++) { if (*s < '0' || *s > '9') return 0; v = v * 10 + (u64)(*s - '0'); }
    *ok = 1; return v;
}
