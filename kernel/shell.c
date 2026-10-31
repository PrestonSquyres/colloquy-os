/* The Colloquy shell: a task like any other, holding only the seats it was
 * given. It reads the serial console by polling and sleeping. */
#include "tasks.h"
#include "transcript.h"
#include "console.h"
#include "lib.h"

static const char *state_name(enum task_state s) {
    switch (s) { case T_READY: return "ready"; case T_SLEEP: return "sleep"; case T_WAIT: return "listen";
                 case T_DEAD: return "dead"; default: return "?"; }
}

static void readline(char *buf, int cap) {
    int n = 0;
    for (;;) {
        int c = serial_getc();
        if (c < 0) { task_sleep(1); continue; }
        if (c == '\r' || c == '\n') { kputc('\n'); buf[n] = 0; return; }
        if ((c == 8 || c == 127) && n) { n--; kputs("\b \b"); continue; }
        if (c >= 32 && c < 127 && n + 1 < cap) { buf[n++] = (char)c; kputc((char)c); }
    }
}

static char *next_word(char **p) {
    while (**p == ' ') (*p)++;
    if (!**p) return NULL;
    char *w = *p;
    while (**p && **p != ' ') (*p)++;
    if (**p) *(*p)++ = 0;
    return w;
}

static void hex8(const u8 *h) { for (int i = 0; i < 4; i++) kprintf("%02x", h[i]); }

static int tid_by_name(const char *name) {
    for (int i = 0; i < MAX_TRANSCRIPTS; i++) { struct transcript *x = t_get(i); if (x && !strcmp(x->name, name)) return i; }
    return -1;
}

static void author_name(u32 a, char *out) {
    if (a == 0xFFFFFFFFu) { strlcpy(out, "kernel", 16); return; }
    struct task *t = task_get((int)a);
    strlcpy(out, t ? t->name : "?", 16);
}

static void cmd_help(void) {
    kputs("  about                 what makes Colloquy different\n"
          "  ps                    tasks, budgets and CPU use\n"
          "  ls                    transcripts (the only kernel objects)\n"
          "  seats                 the seats (capabilities) this shell holds\n"
          "  read <t> [n]          last n turns of transcript <t> (needs listen seat)\n"
          "  say <t> <text>        speak a turn into <t> (needs speak seat)\n"
          "  verify <t>            recompute the SHA-256 hash chain of <t>\n"
          "  tamper <t> <seq>      DEBUG: flip a byte of a stored turn, as corruption would\n"
          "  budget <task> <ticks> set a task's CPU budget (ticks per second, max 100)\n"
          "  uptime                ticks and seconds since boot\n"
          "  fault                 TEST: trigger a CPU exception\n");
}

static void cmd_about(void) {
    kputs("Colloquy is conversation-native. It has no files, pipes or sockets.\n"
          " * Transcripts: the one kernel object. Append-only logs of authored turns.\n"
          " * Seats: unforgeable rights (listen/speak/invite) to one transcript.\n"
          "   No global namespace; you can only reach what you were invited to.\n"
          " * Hash chain: each turn carries SHA-256 of the one before it, so any\n"
          "   change to history is detectable (try: tamper, then verify).\n"
          " * Budgets: CPU is a per-second allowance, not a priority. A task that\n"
          "   spends it waits, even on an idle machine (see the hog in ps).\n"
          " * kernel.ledger: the kernel narrates its own decisions as turns.\n");
}

static void cmd_ps(void) {
    kputs("  id name      state   budget  cpu/last-s  starved  lifetime\n");
    for (int i = 0; i < MAX_TASKS; i++) {
        struct task *t = task_get(i); if (!t) continue;
        kprintf("  %-2d %-9s %-7s ", t->id, t->name, state_name(t->state));
        if (t->budget == BUDGET_UNLIMITED) kputs("   -   "); else kprintf("%3u/s  ", t->budget);
        kprintf("   %3u%%      %5u    %us\n", t->last_epoch_used, t->starved_epochs, (u32)(t->cpu_ticks / HZ));
    }
}

static void cmd_ls(void) {
    kputs("  name            turns  creator  head\n");
    for (int i = 0; i < MAX_TRANSCRIPTS; i++) {
        struct transcript *x = t_get(i); if (!x) continue;
        char cr[16]; author_name(x->creator < 0 ? 0xFFFFFFFFu : (u32)x->creator, cr);
        kprintf("  %-15s %5u  %-7s  ", x->name, x->count, cr); hex8(x->head); kputs("...\n");
    }
}

static void cmd_seats(void) {
    struct task *me = task_current();
    for (int i = 0; i < MAX_SEATS; i++) {
        struct seat *s = &me->seats[i]; if (!s->rights) continue;
        struct transcript *x = t_get(s->tid);
        kprintf("  seat %-2d %-15s %s%s%s\n", i, x ? x->name : "?", (s->rights & SEAT_LISTEN) ? "listen " : "",
                (s->rights & SEAT_SPEAK) ? "speak " : "", (s->rights & SEAT_INVITE) ? "invite" : "");
    }
}

static void cmd_read(char *p) {
    char *name = next_word(&p), *ns = next_word(&p);
    if (!name) { kputs("usage: read <transcript> [n]\n"); return; }
    int seat = t_seat_find(name, SEAT_LISTEN);
    if (seat < 0) { kprintf("read %s: %s\n", name, t_err(seat)); return; }
    struct transcript *x = t_get(task_current()->seats[seat].tid);
    int ok = 1; u64 n = ns ? parse_u64(ns, &ok) : 10; if (!ok) n = 10;
    u32 start = x->count > n ? x->count - (u32)n : 0;
    for (u32 i = start; i < x->count; i++) {
        struct turn *t = &x->turns[i]; char a[16]; author_name(t->author, a);
        kprintf("  #%-3u t=%-6u %-7s ", t->seq, (u32)t->tick, a); hex8(t->hash); kputs(" | ");
        for (int k = 0; k < t->len; k++) kputc(t->text[k]);
        kputc('\n');
    }
    if (!x->count) kputs("  (no turns yet)\n");
}

static void cmd_say(char *p) {
    char *name = next_word(&p);
    while (*p == ' ') p++;
    if (!name || !*p) { kputs("usage: say <transcript> <text>\n"); return; }
    int seat = t_seat_find(name, SEAT_SPEAK);
    if (seat < 0) { t_speak(-1, p); kprintf("say %s: %s (logged in kernel.ledger)\n", name, t_err(seat)); return; }
    int r = t_speak(seat, p);
    if (r < 0) kprintf("say: %s\n", t_err(r)); else kprintf("spoke turn #%d into %s\n", r, name);
}

static void cmd_verify(char *p) {
    char *name = next_word(&p);
    int tid = name ? tid_by_name(name) : -1;
    if (tid < 0) { kputs("usage: verify <transcript>\n"); return; }
    u32 bad = 0;
    if (t_verify(tid, &bad)) kprintf("verify %s: BROKEN at turn #%u (history was altered)\n", name, bad);
    else kprintf("verify %s: OK, %u turns, chain intact\n", name, t_get(tid)->count);
}

static void cmd_tamper(char *p) {
    char *name = next_word(&p), *ss = next_word(&p); int ok = 0;
    int tid = name ? tid_by_name(name) : -1; u64 seq = ss ? parse_u64(ss, &ok) : 0;
    struct transcript *x = t_get(tid);
    if (!x || !ok || seq >= x->count) { kputs("usage: tamper <transcript> <seq>\n"); return; }
    struct turn *t = &x->turns[seq];
    if (t->len) t->text[0] ^= 0x20; else t->tick ^= 1;
    kprintf("tampered with %s turn #%u directly in memory (bypassing the kernel API)\n", name, (u32)seq);
}

static void cmd_budget(char *p) {
    char *a = next_word(&p), *b = next_word(&p); int ok1 = 0, ok2 = 0;
    u64 id = a ? parse_u64(a, &ok1) : 0, v = b ? parse_u64(b, &ok2) : 0;
    struct task *t = ok1 ? task_get((int)id) : NULL;
    if (!t || !ok2 || id == 0 || v == 0 || v > 100) { kputs("usage: budget <task id> <1-100>\n"); return; }
    t->budget = (u32)v;
    ledger_note("shell set budget of task %d to %d", (int)id, (int)v);
    kprintf("task %u '%s' budget is now %u ticks/s\n", (u32)id, t->name, (u32)v);
}

void shell_main(void) {
    t_open("chat");
    kputs("\nType 'help' for commands, 'about' for what makes this OS different.\n");
    char line[160];
    for (;;) {
        kputs("colloquy> ");
        readline(line, sizeof line);
        char *p = line, *cmd = next_word(&p);
        if (!cmd) continue;
        if (!strcmp(cmd, "help")) cmd_help();
        else if (!strcmp(cmd, "about")) cmd_about();
        else if (!strcmp(cmd, "ps")) cmd_ps();
        else if (!strcmp(cmd, "ls")) cmd_ls();
        else if (!strcmp(cmd, "seats")) cmd_seats();
        else if (!strcmp(cmd, "read")) cmd_read(p);
        else if (!strcmp(cmd, "say")) cmd_say(p);
        else if (!strcmp(cmd, "verify")) cmd_verify(p);
        else if (!strcmp(cmd, "tamper")) cmd_tamper(p);
        else if (!strcmp(cmd, "budget")) cmd_budget(p);
        else if (!strcmp(cmd, "uptime")) kprintf("%u ticks, %us\n", (u32)ticks, (u32)(ticks / HZ));
        else if (!strcmp(cmd, "fault")) __asm__ volatile("ud2");
        else kprintf("unknown command '%s' (try help)\n", cmd);
    }
}
