#pragma once
#include "types.h"
#include "arch.h"

#define MAX_TASKS 8
#define MAX_SEATS 16
#define EPOCH_TICKS HZ            /* budgets refill once per second */
#define BUDGET_UNLIMITED 0xFFFFFFFFu

enum task_state { T_FREE, T_READY, T_SLEEP, T_WAIT, T_DEAD };

/* A seat is a capability: the right to speak/listen/invite in one transcript. */
#define SEAT_LISTEN 1
#define SEAT_SPEAK  2
#define SEAT_INVITE 4
struct seat { int tid; u8 rights; };

struct task {
    int id; enum task_state state; char name[16];
    struct frame *ctx; u8 *stack;
    u64 wake_tick; int wait_tid;
    u32 budget, remaining;           /* ticks per epoch, ticks left this epoch */
    u64 cpu_ticks;                   /* lifetime ticks charged */
    u32 epoch_used, last_epoch_used; /* this epoch / previous epoch */
    u32 starved_epochs;              /* epochs in which the budget ran out */
    struct seat seats[MAX_SEATS];
};

void sched_init(void);
int task_spawn(const char *name, void (*entry)(void), u32 budget);
struct task *task_current(void);
struct task *task_get(int id);
const char *sched_current_name(void);
void task_sleep(u64 nticks);
void task_wait_transcript(int tid);   /* call with interrupts disabled */
void sched_wake_transcript(int tid);
struct frame *sched_tick(struct frame *f);
struct frame *sched_switch(struct frame *f);
