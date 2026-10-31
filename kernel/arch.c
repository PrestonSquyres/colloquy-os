/* IDT, legacy PIC, PIT timer, exception reporting, interrupt dispatch. */
#include "arch.h"
#include "console.h"
#include "lib.h"
#include "sched.h"
#include "syscall.h"
#include "transcript.h"

struct idt_entry { u16 off0, sel; u8 ist, type; u16 off1; u32 off2, zero; } __attribute__((packed));
struct idt_ptr { u16 limit; u64 base; } __attribute__((packed));
static struct idt_entry idt[256];
extern u64 isr_table[256];
volatile u64 ticks;

static const char *exc_names[32] = {
    "divide error","debug","NMI","breakpoint","overflow","bound range","invalid opcode","device n/a",
    "double fault","coproc overrun","invalid TSS","segment not present","stack fault","general protection",
    "page fault","reserved","x87 fp","alignment check","machine check","SIMD fp","virtualization",
    "control protection","reserved","reserved","reserved","reserved","reserved","reserved",
    "hypervisor injection","VMM communication","security","reserved"};

static void pic_init(void) {
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);   /* remap IRQ0-15 to vectors 32-47 */
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, 0xFE); outb(0xA1, 0xFF);   /* only IRQ0 (timer) unmasked */
}

static void pit_init(void) {
    u16 div = (u16)(1193182 / HZ);
    outb(0x43, 0x36);
    outb(0x40, (u8)(div & 0xFF));
    outb(0x40, (u8)(div >> 8));
}

void arch_init(void) {
    for (int i = 0; i < 256; i++) {
        u64 a = isr_table[i];
        idt[i].off0 = (u16)a; idt[i].sel = 0x08; idt[i].ist = 0;
        idt[i].type = 0x8E;            /* present, ring 0, interrupt gate */
        if (i == VEC_SYSCALL) idt[i].type = 0xEE;   /* DPL 3: user programs may call it */
        idt[i].off1 = (u16)(a >> 16); idt[i].off2 = (u32)(a >> 32); idt[i].zero = 0;
    }
    struct idt_ptr p = { sizeof idt - 1, (u64)idt };
    __asm__ volatile("lidt %0" ::"m"(p));
    pic_init();
    pit_init();
}

static struct frame *exception(struct frame *f) {
    u64 cr2; __asm__ volatile("mov %%cr2,%0" : "=r"(cr2));
    if ((f->cs & 3) == 3) {   /* a user program faulted: it dies, the system goes on */
        char why[80];
        if (f->vec == 14) fmt_into(why, sizeof why, "%s at rip=%x touching %x", exc_names[f->vec], f->rip, cr2);
        else fmt_into(why, sizeof why, "%s at rip=%x", exc_names[f->vec], f->rip);
        task_kill(task_current(), why);
        return sched_switch(f);
    }
    kprintf("\n*** CPU EXCEPTION %u (%s) in task '%s'\n", (u32)f->vec, exc_names[f->vec], sched_current_name());
    kprintf("    rip=%p err=%x cr2=%p rsp=%p\n", (void *)f->rip, (u32)f->err, (void *)cr2, (void *)f->rsp);
    kprintf("    system halted.\n");
    for (;;) __asm__ volatile("cli; hlt");
    return f;
}

/* Called from isr_common with the saved frame; returns the frame to resume. */
struct frame *isr_dispatch(struct frame *f) {
    if (f->vec < 32) return exception(f);
    if (f->vec == VEC_SYSCALL) return syscall_dispatch(f);
    if (f->vec == VEC_TIMER) {
        ticks++;
        outb(0x20, 0x20);
        return sched_tick(f);
    }
    if (f->vec == VEC_YIELD) return sched_switch(f);
    if (f->vec >= 32 && f->vec < 48) { if (f->vec >= 40) outb(0xA0, 0x20); outb(0x20, 0x20); }
    return f;
}
