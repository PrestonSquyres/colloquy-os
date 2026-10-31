#include "transcript.h"
#include "console.h"
#include "lib.h"
#include "sha256.h"
#include <stdarg.h>

static struct transcript tr[MAX_TRANSCRIPTS];

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

struct turn *t_turn(struct transcript *x, u32 seq) {
    if (seq < x->base || seq >= x->count) return NULL;
    return &x->turns[seq % MAX_TURNS];
}

/* Append with interrupts off. Caller has already checked rights. */
static int append(int tid, u32 author, u16 kind, const char *text, u32 n) {
    struct transcript *x = &tr[tid];
    if (x->count == 0xFFFFFFFFu) return E_FULL;
    struct turn *t = &x->turns[x->count % MAX_TURNS];
    if (x->count - x->base == MAX_TURNS) {   /* drop the oldest, keep its hash as anchor */
        memcpy(x->anchor, t->hash, 32);
        x->base++;
    }
    memset(t, 0, sizeof *t);
    t->seq = x->count; t->author = author; t->tick = ticks; t->kind = kind;
    if (n > TURN_TEXT) n = TURN_TEXT;
    t->len = (u16)n; memcpy(t->text, text, n);
    memcpy(t->prev, x->head, 32);
    t_hash_turn(t, t->hash);
    memcpy(x->head, t->hash, 32);
    x->count++;
    sched_wake_transcript(tid);
    return (int)t->seq;
}

/* --- tiny formatter: %s %d %u %x (u64 for %x) --- */
static int alloc_transcript(const char *name, int creator);
static void vfmt(char *out, usize cap, const char *fmt, va_list ap) {
    usize o = 0;
#define PUT(ch) do { if (o + 1 < cap) out[o++] = (ch); } while (0)
    for (; *fmt; fmt++) {
        if (*fmt != '%') { PUT(*fmt); continue; }
        fmt++;
        if (*fmt == 's') { const char *s = va_arg(ap, const char *); while (*s) PUT(*s++); }
        else if (*fmt == 'x') {
            u64 v = va_arg(ap, u64); char b[20]; int i = 0;
            do { b[i++] = "0123456789abcdef"[v & 15]; v >>= 4; } while (v);
            PUT('0'); PUT('x'); while (i) PUT(b[--i]);
        } else if (*fmt == 'd' || *fmt == 'u') {
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

void fmt_into(char *out, usize cap, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfmt(out, cap, fmt, ap); va_end(ap);
}

void ledger_note(const char *fmt, ...) {
    char buf[TURN_TEXT + 1]; va_list ap; va_start(ap, fmt);
    vfmt(buf, sizeof buf, fmt, ap); va_end(ap);
    u64 f = irq_save();
    append(LEDGER, KERNEL_AUTHOR, TURN_NOTE, buf, (u32)strlen(buf));
    irq_restore(f);
}

void transcripts_init(void) {
    memset(tr, 0, sizeof tr);
    tr[LEDGER].used = 1; tr[LEDGER].creator = -1;
    strlcpy(tr[LEDGER].name, "kernel.ledger", TNAME);
    ledger_note("ledger opened: every kernel decision is a turn here");
    alloc_transcript("kernel.desk", -1);
}

struct transcript *t_get(int tid) { return (tid >= 0 && tid < MAX_TRANSCRIPTS && tr[tid].used) ? &tr[tid] : NULL; }

int t_find(const char *name) {
    for (int i = 0; i < MAX_TRANSCRIPTS; i++) if (tr[i].used && !strcmp(tr[i].name, name)) return i;
    return -1;
}

static int alloc_transcript(const char *name, int creator) {
    if (t_find(name) >= 0) return E_EXISTS;
    for (int i = 1; i < MAX_TRANSCRIPTS; i++) if (!tr[i].used) {
        memset(&tr[i], 0, sizeof tr[i] - sizeof tr[i].turns);
        tr[i].used = 1; tr[i].creator = creator;
        strlcpy(tr[i].name, name, TNAME);
        return i;
    }
    return E_NOSPACE;
}

void t_release(int tid) { if (tid > 0 && tid < MAX_TRANSCRIPTS) tr[tid].used = 0; }

/* Tell a task, through its inbox, that it holds a new seat. */
static void notify_inbox(struct task *to, int seat, int tid, u8 rights, const char *by) {
    if (to->inbox < 0 || to->inbox == tid) return;
    char msg[TURN_TEXT + 1];
    fmt_into(msg, sizeof msg, "seat %d: %s%s%s on '%s' from %s", seat,
             (rights & SEAT_LISTEN) ? "L" : "", (rights & SEAT_SPEAK) ? "S" : "",
             (rights & SEAT_INVITE) ? "I" : "", tr[tid].name, by);
    append(to->inbox, KERNEL_AUTHOR, TURN_NOTE, msg, (u32)strlen(msg));
}

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
    if (!s->rights || (s->rights & need) != need || !tr[s->tid].used) return NULL;
    return s;
}

int t_open(const char *name) {
    if (!name || !*name || strlen(name) >= TNAME) return E_BADARG;
    struct task *me = task_current();
    if (me->user && (!strncmp(name, "inbox.", 6) || !strncmp(name, "kernel.", 7) || !strncmp(name, "console.", 8)))
        return E_BADARG;  /* names reserved for the kernel */
    u64 f = irq_save();
    int tid = alloc_transcript(name, me->id);
    if (tid < 0) { irq_restore(f); return tid; }
    int seat = add_seat(me, tid, SEAT_LISTEN | SEAT_SPEAK | SEAT_INVITE);
    if (seat < 0) { tr[tid].used = 0; irq_restore(f); return seat; }
    irq_restore(f);
    ledger_note("task %d opened transcript '%s'", me->id, name);
    return seat;
}

int t_create_inbox(struct task *t) {
    char name[TNAME];
    fmt_into(name, sizeof name, "inbox.%d", t->id);
    u64 f = irq_save();
    int tid = alloc_transcript(name, -1);
    if (tid < 0) { irq_restore(f); return tid; }
    t->inbox = tid;
    add_seat(t, tid, SEAT_LISTEN);       /* seat 0: listen on its own inbox, nothing else */
    irq_restore(f);
    return tid;
}

int t_invite(int seat, int task_id, u8 rights) {
    u64 f = irq_save();
    struct seat *s = seat_of(seat, SEAT_INVITE);
    struct task *me = task_current(), *to = task_get(task_id);
    if (!s || !to || to->state == T_DEAD || (rights & ~s->rights) || !rights) {
        irq_restore(f);
        ledger_note("DENIED invite by task %d (seat %d -> task %d)", me->id, seat, task_id);
        return s ? E_BADARG : E_NOSEAT;
    }
    int tid = s->tid;
    int r = add_seat(to, tid, rights);
    if (r >= 0) { char by[24]; fmt_into(by, sizeof by, "task %d", me->id); notify_inbox(to, r, tid, rights, by); }
    irq_restore(f);
    ledger_note("task %d invited task %d to '%s' rights=%s%s%s", me->id, task_id, tr[tid].name,
                (rights & SEAT_LISTEN) ? "L" : "", (rights & SEAT_SPEAK) ? "S" : "", (rights & SEAT_INVITE) ? "I" : "");
    return r;
}

int t_grant(int task_id, int tid, u8 rights) {
    struct task *to = task_get(task_id);
    if (!to || !t_get(tid)) return E_BADARG;
    u64 f = irq_save();
    int r = add_seat(to, tid, rights);
    if (r >= 0) notify_inbox(to, r, tid, rights, "kernel");
    irq_restore(f);
    ledger_note("kernel granted task %d seat on '%s'", task_id, tr[tid].name);
    return r;
}

int t_speak_kind(int seat, u16 kind, const char *text, u32 len) {
    u64 f = irq_save();
    struct seat *s = seat_of(seat, SEAT_SPEAK);
    struct task *me = task_current();
    if (!s) {
        irq_restore(f);
        ledger_note("DENIED speak by task %d '%s' (no speak seat)", me->id, me->name);
        return E_NOSEAT;
    }
    int r = append(s->tid, (u32)me->id, kind, text, len);
    irq_restore(f);
    return r;
}

int t_speak(int seat, const char *text, u32 len) { return t_speak_kind(seat, TURN_SAY, text, len); }

int t_listen(int seat, u32 *cursor, struct turn *out, int flags) {
    u64 f = irq_save();
    struct seat *s = seat_of(seat, SEAT_LISTEN);
    if (!s) { irq_restore(f); return E_NOSEAT; }
    int tid = s->tid;
    for (;;) {
        if (!tr[tid].used) { irq_restore(f); return E_NOSEAT; }
        if (*cursor < tr[tid].base) *cursor = tr[tid].base;   /* older turns no longer held */
        if (*cursor < tr[tid].count) break;
        if (flags & LISTEN_NOWAIT) { irq_restore(f); return E_EMPTY; }
        task_wait_transcript(tid);
    }
    *out = *t_turn(&tr[tid], *cursor);
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

int t_seat_info(int seat, struct seat_info *out) {
    struct seat *s = seat_of(seat, 0);
    if (!s) return E_NOSEAT;
    memset(out, 0, sizeof *out);
    strlcpy(out->name, tr[s->tid].name, TNAME);
    out->count = tr[s->tid].count; out->base = tr[s->tid].base; out->rights = s->rights;
    return 0;
}

int t_verify(int tid, u32 *bad_seq) {
    struct transcript *x = t_get(tid);
    if (!x) return 1;
    u8 prev[32], h[32];
    memcpy(prev, x->anchor, 32);
    for (u32 i = x->base; i < x->count; i++) {
        struct turn *t = t_turn(x, i);
        if (t->seq != i || memcmp(t->prev, prev, 32)) { *bad_seq = i; return 1; }
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
    case E_EMPTY: return "nothing new";
    case E_FAULT: return "bad pointer";
    case E_NOSYS: return "no such system call";
    default: return "error";
    }
}
