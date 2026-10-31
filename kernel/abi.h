#pragma once
/* The Colloquy system call ABI, shared by the kernel and user programs.
 * Call: rax = number, args in rdi, rsi, rdx, r10; `int 0x80`; result in rax. */
#include "types.h"

#define SYS_EXIT      0   /* exit(code)                                  */
#define SYS_OPEN      1   /* open(name) -> seat                          */
#define SYS_INVITE    2   /* invite(seat, task, rights) -> seat in task  */
#define SYS_SPEAK     3   /* speak(seat, text, len) -> seq               */
#define SYS_LISTEN    4   /* listen(seat, &cursor, &turn, flags) -> 0    */
#define SYS_SLEEP     5   /* sleep(ticks)                                */
#define SYS_SEAT_FIND 6   /* seat_find(name, rights) -> own seat         */
#define SYS_SEAT_INFO 7   /* seat_info(seat, &info) -> 0 (own seats only)*/
#define SYS_SELF      8   /* self() -> own task id                       */

#define LISTEN_NOWAIT 1

#define SEAT_LISTEN 1
#define SEAT_SPEAK  2
#define SEAT_INVITE 4

#define TURN_TEXT 96
#define TNAME 24

enum turn_kind { TURN_SAY = 1, TURN_NOTE = 2, TURN_END = 4 };

struct turn {
    u32 seq; u32 author; u64 tick;
    u16 kind; u16 len;
    char text[TURN_TEXT];
    u8 prev[32];   /* hash of the previous turn (the anchor for the oldest kept) */
    u8 hash[32];   /* SHA-256(prev || seq || author || tick || kind || len || text) */
};

struct seat_info {
    char name[TNAME];
    u32 count;     /* turns ever spoken */
    u32 base;      /* oldest turn still held in memory */
    u8 rights;
};

/* Errors (negative return values) */
#define E_NOSEAT  -1   /* no seat with the needed right */
#define E_FULL    -2
#define E_NOSPACE -3
#define E_EXISTS  -4
#define E_BADARG  -5
#define E_EMPTY   -6   /* LISTEN_NOWAIT and no new turn */
#define E_FAULT   -7   /* a pointer outside the caller's memory */
#define E_NOSYS   -8   /* not one of the system calls above */
