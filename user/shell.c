/* The Colloquy shell, an ordinary ring-3 program. It reads lines from
 * console.in, writes to console.out, and asks the kernel questions by
 * speaking into kernel.desk. It can only reach what it was invited to. */
#include "colloquy.h"

static int in_seat, out_seat, desk_seat;
static u32 in_cursor;

static void out(const char *s) { say_all(out_seat, s); }

static char *next_word(char **p) {
    while (**p == ' ') (*p)++;
    if (!**p) return 0;
    char *w = *p;
    while (**p && **p != ' ') (*p)++;
    if (**p) *(*p)++ = 0;
    return w;
}

static void hex8(char *o, const u8 *h) { for (int i = 0; i < 4; i++) { o[2*i] = "0123456789abcdef"[h[i] >> 4]; o[2*i+1] = "0123456789abcdef"[h[i] & 15]; } o[8] = 0; }

static void cmd_help(void) {
    out("  about                     what makes Colloquy different\n"
        "  seats                     the seats (capabilities) this shell holds\n"
        "  read <t> [n]              last n turns of <t> (needs a listen seat)\n"
        "  say <t> <text>            speak a turn into <t> (needs a speak seat)\n"
        "  invite <task> <t> <lsi>   give a task a seat on <t> (needs invite)\n");
    out("  asked of the kernel through kernel.desk:\n"
        "  ps | ls | uptime | modules | verify <t> | tamper <t> <seq>\n"
        "  budget <task> <1-100> | spawn <program> | seatsof <task> | where <task> | kfault\n");
}

static void cmd_about(void) {
    out("Colloquy is conversation-native. It has no files, pipes or sockets.\n"
        " * Transcripts: the one kernel object. Append-only logs of authored turns.\n"
        " * Seats: unforgeable rights (listen/speak/invite) to one transcript.\n"
        "   A program is born holding only its inbox; it reaches nothing else\n"
        "   until invited. Programs run in ring 3, in their own address spaces.\n");
    out(" * Hash chain: each turn carries SHA-256 of the one before it, so any\n"
        "   change to history is detectable (try: tamper, then verify).\n"
        " * Budgets: CPU is a per-second allowance, not a priority.\n"
        " * The kernel is a participant: this shell asks it questions on kernel.desk,\n"
        "   and it narrates its own decisions in kernel.ledger.\n");
}

static void cmd_seats(void) {
    char line[128]; struct seat_info si;
    for (int i = 0; i < 16; i++) {
        if (seat_info(i, &si) < 0) continue;
        fmt(line, sizeof line, "  seat %-2d %-15s %s%s%s\n", i, si.name, (si.rights & SEAT_LISTEN) ? "listen " : "",
            (si.rights & SEAT_SPEAK) ? "speak " : "", (si.rights & SEAT_INVITE) ? "invite" : "");
        out(line);
    }
}

static const char *author(u32 a, char *buf) {
    if (a == 0xFFFFFFFFu) return "kernel";
    fmt(buf, 12, "task %u", a); return buf;
}

static void cmd_read(char *p) {
    char *name = next_word(&p), *ns = next_word(&p), line[200];
    if (!name) { out("usage: read <transcript> [n]\n"); return; }
    int seat = seat_find(name, SEAT_LISTEN);
    if (seat < 0) { fmt(line, sizeof line, "read %s: %s\n", name, err_str(seat)); out(line); return; }
    struct seat_info si; seat_info(seat, &si);
    int ok = 1; u64 n = ns ? parse_num(ns, &ok) : 10; if (!ok) n = 10;
    u32 cursor = si.count > n ? si.count - (u32)n : 0; struct turn t;
    if (si.count == 0) { out("  (no turns yet)\n"); return; }
    while (cursor < si.count && listen_t(seat, &cursor, &t, LISTEN_NOWAIT) == 0) {
        char h[9], a[12], text[TURN_TEXT + 1];
        hex8(h, t.hash); memcpy(text, t.text, t.len); text[t.len] = 0;
        if (t.len && text[t.len - 1] == '\n') text[t.len - 1] = 0;
        fmt(line, sizeof line, "  #%-3u t=%-6u %-7s %s | %s\n", t.seq, (u32)t.tick, author(t.author, a), h, text);
        out(line);
    }
}

static void cmd_say(char *p) {
    char *name = next_word(&p), line[160];
    while (*p == ' ') p++;
    if (!name || !*p) { out("usage: say <transcript> <text>\n"); return; }
    int seat = seat_find(name, SEAT_SPEAK);
    if (seat < 0) {
        int any = seat_find(name, 0);                        /* any seat at all, e.g. listen-only */
        speak(any >= 0 ? any : 15, p, (u32)ustrlen(p));      /* ask anyway: the kernel records the refusal */
        fmt(line, sizeof line, "say %s: %s (logged in kernel.ledger)\n", name, err_str(seat)); out(line); return;
    }
    int r = speak(seat, p, (u32)ustrlen(p));
    if (r < 0) fmt(line, sizeof line, "say: %s\n", err_str(r)); else fmt(line, sizeof line, "spoke turn #%d into %s\n", r, name);
    out(line);
}

static void cmd_invite(char *p) {
    char *ts = next_word(&p), *name = next_word(&p), *rs = next_word(&p), line[160]; int ok = 0;
    u64 task = ts ? parse_num(ts, &ok) : 0;
    if (!ok || !name || !rs) { out("usage: invite <task> <transcript> <rights: l s i>\n"); return; }
    u8 rights = 0;
    for (char *c = rs; *c; c++) rights |= *c == 'l' ? SEAT_LISTEN : *c == 's' ? SEAT_SPEAK : *c == 'i' ? SEAT_INVITE : 0;
    int seat = seat_find(name, SEAT_INVITE);
    int r = seat < 0 ? seat : invite(seat, (int)task, rights);
    if (r < 0) fmt(line, sizeof line, "invite: %s\n", err_str(r));
    else fmt(line, sizeof line, "invited task %u to %s (their seat %d)\n", (u32)task, name, r);
    out(line);
}

/* Ask the kernel: speak the line into kernel.desk, print its turns until END. */
static void ask_desk(const char *line) {
    struct seat_info si; seat_info(desk_seat, &si);
    u32 cursor = si.count; struct turn t; int me = self_id();
    if (speak(desk_seat, line, (u32)ustrlen(line)) < 0) { out("kernel.desk refused the question\n"); return; }
    for (;;) {
        if (listen_t(desk_seat, &cursor, &t, 0) < 0) return;
        if (t.author == (u32)me) continue;
        if (t.kind == TURN_END) return;
        char text[TURN_TEXT + 1]; memcpy(text, t.text, t.len); text[t.len] = 0;
        out(text);
    }
}

static void readline(char *buf, usize cap) {
    struct turn t;
    while (listen_t(in_seat, &in_cursor, &t, 0) < 0) sleep_ticks(1);
    usize n = t.len < cap - 1 ? t.len : cap - 1;
    memcpy(buf, t.text, n); buf[n] = 0;
}

int main(void) {
    in_seat = wait_for_seat("console.in", SEAT_LISTEN);
    out_seat = wait_for_seat("console.out", SEAT_SPEAK);
    desk_seat = wait_for_seat("kernel.desk", SEAT_SPEAK | SEAT_LISTEN);
    struct seat_info si; seat_info(in_seat, &si); in_cursor = si.count;
    open_t("chat");
    out("\nType 'help' for commands, 'about' for what makes this OS different.\n");
    char line[TURN_TEXT + 1], copy[TURN_TEXT + 1];
    for (;;) {
        out("colloquy> ");
        readline(line, sizeof line);
        memcpy(copy, line, sizeof line);
        char *p = line, *cmd = next_word(&p);
        if (!cmd) continue;
        if (!ustrcmp(cmd, "help")) cmd_help();
        else if (!ustrcmp(cmd, "about")) cmd_about();
        else if (!ustrcmp(cmd, "seats")) cmd_seats();
        else if (!ustrcmp(cmd, "read")) cmd_read(p);
        else if (!ustrcmp(cmd, "say")) cmd_say(p);
        else if (!ustrcmp(cmd, "invite")) cmd_invite(p);
        else ask_desk(copy);
    }
}
