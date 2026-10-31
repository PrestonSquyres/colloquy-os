#pragma once
#include "types.h"
#include "sched.h"

/* A transcript is Colloquy's only IPC/storage object: an append-only,
 * authored, hash-chained log of turns. There are no files, pipes or sockets. */
#define MAX_TRANSCRIPTS 16
#define MAX_TURNS 256
#define TURN_TEXT 96
#define TNAME 24

enum turn_kind { TURN_SAY = 1, TURN_NOTE = 2, TURN_DENIED = 3 };

struct turn {
    u32 seq; u32 author; u64 tick;
    u16 kind; u16 len;
    char text[TURN_TEXT];
    u8 prev[32];   /* hash of the previous turn (zeros for the first) */
    u8 hash[32];   /* SHA-256(prev || seq || author || tick || kind || len || text) */
};

struct transcript {
    int used; char name[TNAME]; int creator;
    u32 count; u8 head[32];
    struct turn turns[MAX_TURNS];
};

/* Errors */
#define E_NOSEAT  -1   /* caller holds no seat with the needed right */
#define E_FULL    -2
#define E_NOSPACE -3
#define E_EXISTS  -4
#define E_BADARG  -5

void transcripts_init(void);

/* Seat-checked API: the only way tasks touch transcripts. Seat = index into
 * the calling task's seat table. */
int  t_open(const char *name);                         /* -> seat */
int  t_invite(int seat, int task_id, u8 rights);       /* -> seat in target */
int  t_speak(int seat, const char *text);              /* -> seq */
int  t_listen(int seat, u32 *cursor, struct turn *out);/* blocks until a turn */
int  t_seat_find(const char *name, u8 need);           /* caller's seat by name */

/* Introspection used by the shell and tests. */
struct transcript *t_get(int tid);
int  t_verify(int tid, u32 *bad_seq);                  /* 0 ok, 1 broken */
void t_hash_turn(const struct turn *t, u8 out[32]);
void ledger_note(const char *fmt, ...);
const char *t_err(int e);
/* Kernel-authority grant (boot wiring only; logged to the ledger). */
int  t_grant(int task_id, int tid, u8 rights);
