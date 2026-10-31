/* kernel.desk: the kernel takes part in conversations like everyone else.
 * A task holding a speak seat on kernel.desk can ask questions (ps, ls,
 * verify ...) and give orders (budget, spawn); the kernel answers with turns,
 * ending each answer with an empty TURN_END turn. The seat is the authority. */
#include "kernel_tasks.h"
#include "transcript.h"
#include "console.h"
#include "elf.h"
#include "mm.h"
#include "lib.h"

static int desk_seat;
#define LINE 160

static void say(const char *s) {
    u32 n = (u32)strlen(s);
    do { u32 k = n > TURN_TEXT ? TURN_TEXT : n; t_speak_kind(desk_seat, TURN_SAY, s, k); s += k; n -= k; } while (n);
}

static char *next_word(char **p) {
    while (**p == ' ') (*p)++;
    if (!**p) return NULL;
    char *w = *p;
    while (**p && **p != ' ') (*p)++;
    if (**p) *(*p)++ = 0;
    return w;
}

static void pad(char *s, usize w) { usize n = strlen(s); while (n < w) s[n++] = ' '; s[n] = 0; }
static void hex8(char *out, const u8 *h) { const char *d = "0123456789abcdef"; for (int i = 0; i < 4; i++) { out[2*i] = d[h[i] >> 4]; out[2*i+1] = d[h[i] & 15]; } out[8] = 0; }

static const char *state_name(enum task_state s) {
    switch (s) { case T_READY: return "ready"; case T_SLEEP: return "sleep"; case T_WAIT: return "listen";
                 case T_DEAD: return "dead"; default: return "?"; }
}

static void cmd_ps(void) {
    say("  id name        ring state   budget  cpu/last-s  starved  lifetime\n");
    for (int i = 0; i < MAX_TASKS; i++) {
        struct task *t = task_get(i); if (!t) continue;
        char line[LINE], col[24];
        fmt_into(col, sizeof col, "  %d", t->id); pad(col, 5); strlcpy(line, col, LINE);
        strlcpy(col, t->name, sizeof col); pad(col, 12); strlcpy(line + strlen(line), col, LINE - strlen(line));
        fmt_into(col, sizeof col, "%d", t->user ? 3 : 0); pad(col, 5); strlcpy(line + strlen(line), col, LINE - strlen(line));
        strlcpy(col, state_name(t->state), sizeof col); pad(col, 8); strlcpy(line + strlen(line), col, LINE - strlen(line));
        if (t->budget == BUDGET_UNLIMITED) strlcpy(col, "   -", sizeof col); else fmt_into(col, sizeof col, "%u/s", t->budget);
        pad(col, 11); strlcpy(line + strlen(line), col, LINE - strlen(line));
        fmt_into(col, sizeof col, "%u%%", t->last_epoch_used); pad(col, 9); strlcpy(line + strlen(line), col, LINE - strlen(line));
        fmt_into(col, sizeof col, "%u", t->starved_epochs); pad(col, 9); strlcpy(line + strlen(line), col, LINE - strlen(line));
        fmt_into(col, sizeof col, "%us\n", (u32)(t->cpu_ticks / HZ)); strlcpy(line + strlen(line), col, LINE - strlen(line));
        say(line);
        if (t->state == T_DEAD && t->death[0]) { fmt_into(line, LINE, "       died: %s\n", t->death); say(line); }
    }
}

static void cmd_ls(void) {
    say("  name            turns  held  creator     head\n");
    for (int i = 0; i < MAX_TRANSCRIPTS; i++) {
        struct transcript *x = t_get(i); if (!x) continue;
        char line[LINE], col[32], h[9];
        strlcpy(col, "  ", sizeof col); strlcpy(col + 2, x->name, sizeof col - 2); pad(col, 18); strlcpy(line, col, LINE);
        fmt_into(col, sizeof col, "%u", x->count); pad(col, 7); strlcpy(line + strlen(line), col, LINE - strlen(line));
        fmt_into(col, sizeof col, "%u", x->count - x->base); pad(col, 6); strlcpy(line + strlen(line), col, LINE - strlen(line));
        struct task *c = x->creator >= 0 ? task_get(x->creator) : NULL;
        strlcpy(col, x->creator < 0 ? "kernel" : (c ? c->name : "?"), sizeof col); pad(col, 12); strlcpy(line + strlen(line), col, LINE - strlen(line));
        hex8(h, x->head); fmt_into(col, sizeof col, "%s...\n", h); strlcpy(line + strlen(line), col, LINE - strlen(line));
        say(line);
    }
}

static void cmd_verify(char *p) {
    char *name = next_word(&p), line[LINE];
    int tid = name ? t_find(name) : -1;
    if (tid < 0) { say("usage: verify <transcript>\n"); return; }
    u32 bad = 0; struct transcript *x = t_get(tid);
    if (t_verify(tid, &bad)) fmt_into(line, LINE, "verify %s: BROKEN at turn #%u (history was altered)\n", name, bad);
    else fmt_into(line, LINE, "verify %s: OK, turns %u..%u held, chain intact\n", name, x->base, x->count ? x->count - 1 : 0);
    say(line);
}

static void cmd_tamper(char *p) {
    char *name = next_word(&p), *ss = next_word(&p), line[LINE]; int ok = 0;
    int tid = name ? t_find(name) : -1; u64 seq = ss ? parse_u64(ss, &ok) : 0;
    struct transcript *x = t_get(tid);
    struct turn *t = (x && ok) ? t_turn(x, (u32)seq) : NULL;
    if (!t) { say("usage: tamper <transcript> <seq>\n"); return; }
    if (t->len) t->text[0] ^= 0x20; else t->tick ^= 1;
    fmt_into(line, LINE, "tampered with %s turn #%u directly in memory (bypassing the kernel API)\n", name, (u32)seq);
    say(line);
}

static void cmd_budget(char *p) {
    char *a = next_word(&p), *b = next_word(&p), line[LINE]; int ok1 = 0, ok2 = 0;
    u64 id = a ? parse_u64(a, &ok1) : 0, v = b ? parse_u64(b, &ok2) : 0;
    struct task *t = ok1 ? task_get((int)id) : NULL;
    if (!t || !ok2 || id == 0 || v == 0 || v > 100) { say("usage: budget <task id> <1-100>\n"); return; }
    t->budget = (u32)v;
    ledger_note("desk set budget of task %d to %d", (int)id, (int)v);
    fmt_into(line, LINE, "task %u '%s' budget is now %u ticks/s\n", (u32)id, t->name, (u32)v);
    say(line);
}

static void cmd_spawn(char *p) {
    char *name = next_word(&p), line[LINE];
    if (!name) { say("usage: spawn <program>\n"); return; }
    int id = spawn_program(name);
    if (id < 0) fmt_into(line, LINE, "spawn %s: %s\n", name, t_err(id));
    else fmt_into(line, LINE, "spawned '%s' as task %d\n", name, id);
    say(line);
}

static void cmd_seatsof(char *p) {
    char *a = next_word(&p), line[LINE]; int ok = 0;
    u64 id = a ? parse_u64(a, &ok) : 0; struct task *t = ok ? task_get((int)id) : NULL;
    if (!t) { say("usage: seatsof <task id>\n"); return; }
    int n = 0;
    for (int i = 0; i < MAX_SEATS; i++) {
        struct seat *s = &t->seats[i]; struct transcript *x = t_get(s->tid);
        if (!s->rights || !x) continue;
        n++;
        fmt_into(line, LINE, "  seat %d %s %s%s%s\n", i, x->name, (s->rights & SEAT_LISTEN) ? "listen " : "",
                 (s->rights & SEAT_SPEAK) ? "speak " : "", (s->rights & SEAT_INVITE) ? "invite" : "");
        say(line);
    }
    fmt_into(line, LINE, "  task %u '%s' holds %d seat(s)\n", (u32)id, t->name, n);
    say(line);
}

static void cmd_where(char *p) {
    char *a = next_word(&p), line[LINE]; int ok = 0;
    u64 id = a ? parse_u64(a, &ok) : 0; struct task *t = ok ? task_get((int)id) : NULL;
    if (!t || !t->user) { say("usage: where <user task id>\n"); return; }
    fmt_into(line, LINE, "task %u '%s': address space %x, first program page at physical %x\n",
             (u32)id, t->name, t->cr3, t->first_frame);
    say(line);
}

static void cmd_modules(void) {
    char line[LINE];
    for (int i = 0; i < nmodules; i++) {
        fmt_into(line, LINE, "  %s  %u bytes  budget %u/s  %s\n", modules[i].name, (u32)(modules[i].end - modules[i].start),
                 modules[i].budget, modules[i].autostart ? "starts at boot" : "start with: spawn");
        say(line);
    }
}

static void handle(char *p) {
    char *cmd = next_word(&p), line[LINE];
    if (!cmd) return;
    if (!strcmp(cmd, "ps")) cmd_ps();
    else if (!strcmp(cmd, "ls")) cmd_ls();
    else if (!strcmp(cmd, "verify")) cmd_verify(p);
    else if (!strcmp(cmd, "tamper")) cmd_tamper(p);
    else if (!strcmp(cmd, "budget")) cmd_budget(p);
    else if (!strcmp(cmd, "spawn")) cmd_spawn(p);
    else if (!strcmp(cmd, "seatsof")) cmd_seatsof(p);
    else if (!strcmp(cmd, "where")) cmd_where(p);
    else if (!strcmp(cmd, "modules")) cmd_modules();
    else if (!strcmp(cmd, "uptime")) { fmt_into(line, LINE, "%u ticks, %us, %u free pages\n", (u32)ticks, (u32)(ticks / HZ), (u32)mm_free_frames()); say(line); }
    else if (!strcmp(cmd, "kfault")) __asm__ volatile("ud2");
    else { fmt_into(line, LINE, "unknown command '%s' (try help)\n", cmd); say(line); }
}

void desk_main(void) {
    struct task *me = task_current();
    int tid = t_find("kernel.desk");
    desk_seat = t_grant(me->id, tid, SEAT_LISTEN | SEAT_SPEAK | SEAT_INVITE);
    while (shell_id < 0) task_sleep(1);
    t_invite(desk_seat, shell_id, SEAT_LISTEN | SEAT_SPEAK);
    u32 cursor = t_get(tid)->count; struct turn t;
    for (;;) {
        if (t_listen(desk_seat, &cursor, &t, 0) < 0) { task_sleep(HZ); continue; }
        if (t.author == (u32)me->id || t.kind != TURN_SAY) continue;
        char buf[TURN_TEXT + 1]; memcpy(buf, t.text, t.len); buf[t.len] = 0;
        handle(buf);
        t_speak_kind(desk_seat, TURN_END, "", 0);
    }
}

/* init: boot wiring. Programs are born with only an inbox; the kernel's init
 * introduces them according to this table, and every grant is on the ledger. */
static const struct { const char *transcript, *program; u8 rights; } wiring[] = {
    { "clock", "scribe", SEAT_LISTEN },
    { "clock", "shell",  SEAT_LISTEN },
    { "notes", "shell",  SEAT_LISTEN },
};

static int task_by_name(const char *n) {
    for (int i = 0; i < MAX_TASKS; i++) { struct task *t = task_get(i); if (t && t->state != T_DEAD && !strcmp(t->name, n)) return i; }
    return -1;
}

void init_main(void) {
    int n = sizeof wiring / sizeof wiring[0], done = 0; u8 did[8] = {0};
    for (u64 waited = 0; done < n && waited < 30 * HZ; waited += 5) {
        for (int i = 0; i < n; i++) {
            if (did[i]) continue;
            int tid = t_find(wiring[i].transcript), task = task_by_name(wiring[i].program);
            if (tid >= 0 && task >= 0) { t_grant(task, tid, wiring[i].rights); did[i] = 1; done++; }
        }
        task_sleep(5);
    }
}
