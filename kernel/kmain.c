#include "types.h"
#include "console.h"
#include "arch.h"
#include "sched.h"
#include "transcript.h"
#include "tasks.h"

extern u64 mb_info_addr, mb_magic;

static void boot_info(void) {
    if ((u32)mb_magic != 0x36d76289) { kputs("warning: not booted by a Multiboot2 loader\n"); return; }
    u8 *p = (u8 *)(mb_info_addr & 0xFFFFFFFF) + 8;
    for (;;) {
        u32 type = *(u32 *)p, size = *(u32 *)(p + 4);
        if (type == 0) break;
        if (type == 2) kprintf("  loader: %s\n", (const char *)(p + 8));
        if (type == 4) kprintf("  memory: %u MiB\n", (*(u32 *)(p + 12)) / 1024 + 1);
        p += (size + 7) & ~7u;
    }
}

void kmain(void) {
    console_init();
    kputs("Colloquy OS 0.1 (milestone 1) -- a conversation-native kernel\n");
    boot_info();
    sched_init();
    transcripts_init();
    arch_init();

    shell_id  = task_spawn("shell",  shell_main,  50);
    clock_id  = task_spawn("clock",  clock_main,  10);
    scribe_id = task_spawn("scribe", scribe_main, 10);
    hog_id    = task_spawn("hog",    hog_main,     5);
    t_grant(shell_id, 0, SEAT_LISTEN);  /* the shell may read kernel.ledger */
    kprintf("  tasks: shell=%d clock=%d scribe=%d hog=%d; timer %u Hz\n", shell_id, clock_id, scribe_id, hog_id, HZ);

    __asm__ volatile("sti");
    for (;;) __asm__ volatile("hlt");  /* this is now the idle task */
}
