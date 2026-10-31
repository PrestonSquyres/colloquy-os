/* GDT with ring-3 segments and a TSS, so interrupts from user mode land on
 * the running task's own kernel stack. */
#include "types.h"
#include "gdt.h"

struct tss {
    u32 rsv0; u64 rsp0, rsp1, rsp2; u64 rsv1; u64 ist[7]; u64 rsv2; u16 rsv3; u16 iomap;
} __attribute__((packed));

static struct tss tss;
static u64 gdt[7];
struct gdt_ptr { u16 limit; u64 base; } __attribute__((packed));

void gdt_init(void) {
    gdt[0] = 0;
    gdt[1] = 0x00AF9A000000FFFFULL;  /* 0x08 kernel code */
    gdt[2] = 0x00CF92000000FFFFULL;  /* 0x10 kernel data */
    gdt[3] = 0x00CFF2000000FFFFULL;  /* 0x18 user data (DPL 3) */
    gdt[4] = 0x00AFFA000000FFFFULL;  /* 0x20 user code (DPL 3, 64-bit) */
    u64 base = (u64)&tss, limit = sizeof tss - 1;
    tss.iomap = sizeof tss;          /* no I/O bitmap: ring 3 gets no port access */
    gdt[5] = (limit & 0xFFFF) | ((base & 0xFFFFFF) << 16) | (0x89ULL << 40) |
             (((limit >> 16) & 0xF) << 48) | (((base >> 24) & 0xFF) << 56);
    gdt[6] = base >> 32;
    struct gdt_ptr p = { sizeof gdt - 1, (u64)gdt };
    __asm__ volatile(
        "lgdt %0\n"
        "pushq $0x08\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1: mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n mov %%ax, %%es\n mov %%ax, %%ss\n mov %%ax, %%fs\n mov %%ax, %%gs\n"
        "mov $0x28, %%ax\n ltr %%ax\n"
        :: "m"(p) : "rax", "memory");
}

void tss_set_kernel_stack(u64 top) { tss.rsp0 = top; }
