#pragma once
#include "types.h"
#include "arch.h"
#include "abi.h"

#define MAX_TASKS 16
#define MAX_SEATS 16
#define EPOCH_TICKS HZ            /* budgets refill once per second */
#define BUDGET_UNLIMITED 0xFFFFFFFFu

enum task_state { T_FREE, T_READY, T_SLEEP, T_WAIT, T_DEAD };

/* A seat is a capability: the right to listen/speak/invite in one transcript. */
struct seat { int tid; u8 rights; };

struct task {
    int id; enum task_state state; char name[16];
    struct frame *ctx; u8 *kstack; u64 kstack_top;
    u64 cr3;                         /* address space (kernel tasks: boot tables) */
    int user;                        /* 1 = runs in ring 3 */
    int inbox;                       /* transcript id of this task's inbox, or -1 */
    u64 first_frame;                 /* physical page of the program's first segment */
    u64 wake_tick; int wait_tid;
    u32 budget, remaining;           /* ticks per epoch, ticks left this epoch */
    u64 cpu_ticks;                   /* lifetime ticks charged */
    u32 epoch_used, last_epoch_used;
    u32 starved_epochs;              /* epochs in which the budget ran out */
    char death[80];                  /* why it died, if it did */
    struct seat seats[MAX_SEATS];
};

void sched_init(void);
struct task *task_new(const char *name, u32 budget);     /* allocates a slot (state T_FREE until started) */
void task_start(struct task *t);
int  task_spawn_kernel(const char *name, void (*entry)(void), u32 budget);
void task_kill(struct task *t, const char *why);
struct task *task_current(void);
struct task *task_get(int id);
const char *sched_current_name(void);
void task_sleep(u64 nticks);
void task_wait_transcript(int tid);   /* call with interrupts disabled */
void sched_wake_transcript(int tid);
struct frame *sched_tick(struct frame *f);
struct frame *sched_switch(struct frame *f);
