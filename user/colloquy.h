#pragma once
/* libcolloquy: the four verbs as ordinary functions, plus a few helpers. */
#include "abi.h"

static inline i64 syscall4(u64 n, u64 a, u64 b, u64 c, u64 d) {
    i64 r;
    register u64 r10 __asm__("r10") = d;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10) : "memory");
    return r;
}

static inline void sys_exit(int code) { syscall4(SYS_EXIT, (u64)code, 0, 0, 0); for (;;) {} }
static inline int open_t(const char *name) { return (int)syscall4(SYS_OPEN, (u64)name, 0, 0, 0); }
static inline int invite(int seat, int task, u8 rights) { return (int)syscall4(SYS_INVITE, (u64)seat, (u64)task, rights, 0); }
static inline int speak(int seat, const char *text, u32 len) { return (int)syscall4(SYS_SPEAK, (u64)seat, (u64)text, len, 0); }
static inline int listen_t(int seat, u32 *cursor, struct turn *t, int flags) { return (int)syscall4(SYS_LISTEN, (u64)seat, (u64)cursor, (u64)t, (u64)flags); }
static inline void sleep_ticks(u64 n) { syscall4(SYS_SLEEP, n, 0, 0, 0); }
static inline int seat_find(const char *name, u8 rights) { return (int)syscall4(SYS_SEAT_FIND, (u64)name, rights, 0, 0); }
static inline int seat_info(int seat, struct seat_info *si) { return (int)syscall4(SYS_SEAT_INFO, (u64)seat, (u64)si, 0, 0); }
static inline int self_id(void) { return (int)syscall4(SYS_SELF, 0, 0, 0, 0); }

#define HZ 100
#define INBOX_SEAT 0

usize ustrlen(const char *s);
int ustrcmp(const char *a, const char *b);
int ustrncmp(const char *a, const char *b, usize n);
void *memcpy(void *d, const void *s, usize n);
void *memset(void *d, int c, usize n);
void fmt(char *out, usize cap, const char *f, ...);       /* %s %d %u %x(u64) %c, width like %-8s %5u */
int  say_all(int seat, const char *s);                    /* speak, split into 96-byte turns */
int  wait_for_seat(const char *name, u8 rights);          /* block on the inbox until invited */
const char *err_str(int e);
u64  parse_num(const char *s, int *ok);                    /* decimal or 0x hex */
