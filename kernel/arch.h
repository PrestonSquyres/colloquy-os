#pragma once
#include "types.h"
struct frame {
    u64 r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax;
    u64 vec, err;
    u64 rip, cs, rflags, rsp, ss;
};
#define VEC_TIMER 32
#define VEC_YIELD 0x81
#define HZ 100
void arch_init(void);
extern volatile u64 ticks;
static inline void yield(void) { __asm__ volatile("int $0x81" ::: "memory"); }
