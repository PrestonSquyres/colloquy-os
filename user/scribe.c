/* scribe: waits to be invited to "clock", then relays each turn into "notes". */
#include "colloquy.h"
int main(void) {
    int out = open_t("notes");
    int in = wait_for_seat("clock", SEAT_LISTEN);
    u32 cursor = 0; struct turn t; char msg[TURN_TEXT + 1], text[TURN_TEXT + 1];
    for (;;) {
        if (listen_t(in, &cursor, &t, 0) < 0) { sleep_ticks(HZ); continue; }
        memcpy(text, t.text, t.len); text[t.len] = 0;
        fmt(msg, sizeof msg, "heard clock #%u from task %u: \"%s\"", t.seq, t.author, text);
        speak(out, msg, (u32)ustrlen(msg));
    }
}
