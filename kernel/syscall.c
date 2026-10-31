/* The system call gate (int 0x80, callable from ring 3). Every pointer is
 * checked against the caller's own address space before use. */
#include "syscall.h"
#include "abi.h"
#include "mm.h"
#include "sched.h"
#include "transcript.h"
#include "lib.h"

struct frame *syscall_dispatch(struct frame *f) {
    struct task *me = task_current();
    u64 a = f->rdi, b = f->rsi, c = f->rdx, d = f->r10;
    i64 r;
    switch (f->rax) {
    case SYS_EXIT:
        ledger_note("task %d '%s' exited with code %d", me->id, me->name, (int)a);
        strlcpy(me->death, "exited", sizeof me->death);
        me->state = T_DEAD;
        return sched_switch(f);
    case SYS_OPEN: {
        char name[TNAME];
        r = copy_str_from_user(name, a, TNAME);
        if (r == 0) r = t_open(name);
        break; }
    case SYS_INVITE:
        r = t_invite((int)a, (int)b, (u8)c);
        break;
    case SYS_SPEAK: {
        char text[TURN_TEXT];
        if (c > TURN_TEXT) c = TURN_TEXT;
        r = copy_from_user(text, b, c);
        if (r == 0) r = t_speak((int)a, text, (u32)c);
        else ledger_note("REFUSED speak by task %d: text pointer %x is outside its memory", me->id, b);
        break; }
    case SYS_LISTEN: {
        u32 cursor; struct turn t;
        if (!user_ok(b, sizeof cursor) || !user_ok(c, sizeof t)) { r = E_FAULT; break; }
        copy_from_user(&cursor, b, sizeof cursor);
        r = t_listen((int)a, &cursor, &t, (int)d);   /* may block; our CR3 is restored on wake */
        if (r == 0) { copy_to_user(c, &t, sizeof t); }
        copy_to_user(b, &cursor, sizeof cursor);
        break; }
    case SYS_SLEEP:
        task_sleep(a > 100000 ? 100000 : a);
        r = 0;
        break;
    case SYS_SEAT_FIND: {
        char name[TNAME];
        r = copy_str_from_user(name, a, TNAME);
        if (r == 0) r = t_seat_find(name, (u8)b);
        break; }
    case SYS_SEAT_INFO: {
        struct seat_info si;
        r = t_seat_info((int)a, &si);
        if (r == 0) r = copy_to_user(b, &si, sizeof si);
        break; }
    case SYS_SELF:
        r = me->id;
        break;
    default:
        ledger_note("REFUSED unknown system call %d from task %d '%s'", (int)f->rax, me->id, me->name);
        r = E_NOSYS;
    }
    f->rax = (u64)r;
    return f;
}
