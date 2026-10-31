/* clock: speaks the uptime into "clock" every 5 seconds. */
#include "colloquy.h"
static u64 uptime_ticks;
int main(void) {
    int seat = open_t("clock");
    char msg[64];
    for (u64 n = 1;; n++) {
        fmt(msg, sizeof msg, "tick %u, uptime %us", (u32)n, (u32)(uptime_ticks / HZ));
        speak(seat, msg, (u32)ustrlen(msg));
        sleep_ticks(5 * HZ); uptime_ticks += 5 * HZ;
    }
}
