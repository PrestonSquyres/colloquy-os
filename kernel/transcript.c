#include "transcript.h"
#include "console.h"
#include "lib.h"
#include "sha256.h"
#include <stdarg.h>

static struct transcript tr[MAX_TRANSCRIPTS];
#define LEDGER 0     /* transcript 0: kernel.ledger, authored by the kernel */
#define KERNEL_AUTHOR 0xFFFFFFFFu

void t_hash_turn(const struct turn *t, u8 out[32]) {
    sha256_ctx c; sha256_init(&c);
    sha256_update(&c, t->prev, 32);
    sha256_update(&c, &t->seq, 4);
    sha256_update(&c, &t->author, 4);
    sha256_update(&c, &t->tick, 8);
    sha256_update(&c, &t->kind, 2);
    sha256_update(&c, &t->len, 2);
    sha256_update(&c, t->text, t->len);
    sha256_final(&c, out);
}

/* Append with interrupts off. Caller has already checked rights. */
static int append(int tid, u32 author, u16 kind, const char *text) {
    struct transcript *x = &tr[tid];
    if (x->count >= MAX_TURNS) return E_FULL;
    struct turn *t = &x->turns[x->count];
    memset(t, 0, sizeof *t);
    t->seq = x->count; t->author = author; t->tick = ticks; t->kind = kind;
    usize n = strlen(text); if (n > TURN_TEXT) n = TURN_TEXT;
    t->len = (u16)n; memcpy(t->text, text, n);
    memcpy(t->prev, x->head, 32);
    t_hash_turn(t, t->hash);
    memcpy(x->head, t->hash, 32);
    x->count++;
    sched_wake_transcript(tid);
    return (int)t->seq;
}

/* --- tiny vsnprintf for ledger notes --- */
static void fmt_into(char *out, usize cap, const char *fmt, va_list ap) {
    usize o = 0;
#define PUT(ch) do { if (o + 1 < cap) out[o++] = (ch); } while (0)
    for (; *fmt; fmt++) {
        if (*fmt != '%') { PUT(*fmt); continue; }
        fmt++;
        if (*fmt == 's') { const char *s = va_arg(ap, const char *); while (*s) PUT(*s++); }
        else if (*fmt == 'd' || *fmt == 'u') {
            i64 v = (*fmt == 'd') ? va_arg(ap, int) : (i64)va_arg(ap, u32);
            char b[24]; int i = 0; int neg = v < 0; u64 u = (u64)(neg ? -v : v);
            do { b[i++] = (char)('0' + u % 10); u /= 10; } while (u);
            if (neg) PUT('-');
            while (i) PUT(b[--i]);
        } else PUT(*fmt);
    }
    out[o] = 0;
#undef PUT
}

void ledger_note(const char *fmt, ...) {
    char buf[TURN_TEXT + 1]; va_list ap; va_start(ap, fmt);
    fmt_into(buf, sizeof buf, fmt, ap); va_end(ap);
    u64 f = irq_save();
    append(LEDGER, KERNEL_AUTHOR, TURN_NOTE, buf);
    irq_restore(f);
}

void transcripts_init(void) {
    memset(tr, 0, sizeof tr);
    tr[LEDGER].used = 1; tr[LEDGER].creator = -1;
    strlcpy(tr[LEDGER].name, "kernel.ledger", TNAME);
    ledger_note("ledger opened: every kernel decision is a turn here");
}

struct transcript *t_get(int tid) { return (tid >= 0 && tid < MAX_TRANSCRIPTS && tr[tid].used) ? &tr[tid] : NULL; }

static int add_seat(struct task *t, int tid, u8 rights) {
    for (int i = 0; i < MAX_SEATS; i++)
        if (t->seats[i].rights && t->seats[i].tid == tid) { t->seats[i].rights |= rights; return i; }
    for (int i = 0; i < MAX_SEATS; i++)
        if (!t->seats[i].rights) { t->seats[i].tid = tid; t->seats[i].rights = rights; return i; }
    return E_NOSPACE;
}

static struct seat *seat_of(int seat, u8 need) {
    struct task *me = task_current();
    if (seat < 0 || seat >= MAX_SEATS) return NULL;
    struct seat *s = &me->seats[seat];
    if ((s->rights & need) != need) return NULL;
    return s;
}

int t_open(const char *name) {
    if (!name || !*name || strlen(name) >= TNAME) return E_BADARG;
    u64 f = irq_save();
    for (int i = 0; i < MAX_TRANSCRIPTS; i++)
        if (tr[i].used && !strcmp(tr[i].name, name)) { irq_restore(f); return E_EXISTS; }
    int tid = -1;
    for (int i = 1; i < MAX_TRANSCRIPTS; i++) if (!tr[i].used) { tid = i; break; }
    if (tid < 0) { irq_restore(f); return E_NOSPACE; }
    struct task *me = task_current();
    int seat = add_seat(me, tid, SEAT_LISTEN | SEAT_SPEAK | SEAT_INVITE);
    if (seat < 0) { irq_restore(f); return seat; }
    tr[tid].used = 1; tr[tid].creator = me->id; tr[tid].count = 0;
    strlcpy(tr[tid].name, name, TNAME); memset(tr[tid].head, 0, 32);
    irq_restore(f);
    ledger_note("task %d opened transcript '%s'", me->id, name);
    return seat;
}

int t_invite(int seat, int task_id, u8 rights) {
    u64 f = irq_save();
    struct seat *s = seat_of(seat, SEAT_INVITE);
    struct task *me = task_current(), *to = task_get(task_id);
    if (!s || !to || (rights & ~s->rights) || !rights) {
        irq_restore(f);
        ledger_note("DENIED invite by task %d (seat %d -> task %d)", me->id, seat, task_id);
        return s ? E_BADARG : E_NOSEAT;
    }
    int tid = s->tid;
    int r = add_seat(to, tid, rights);
    irq_restore(f);
    ledger_note("task %d invited task %d to '%s' rights=%s%s%s", me->id, task_id, tr[tid].name,
                (rights & SEAT_LISTEN) ? "L" : "", (rights & SEAT_SPEAK) ? "S" : "", (rights & SEAT_INVITE) ? "I" : "");
    return r;
}

int t_speak(int seat, const char *text) {
    u64 f = irq_save();
    struct seat *s = seat_of(seat, SEAT_SPEAK);
    struct task *me = task_current();
    if (!s) {
        irq_restore(f);
        ledger_note("DENIED speak by task %d '%s' (no speak seat)", me->id, me->name);
        return E_NOSEAT;
    }
    int r = append(s->tid, (u32)me->id, TURN_SAY, text);
    irq_restore(f);
    return r;
}

int t_listen(int seat, u32 *cursor, struct turn *out) {
    u64 f = irq_save();
    struct seat *s = seat_of(seat, SEAT_LISTEN);
    if (!s) { irq_restore(f); return E_NOSEAT; }
    int tid = s->tid;
    while (*cursor >= tr[tid].count) task_wait_transcript(tid);
    *out = tr[tid].turns[*cursor];
    (*cursor)++;
    irq_restore(f);
    return 0;
}

int t_seat_find(const char *name, u8 need) {
    struct task *me = task_current();
    for (int i = 0; i < MAX_SEATS; i++) {
        struct seat *s = &me->seats[i];
        if (s->rights && tr[s->tid].used && !strcmp(tr[s->tid].name, name)) return ((s->rights & need) == need) ? i : E_NOSEAT;
    }
    return E_NOSEAT;
}

int t_verify(int tid, u32 *bad_seq) {
    struct transcript *x = t_get(tid);
    if (!x) return 1;
    u8 prev[32] = {0}, h[32];
    for (u32 i = 0; i < x->count; i++) {
        struct turn *t = &x->turns[i];
        if (memcmp(t->prev, prev, 32)) { *bad_seq = i; return 1; }
        t_hash_turn(t, h);
        if (memcmp(h, t->hash, 32)) { *bad_seq = i; return 1; }
        memcpy(prev, h, 32);
    }
    if (x->count && memcmp(prev, x->head, 32)) { *bad_seq = x->count - 1; return 1; }
    return 0;
}

const char *t_err(int e) {
    switch (e) {
    case E_NOSEAT: return "denied: no seat with that right";
    case E_FULL: return "transcript full";
    case E_NOSPACE: return "out of slots";
    case E_EXISTS: return "already exists";
    case E_BADARG: return "bad argument";
    default: return "error";
    }
}

int t_grant(int task_id, int tid, u8 rights) {
    struct task *to = task_get(task_id);
    if (!to || !t_get(tid)) return E_BADARG;
    u64 f = irq_save();
    int r = add_seat(to, tid, rights);
    irq_restore(f);
    ledger_note("kernel granted task %d seat on '%s'", task_id, tr[tid].name);
    return r;
}
