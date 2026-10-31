/* probe: a deliberately badly behaved program used to test isolation.
 * At birth it counts its seats and tries to speak everywhere (all refused).
 * Once invited to "chat" it obeys lines of the form "@<its id> <command>". */
#include "colloquy.h"

static int me, chat;
static void report(const char *s) { char line[TURN_TEXT + 1]; fmt(line, sizeof line, "probe %d: %s", me, s); say_all(chat, line); }

int main(void) {
    me = self_id();
    struct seat_info si; int born_with = 0, refused = 0;
    for (int i = 0; i < 16; i++) if (seat_info(i, &si) == 0) born_with++;
    for (int i = 0; i < 4; i++) if (speak(i, "let me out", 10) < 0) refused++;

    chat = wait_for_seat("chat", SEAT_LISTEN | SEAT_SPEAK);
    char msg[TURN_TEXT + 1], tag[12];
    fmt(msg, sizeof msg, "ready; born with %d seat(s); %d of 4 speak attempts refused", born_with, refused);
    report(msg);
    fmt(tag, sizeof tag, "@%d ", me);
    usize tl = ustrlen(tag);

    seat_info(chat, &si); u32 cursor = si.count; struct turn t;
    for (;;) {
        if (listen_t(chat, &cursor, &t, 0) < 0) continue;
        if (t.author == (u32)me) continue;
        char text[TURN_TEXT + 1]; memcpy(text, t.text, t.len); text[t.len] = 0;
        if (ustrncmp(text, tag, tl)) continue;
        char *cmd = text + tl;
        if (!ustrcmp(cmd, "ring")) {
            u16 cs; __asm__ volatile("mov %%cs, %0" : "=r"(cs));
            fmt(msg, sizeof msg, "running at ring %u", (u32)(cs & 3)); report(msg);
        } else if (!ustrcmp(cmd, "badsys")) {
            i64 r = syscall4(99, 0, 0, 0, 0);
            fmt(msg, sizeof msg, "system call 99 returned %d (%s)", (int)r, err_str((int)r)); report(msg);
        } else if (!ustrcmp(cmd, "badptr")) {
            int r = speak(chat, (const char *)0x100000, 32);   /* kernel memory as turn text */
            fmt(msg, sizeof msg, "speak with a kernel pointer returned %d (%s)", r, err_str(r)); report(msg);
        } else if (!ustrcmp(cmd, "kwrite")) {
            report("writing to kernel memory at 0x100000...");
            *(volatile u64 *)0x100000 = 0xdead;
            report("kernel write SUCCEEDED (isolation broken!)");
        } else if (!ustrncmp(cmd, "peek ", 5)) {
            int ok; u64 a = parse_num(cmd + 5, &ok);
            fmt(msg, sizeof msg, "reading %x...", a); report(msg);
            u8 v = *(volatile u8 *)a;
            fmt(msg, sizeof msg, "read %x = %u (memory was reachable)", a, (u32)v); report(msg);
        } else if (!ustrcmp(cmd, "ud2")) {
            report("executing an invalid instruction...");
            __asm__ volatile("ud2");
        } else if (!ustrcmp(cmd, "io")) {
            report("touching an I/O port...");
            __asm__ volatile("outb %%al, $0x80" :: "a"(0));
            report("port write SUCCEEDED (isolation broken!)");
        } else if (!ustrcmp(cmd, "hello")) {
            report("hello, I can speak here because I was invited");
        } else {
            report("unknown command");
        }
    }
}
