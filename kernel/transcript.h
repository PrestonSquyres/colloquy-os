#pragma once
#include "types.h"
#include "abi.h"
#include "sched.h"

/* A transcript is Colloquy's only IPC/storage object: an append-only,
 * authored, hash-chained log of turns. There are no files, pipes or sockets.
 * Memory holds the newest MAX_TURNS turns; older ones are dropped, leaving
 * their hash as the chain's anchor (M3 moves them to disk instead). */
#define MAX_TRANSCRIPTS 32
#define MAX_TURNS 256
#define KERNEL_AUTHOR 0xFFFFFFFFu
#define LEDGER 0

struct transcript {
    int used; char name[TNAME]; int creator;
    u32 count;            /* turns ever spoken; next seq */
    u32 base;             /* oldest seq still in memory */
    u8 anchor[32];        /* hash of turn base-1 (zeros if base == 0) */
    u8 head[32];
    struct turn turns[MAX_TURNS];   /* turn s lives at turns[s % MAX_TURNS] */
};

void transcripts_init(void);

/* Seat-checked API used by kernel tasks directly and by system calls on
 * behalf of user programs. `seat` indexes the calling task's seat table. */
int  t_open(const char *name);                         /* -> seat */
int  t_invite(int seat, int task_id, u8 rights);       /* -> seat in target */
int  t_speak(int seat, const char *text, u32 len);     /* -> seq */
int  t_speak_kind(int seat, u16 kind, const char *text, u32 len);
int  t_listen(int seat, u32 *cursor, struct turn *out, int flags);
int  t_seat_find(const char *name, u8 need);           /* caller's seat by name */
int  t_seat_info(int seat, struct seat_info *out);

/* Kernel-only. */
int  t_create_inbox(struct task *t);                   /* kernel-owned; task gets listen */
int  t_grant(int task_id, int tid, u8 rights);         /* boot wiring, logged */
void t_release(int tid);
int  t_find(const char *name);
struct transcript *t_get(int tid);
struct turn *t_turn(struct transcript *x, u32 seq);    /* NULL if not held */
int  t_verify(int tid, u32 *bad_seq);                  /* 0 ok, 1 broken */
void t_hash_turn(const struct turn *t, u8 out[32]);
void ledger_note(const char *fmt, ...);
const char *t_err(int e);
void fmt_into(char *out, usize cap, const char *fmt, ...);
