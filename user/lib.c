#include "colloquy.h"
#include <stdarg.h>

usize ustrlen(const char *s) { usize n = 0; while (s[n]) n++; return n; }
int ustrcmp(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return (u8)*a - (u8)*b; }
int ustrncmp(const char *a, const char *b, usize n) {
    for (; n; n--, a++, b++) { if (*a != *b) return (u8)*a - (u8)*b; if (!*a) return 0; }
    return 0;
}
void *memcpy(void *d, const void *s, usize n) { u8 *a = d; const u8 *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c, usize n) { u8 *a = d; while (n--) *a++ = (u8)c; return d; }

void fmt(char *out, usize cap, const char *f, ...) {
    va_list ap; va_start(ap, f); usize o = 0;
#define PUT(ch) do { if (o + 1 < cap) out[o++] = (ch); } while (0)
    for (; *f; f++) {
        if (*f != '%') { PUT(*f); continue; }
        f++;
        int left = 0, width = 0; char padc = ' ';
        if (*f == '-') { left = 1; f++; }
        if (*f == '0') { padc = '0'; f++; }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        char tmp[24]; const char *s = tmp; int n = 0;
        if (*f == 's') { s = va_arg(ap, const char *); n = (int)ustrlen(s); }
        else if (*f == 'c') { tmp[0] = (char)va_arg(ap, int); n = 1; }
        else if (*f == 'd' || *f == 'u' || *f == 'x') {
            u64 v; int neg = 0, base = *f == 'x' ? 16 : 10;
            if (*f == 'd') { i64 x = va_arg(ap, int); neg = x < 0; v = (u64)(neg ? -x : x); }
            else if (*f == 'u') v = va_arg(ap, u32); else v = va_arg(ap, u64);
            char b[24]; int i = 0;
            do { b[i++] = "0123456789abcdef"[v % (u64)base]; v /= (u64)base; } while (v);
            if (neg) b[i++] = '-';
            while (i) tmp[n++] = b[--i];
        } else { tmp[0] = *f; n = 1; }
        if (!left) for (int i = n; i < width; i++) PUT(padc);
        for (int i = 0; i < n; i++) PUT(s[i]);
        if (left) for (int i = n; i < width; i++) PUT(' ');
    }
    out[o] = 0; va_end(ap);
#undef PUT
}

int say_all(int seat, const char *s) {
    usize n = ustrlen(s); int r = 0;
    while (n) { u32 k = n > TURN_TEXT ? TURN_TEXT : (u32)n; r = speak(seat, s, k); if (r < 0) return r; s += k; n -= k; }
    return r;
}

int wait_for_seat(const char *name, u8 rights) {
    u32 cursor = 0; struct turn t;
    for (;;) {
        int s = seat_find(name, rights);
        if (s >= 0) return s;
        if (listen_t(INBOX_SEAT, &cursor, &t, 0) < 0) sleep_ticks(HZ);
    }
}

const char *err_str(int e) {
    switch (e) {
    case E_NOSEAT: return "denied: no seat with that right";
    case E_FULL: return "transcript full";
    case E_NOSPACE: return "out of slots";
    case E_EXISTS: return "already exists";
    case E_BADARG: return "bad argument";
    case E_EMPTY: return "nothing new";
    case E_FAULT: return "bad pointer";
    case E_NOSYS: return "no such system call";
    default: return "error";
    }
}

u64 parse_num(const char *s, int *ok) {
    u64 v = 0; *ok = 0;
    if (!*s) return 0;
    if (s[0] == '0' && s[1] == 'x') {
        s += 2; if (!*s) return 0;
        for (; *s; s++) {
            int d = (*s >= '0' && *s <= '9') ? *s - '0' : (*s >= 'a' && *s <= 'f') ? *s - 'a' + 10 : -1;
            if (d < 0) return 0;
            v = v * 16 + (u64)d;
        }
    } else for (; *s; s++) { if (*s < '0' || *s > '9') return 0; v = v * 10 + (u64)(*s - '0'); }
    *ok = 1; return v;
}
