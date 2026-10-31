#pragma once
#include "types.h"
/* Physical frames and per-program address spaces.
 * Every space maps the kernel's first GiB (identity, supervisor-only) at
 * PDPT slot 0 and the program's own memory in USER_BASE..USER_TOP. */
#define PAGE 4096ULL
#define USER_BASE 0x40000000ULL
#define USER_TOP  0x80000000ULL
#define USER_STACK_TOP  0x7FFF0000ULL
#define USER_STACK_PAGES 8

void mm_init(u64 free_start, u64 mem_end);
u64  frame_alloc(void);           /* zeroed 4 KiB frame (physical == kernel virtual) */
void frame_free(u64 f);
u64  mm_free_frames(void);
u64  space_new(void);             /* returns PML4 physical address */
void space_free(u64 pml4);
int  space_map(u64 pml4, u64 va, u64 frame);
u64  space_phys(u64 pml4, u64 va);  /* 0 if va is not a mapped user page */
int  space_copy_in(u64 pml4, u64 va, const void *src, u64 len);  /* kernel -> user page */
u64  kernel_pml4(void);

/* Checks against the *current* task's space (used by system calls). */
int  user_ok(u64 va, u64 len);
int  copy_from_user(void *dst, u64 src, u64 len);
int  copy_to_user(u64 dst, const void *src, u64 len);
int  copy_str_from_user(char *dst, u64 src, u64 max);
