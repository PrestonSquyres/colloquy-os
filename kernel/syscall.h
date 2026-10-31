#pragma once
#include "arch.h"
#define VEC_SYSCALL 0x80
struct frame *syscall_dispatch(struct frame *f);
