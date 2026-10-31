/* Demo tasks for milestone 1. They talk only through transcripts. */
#include "tasks.h"
#include "transcript.h"
#include "console.h"
#include "lib.h"

int shell_id, clock_id, scribe_id, hog_id;
volatile u64 hog_counter;

static void utoa(u64 v, char *out) {
    char b[24]; int i = 0;
    do { b[i++] = (char)('0' + v % 10); v /= 10; } while (v);
    int o = 0; while (i) out[o++] = b[--i]; out[o] = 0;
}
static void cat(char *d, const char *s) { d += strlen(d); while ((*d++ = *s++)) {} }

/* clock: speaks the uptime into "clock" every 5 seconds. */
void clock_main(void) {
    int seat = t_open("clock");
    t_invite(seat, scribe_id, SEAT_LISTEN);
    t_invite(seat, shell_id, SEAT_LISTEN);
    for (u64 n = 1;; n++) {
        char msg[64] = "tick "; char num[24];
        utoa(n, num); cat(msg, num); cat(msg, ", uptime ");
        utoa(ticks / HZ, num); cat(msg, num); cat(msg, "s");
        if (t_speak(seat, msg) == E_FULL) { ledger_note("clock: transcript full, falling silent"); for (;;) task_sleep(1000 * HZ); }
        task_sleep(5 * HZ);
    }
}

/* scribe: listens to "clock" and writes a summary turn into "notes". */
void scribe_main(void) {
    int in;
    while ((in = t_seat_find("clock", SEAT_LISTEN)) < 0) task_sleep(1);
    int out = t_open("notes");
    t_invite(out, shell_id, SEAT_LISTEN);
    u32 cursor = 0; struct turn t;
    for (;;) {
        if (t_listen(in, &cursor, &t) < 0) { task_sleep(HZ); continue; }
        char msg[TURN_TEXT + 1] = "heard clock #"; char num[24];
        utoa(t.seq, num); cat(msg, num); cat(msg, " from task ");
        utoa(t.author, num); cat(msg, num); cat(msg, ": \"");
        usize l = strlen(msg), n = t.len; if (l + n + 2 > TURN_TEXT) n = TURN_TEXT - l - 2;
        memcpy(msg + l, t.text, n); msg[l + n] = 0; cat(msg, "\"");
        t_speak(out, msg);
    }
}

/* hog: a pure CPU burner. Its budget, not its greed, decides its share. */
void hog_main(void) { for (;;) hog_counter++; }
