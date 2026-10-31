/* Budget scheduler: each task owns a CPU budget (ticks per one-second epoch).
 * Ready tasks with budget left run round-robin; a task that spends its budget
 * waits for the next epoch even if the CPU is otherwise idle. Task 0 (idle)
 * runs only when nobody else can. Switching tasks also switches address
 * space (CR3) and the kernel stack used for interrupts from ring 3 (TSS). */
#include "sched.h"
#include "console.h"
#include "lib.h"
#include "transcript.h"
#include "mm.h"
#include "gdt.h"

#define KSTACK_SIZE 16384
static struct task tasks[MAX_TASKS];
static u8 kstacks[MAX_TASKS][KSTACK_SIZE] __attribute__((aligned(16)));
static int cur;
extern u8 boot_stack_top[];

void sched_init(void) {
    memset(tasks, 0, sizeof tasks);
    for (int i = 0; i < MAX_TASKS; i++) tasks[i].inbox = -1;
    tasks[0].id = 0; tasks[0].state = T_READY;
    strlcpy(tasks[0].name, "idle", 16);
    tasks[0].budget = tasks[0].remaining = BUDGET_UNLIMITED;
    tasks[0].cr3 = kernel_pml4();
    tasks[0].kstack_top = (u64)boot_stack_top;
    cur = 0; /* kmain's boot stack becomes the idle task */
}

struct task *task_current(void) { return &tasks[cur]; }
struct task *task_get(int id) { return (id >= 0 && id < MAX_TASKS && tasks[id].state != T_FREE) ? &tasks[id] : NULL; }
const char *sched_current_name(void) { return tasks[cur].name; }

struct task *task_new(const char *name, u32 budget) {
    u64 fl = irq_save();
    int id = -1;
    for (int i = 1; i < MAX_TASKS && id < 0; i++) if (tasks[i].state == T_FREE) id = i;
    for (int i = 1; i < MAX_TASKS && id < 0; i++) if (tasks[i].state == T_DEAD && i != cur) id = i;
    if (id < 0) { irq_restore(fl); return NULL; }
    struct task *t = &tasks[id];
    if (t->state == T_DEAD) {                    /* recycle a dead slot */
        if (t->user && t->cr3) space_free(t->cr3);
        if (t->inbox >= 0) t_release(t->inbox);
    }
    memset(t, 0, sizeof *t);
    t->id = id; t->state = T_FREE; t->inbox = -1;
    strlcpy(t->name, name, 16);
    t->budget = t->remaining = budget;
    t->kstack = kstacks[id];
    t->kstack_top = (u64)(t->kstack + KSTACK_SIZE);
    t->cr3 = kernel_pml4();
    irq_restore(fl);
    return t;
}

void task_start(struct task *t) {
    u64 fl = irq_save();
    t->state = T_READY;
    irq_restore(fl);
}

static void kernel_trampoline(void (*entry)(void)) {
    entry();
    u64 f = irq_save();
    ledger_note("task %s exited", tasks[cur].name);
    tasks[cur].state = T_DEAD;
    yield();
    irq_restore(f);
    for (;;) {}
}

int task_spawn_kernel(const char *name, void (*entry)(void), u32 budget) {
    struct task *t = task_new(name, budget);
    if (!t) return -1;
    struct frame *f = (struct frame *)(t->kstack_top - 8 - sizeof(struct frame));
    memset(f, 0, sizeof *f);
    f->rip = (u64)kernel_trampoline; f->rdi = (u64)entry;
    f->cs = 0x08; f->ss = 0x10; f->rflags = 0x202;
    f->rsp = t->kstack_top - 8;
    t->ctx = f;
    ledger_note("spawned kernel task %d '%s' budget=%u", t->id, name, budget);
    task_start(t);
    return t->id;
}

void task_kill(struct task *t, const char *why) {
    u64 fl = irq_save();
    strlcpy(t->death, why, sizeof t->death);
    t->state = T_DEAD;
    irq_restore(fl);
    ledger_note("killed task %d '%s': %s", t->id, t->name, why);
}

static int pick_next(void) {
    for (int k = 1; k <= MAX_TASKS; k++) {
        int i = (cur + k) % MAX_TASKS;
        if (i != 0 && tasks[i].state == T_READY && tasks[i].remaining > 0) return i;
    }
    return 0;
}

struct frame *sched_switch(struct frame *f) {
    u64 old_cr3 = tasks[cur].cr3;
    tasks[cur].ctx = f;
    cur = pick_next();
    struct task *n = &tasks[cur];
    tss_set_kernel_stack(n->kstack_top);
    if (n->cr3 != old_cr3) __asm__ volatile("mov %0, %%cr3" ::"r"(n->cr3) : "memory");
    return n->ctx;
}

struct frame *sched_tick(struct frame *f) {
    struct task *c = &tasks[cur];
    c->cpu_ticks++; c->epoch_used++;
    if (cur != 0 && c->remaining > 0 && --c->remaining == 0) c->starved_epochs++;
    if (ticks % EPOCH_TICKS == 0) {
        for (int i = 0; i < MAX_TASKS; i++) {
            tasks[i].remaining = tasks[i].budget;
            tasks[i].last_epoch_used = tasks[i].epoch_used;
            tasks[i].epoch_used = 0;
        }
    }
    for (int i = 0; i < MAX_TASKS; i++)
        if (tasks[i].state == T_SLEEP && ticks >= tasks[i].wake_tick) tasks[i].state = T_READY;
    return sched_switch(f);
}

void task_sleep(u64 nticks) {
    u64 fl = irq_save();
    tasks[cur].wake_tick = ticks + (nticks ? nticks : 1);
    tasks[cur].state = T_SLEEP;
    yield();
    irq_restore(fl);
}

void task_wait_transcript(int tid) {
    tasks[cur].state = T_WAIT; tasks[cur].wait_tid = tid;
    yield();
}

void sched_wake_transcript(int tid) {
    for (int i = 0; i < MAX_TASKS; i++)
        if (tasks[i].state == T_WAIT && tasks[i].wait_tid == tid) tasks[i].state = T_READY;
}
