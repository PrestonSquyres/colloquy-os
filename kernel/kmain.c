#include "types.h"
#include "console.h"
#include "arch.h"
#include "gdt.h"
#include "mm.h"
#include "sched.h"
#include "transcript.h"
#include "elf.h"
#include "kernel_tasks.h"
#include "lib.h"

extern u64 mb_info_addr, mb_magic;
extern u8 __kernel_end[];

static u64 boot_info(void) {
    u64 mem_end = 64ULL << 20, high = (u64)__kernel_end;
    if ((u32)mb_magic != 0x36d76289) { kputs("warning: not booted by a Multiboot2 loader\n"); return high; }
    u8 *info = (u8 *)(mb_info_addr & 0xFFFFFFFF), *p = info + 8;
    u64 info_end = (u64)info + *(u32 *)info;
    if (info_end > high) high = info_end;
    for (;;) {
        u32 type = *(u32 *)p, size = *(u32 *)(p + 4);
        if (type == 0) break;
        if (type == 2) kprintf("  loader: %s\n", (const char *)(p + 8));
        if (type == 3) {
            u32 s = *(u32 *)(p + 8), e = *(u32 *)(p + 12);
            modules_add((const char *)(p + 16), s, e);
            if (e > high) high = e;
        }
        if (type == 4) { u32 upper_kb = *(u32 *)(p + 12); mem_end = (1ULL << 20) + (u64)upper_kb * 1024; }
        p += (size + 7) & ~7u;
    }
    kprintf("  memory: %u MiB, %d program(s) loaded by the boot loader\n", (u32)(mem_end >> 20), nmodules);
    mm_init(high, mem_end);
    return high;
}

void kmain(void) {
    console_init();
    kputs("Colloquy OS 0.2 (milestone 2) -- a conversation-native kernel\n");
    boot_info();
    gdt_init();
    sched_init();
    transcripts_init();
    arch_init();

    int sh = spawn_program("shell");
    if (sh < 0) kputs("  no shell program found!\n");
    t_grant(sh, 0, SEAT_LISTEN);  /* the shell may read kernel.ledger */
    shell_id = sh;
    task_spawn_kernel("console.rx", console_rx_main, 20);
    task_spawn_kernel("console.tx", console_tx_main, 30);
    task_spawn_kernel("desk",       desk_main,       30);
    task_spawn_kernel("init",       init_main,       10);
    for (int i = 0; i < nmodules; i++)
        if (modules[i].autostart && strcmp(modules[i].name, "shell") != 0) spawn_program(modules[i].name);
    kprintf("  shell is task %d; %u free pages; timer %u Hz\n", sh, (u32)mm_free_frames(), HZ);

    __asm__ volatile("sti");
    for (;;) __asm__ volatile("hlt");  /* this is now the idle task */
}
