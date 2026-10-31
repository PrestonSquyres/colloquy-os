/* The console driver: two kernel tasks that turn the serial line into
 * transcripts. Every line typed becomes a turn on console.in; every turn on
 * console.out is printed. Even the terminal is a conversation on the record. */
#include "kernel_tasks.h"
#include "transcript.h"
#include "console.h"
#include "lib.h"

int shell_id = -1;

void console_rx_main(void) {
    int seat = t_open("console.in");
    while (shell_id < 0) task_sleep(1);
    t_invite(seat, shell_id, SEAT_LISTEN);
    char buf[TURN_TEXT]; u32 n = 0;
    for (;;) {
        int c = serial_getc();
        if (c < 0) { task_sleep(1); continue; }
        if (c == '\r' || c == '\n') { kputc('\n'); t_speak(seat, buf, n); n = 0; continue; }
        if ((c == 8 || c == 127) && n) { n--; kputs("\b \b"); continue; }
        if (c >= 32 && c < 127 && n < TURN_TEXT) { buf[n++] = (char)c; kputc((char)c); }
    }
}

void console_tx_main(void) {
    int seat = t_open("console.out");
    while (shell_id < 0) task_sleep(1);
    t_invite(seat, shell_id, SEAT_SPEAK);
    struct transcript *x = t_get(task_current()->seats[seat].tid);
    u32 cursor = x->count; struct turn t;
    for (;;) {
        if (t_listen(seat, &cursor, &t, 0) < 0) { task_sleep(HZ); continue; }
        for (int i = 0; i < t.len; i++) kputc(t.text[i]);
    }
}
