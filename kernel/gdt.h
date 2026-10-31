#pragma once
#include "types.h"
#define USER_CS 0x23
#define USER_SS 0x1B
void gdt_init(void);
void tss_set_kernel_stack(u64 top);
