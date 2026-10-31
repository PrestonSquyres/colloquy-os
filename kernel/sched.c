/* Budget scheduler: each task owns a CPU budget (ticks per one-second epoch).
 * Ready tasks with budget left run round-robin; a task that spends its budget
 * waits for the next epoch even if the CPU is otherwise idle. Task 0 (idle)
 * runs only when nobody else can. */
#include "sched.h"
#include "console.h"
#include "lib.h"
#include "transcript.h"

#define STACK_SIZE 16384
static struct task tasks[MAX_TASKS];
static u8 stacks[MAX_TASKS][STACK_SIZE] __attribute__((aligned(16)));
static int cur;

void sched_init(void) {
    memset(tasks, 0, sizeof tasks);
    tasks[0].id = 0; tasks[0].state = T_READY;
    strlcpy(tasks[0].name, "idle", 16);
    tasks[0].budget = tasks[0].remaining = BUDGET_UNLIMITED;
    cur = 0; /* kmain's boot stack becomes the idle task */
}

struct task *task_current(void) { return &tasks[cur]; }
struct task *task_get(int id) { return (id >= 0 && id < MAX_TASKS && tasks[id].state != T_FREE) ? &tasks[id] : NULL; }
const char *sched_current_name(void) { return tasks[cur].name; }

static void task_trampoline(void (*entry)(void)) {
    entry();
    u64 f = irq_save();
    tasks[cur].state = T_DEAD;
    ledger_note("task %s exited", tasks[cur].name);
    yield();
    irq_restore(f);
    for (;;) {}
}

int task_spawn(const char *name, void (*entry)(void), u32 budget) {
    u64 fl = irq_save();
    int id = -1;
    for (int i = 1; i < MAX_TASKS; i++) if (tasks[i].state == T_FREE) { id = i; break; }
    if (id < 0) { irq_restore(fl); return -1; }
    struct task *t = &tasks[id];
    memset(t, 0, sizeof *t);
    t->id = id; strlcpy(t->name, name, 16);
    t->budget = t->remaining = budget;
    t->stack = stacks[id];
    struct frame *f = (struct frame *)(t->stack + STACK_SIZE - 8 - sizeof(struct frame));
    memset(f, 0, sizeof *f);
    f->rip = (u64)task_trampoline; f->rdi = (u64)entry;
    f->cs = 0x08; f->ss = 0x10; f->rflags = 0x202;
    f->rsp = (u64)(t->stack + STACK_SIZE - 8); /* rsp%16==8 at entry, like after a call */
    t->ctx = f;
    t->state = T_READY;
    irq_restore(fl);
    ledger_note("spawned task %d '%s' budget=%u", id, name, budget);
    return id;
}

static int runnable(struct task *t) {
    return t->state == T_READY && t->remaining > 0;
}

static int pick_next(void) {
    for (int k = 1; k <= MAX_TASKS; k++) {
        int i = (cur + k) % MAX_TASKS;
        if (i != 0 && runnable(&tasks[i])) return i;
    }
    return 0;
}

struct frame *sched_switch(struct frame *f) {
    tasks[cur].ctx = f;
    cur = pick_next();
    return tasks[cur].ctx;
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
