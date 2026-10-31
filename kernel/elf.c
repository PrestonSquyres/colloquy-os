/* Boot modules and the ELF64 loader. Each program gets a fresh address
 * space, a stack, an inbox, and nothing else. */
#include "elf.h"
#include "mm.h"
#include "sched.h"
#include "transcript.h"
#include "gdt.h"
#include "lib.h"

struct module modules[MAX_MODULES];
int nmodules;

/* cmdline: "<name> [budget] [manual]" */
void modules_add(const char *cmdline, u64 start, u64 end) {
    if (nmodules >= MAX_MODULES) return;
    struct module *m = &modules[nmodules++];
    int i = 0;
    while (*cmdline == ' ') cmdline++;
    while (*cmdline && *cmdline != ' ' && i < 15) m->name[i++] = *cmdline++;
    m->name[i] = 0;
    m->start = start; m->end = end; m->budget = 10; m->autostart = 1;
    while (*cmdline) {
        while (*cmdline == ' ') cmdline++;
        if (*cmdline >= '0' && *cmdline <= '9') {
            u32 v = 0; while (*cmdline >= '0' && *cmdline <= '9') v = v * 10 + (u32)(*cmdline++ - '0');
            if (v >= 1 && v <= 100) m->budget = v;
        } else if (!strncmp(cmdline, "manual", 6)) { m->autostart = 0; cmdline += 6; }
        else if (*cmdline) cmdline++;
    }
}

struct module *module_find(const char *name) {
    for (int i = 0; i < nmodules; i++) if (!strcmp(modules[i].name, name)) return &modules[i];
    return NULL;
}

struct ehdr { u8 ident[16]; u16 type, machine; u32 version; u64 entry, phoff, shoff; u32 flags;
              u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx; } __attribute__((packed));
struct phdr { u32 type, flags; u64 offset, vaddr, paddr, filesz, memsz, align; } __attribute__((packed));

static int load(struct task *t, struct module *m, u64 *entry) {
    const u8 *img = (const u8 *)m->start; u64 size = m->end - m->start;
    const struct ehdr *e = (const struct ehdr *)img;
    if (size < sizeof *e || memcmp(e->ident, "\x7f" "ELF", 4) || e->ident[4] != 2 ||
        e->machine != 0x3E || e->type != 2 || e->phoff + (u64)e->phnum * sizeof(struct phdr) > size) return -1;
    for (int i = 0; i < e->phnum; i++) {
        const struct phdr *p = (const struct phdr *)(img + e->phoff + (u64)i * e->phentsize);
        if (p->type != 1) continue;                     /* PT_LOAD */
        if (p->vaddr < USER_BASE || p->vaddr + p->memsz > USER_STACK_TOP - USER_STACK_PAGES * PAGE ||
            p->filesz > p->memsz || p->offset + p->filesz > size) return -1;
        for (u64 va = p->vaddr & ~(PAGE - 1); va < p->vaddr + p->memsz; va += PAGE) {
            if (space_phys(t->cr3, va)) continue;
            u64 fr = frame_alloc(); if (!fr || space_map(t->cr3, va, fr)) return -1;
            if (!t->first_frame) t->first_frame = fr;
        }
        if (space_copy_in(t->cr3, p->vaddr, img + p->offset, p->filesz)) return -1;
    }
    for (u64 i = 1; i <= USER_STACK_PAGES; i++) {
        u64 fr = frame_alloc(); if (!fr || space_map(t->cr3, USER_STACK_TOP - i * PAGE, fr)) return -1;
    }
    *entry = e->entry;
    return 0;
}

int spawn_program(const char *name) {
    struct module *m = module_find(name);
    if (!m) return E_BADARG;
    struct task *t = task_new(name, m->budget);
    if (!t) return E_NOSPACE;
    t->user = 1;
    t->cr3 = space_new();
    u64 entry = 0;
    if (!t->cr3 || load(t, m, &entry)) {
        if (t->cr3) space_free(t->cr3);
        t->cr3 = 0; t->state = T_DEAD; strlcpy(t->death, "load failed", sizeof t->death);
        ledger_note("failed to load program '%s'", name);
        return E_BADARG;
    }
    t_create_inbox(t);
    struct frame *f = (struct frame *)(t->kstack_top - sizeof(struct frame));
    memset(f, 0, sizeof *f);
    f->rip = entry; f->cs = USER_CS; f->ss = USER_SS; f->rflags = 0x202; f->rsp = USER_STACK_TOP;
    t->ctx = f;
    ledger_note("spawned program %d '%s' in ring 3, budget=%u, seats: its inbox only", t->id, name, m->budget);
    task_start(t);
    return t->id;
}
