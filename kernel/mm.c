#include "mm.h"
#include "lib.h"
#include "sched.h"

extern u64 pml4[], pd[];          /* boot page tables (boot.asm) */
static u64 next_frame, end_frame, free_list, free_count;

void mm_init(u64 free_start, u64 mem_end) {
    next_frame = (free_start + PAGE - 1) & ~(PAGE - 1);
    if (mem_end > 0x40000000ULL) mem_end = 0x40000000ULL;  /* only the first GiB is identity-mapped */
    end_frame = mem_end & ~(PAGE - 1);
    free_list = 0; free_count = 0;
}

u64 frame_alloc(void) {
    u64 fl = irq_save(), f = 0;
    if (free_list) { f = free_list; free_list = *(u64 *)f; free_count--; }
    else if (next_frame < end_frame) { f = next_frame; next_frame += PAGE; }
    irq_restore(fl);
    if (f) memset((void *)f, 0, PAGE);
    return f;
}

void frame_free(u64 f) {
    if (!f) return;
    u64 fl = irq_save();
    *(u64 *)f = free_list; free_list = f; free_count++;
    irq_restore(fl);
}

u64 mm_free_frames(void) { return free_count + (end_frame - next_frame) / PAGE; }
u64 kernel_pml4(void) { return (u64)pml4; }

#define P 1ULL
#define W 2ULL
#define U 4ULL
#define ADDR(e) ((e) & 0x000FFFFFFFFFF000ULL)

u64 space_new(void) {
    u64 top = frame_alloc(), pdpt = frame_alloc();
    if (!top || !pdpt) { frame_free(top); frame_free(pdpt); return 0; }
    ((u64 *)top)[0] = pdpt | P | W | U;
    ((u64 *)pdpt)[0] = (u64)pd | P | W;   /* kernel GiB: no U bit, so ring 3 cannot touch it */
    return top;
}

static u64 *walk(u64 top, u64 va, int create) {
    if (va < USER_BASE || va >= USER_TOP) return NULL;
    u64 *pdpt = (u64 *)ADDR(((u64 *)top)[0]);
    u64 *e = &pdpt[(va >> 30) & 511];
    for (int level = 0; level < 2; level++) {
        if (!(*e & P)) {
            if (!create) return NULL;
            u64 t = frame_alloc(); if (!t) return NULL;
            *e = t | P | W | U;
        }
        u64 *tbl = (u64 *)ADDR(*e);
        e = &tbl[(va >> (level == 0 ? 21 : 12)) & 511];
    }
    return e;
}

int space_map(u64 top, u64 va, u64 frame) {
    u64 *e = walk(top, va, 1);
    if (!e) return -1;
    *e = frame | P | W | U;
    return 0;
}

u64 space_phys(u64 top, u64 va) {
    u64 *e = walk(top, va & ~(PAGE - 1), 0);
    if (!e || !(*e & P) || !(*e & U)) return 0;
    return ADDR(*e) | (va & (PAGE - 1));
}

int space_copy_in(u64 top, u64 va, const void *src, u64 len) {
    const u8 *s = src;
    while (len) {
        u64 ph = space_phys(top, va); if (!ph) return -1;
        u64 n = PAGE - (va & (PAGE - 1)); if (n > len) n = len;
        memcpy((void *)ph, s, n);
        va += n; s += n; len -= n;
    }
    return 0;
}

void space_free(u64 top) {
    if (!top) return;
    u64 *pdpt = (u64 *)ADDR(((u64 *)top)[0]);
    for (int i = 1; i < 512; i++) {               /* slot 0 is the shared kernel GiB */
        if (!(pdpt[i] & P)) continue;
        u64 *pdir = (u64 *)ADDR(pdpt[i]);
        for (int j = 0; j < 512; j++) {
            if (!(pdir[j] & P)) continue;
            u64 *pt = (u64 *)ADDR(pdir[j]);
            for (int k = 0; k < 512; k++) if (pt[k] & P) frame_free(ADDR(pt[k]));
            frame_free((u64)pt);
        }
        frame_free((u64)pdir);
    }
    frame_free((u64)pdpt);
    frame_free(top);
}

int user_ok(u64 va, u64 len) {
    struct task *t = task_current();
    if (!t->cr3 || t->cr3 == kernel_pml4()) return 0;
    if (len == 0) return 1;
    if (va < USER_BASE || va + len > USER_TOP || va + len < va) return 0;
    for (u64 p = va & ~(PAGE - 1); p < va + len; p += PAGE)
        if (!space_phys(t->cr3, p)) return 0;
    return 1;
}

int copy_from_user(void *dst, u64 src, u64 len) {
    if (!user_ok(src, len)) return E_FAULT;
    memcpy(dst, (const void *)src, len);   /* the caller's space is live in CR3 */
    return 0;
}

int copy_to_user(u64 dst, const void *src, u64 len) {
    if (!user_ok(dst, len)) return E_FAULT;
    memcpy((void *)dst, src, len);
    return 0;
}

int copy_str_from_user(char *dst, u64 src, u64 max) {
    for (u64 i = 0; i < max; i++) {
        if (!user_ok(src + i, 1)) return E_FAULT;
        dst[i] = *(const char *)(src + i);
        if (!dst[i]) return 0;
    }
    return E_BADARG;   /* too long */
}
