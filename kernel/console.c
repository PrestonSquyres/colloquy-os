/* Console: COM1 serial (primary, interactive) mirrored to VGA text mode. */
#include "console.h"
#include "lib.h"
#include <stdarg.h>

#define COM1 0x3F8
static volatile u16 *const vga = (u16 *)0xB8000;
static int vx, vy;
static const u8 vattr = 0x0B; /* light cyan on black */

void console_init(void) {
    outb(COM1 + 1, 0x00); /* no serial interrupts: the shell polls */
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03); /* 38400 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); /* 8N1 */
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
    for (int i = 0; i < 80 * 25; i++) vga[i] = (u16)(vattr << 8 | ' ');
    vx = vy = 0;
}

static void serial_putc(char c) {
    while (!(inb(COM1 + 5) & 0x20)) {}
    outb(COM1, (u8)c);
}

static void vga_putc(char c) {
    if (c == '\n') { vx = 0; vy++; }
    else if (c == '\r') { vx = 0; }
    else if (c == '\b') { if (vx) vx--; vga[vy * 80 + vx] = (u16)(vattr << 8 | ' '); }
    else { vga[vy * 80 + vx] = (u16)(vattr << 8 | (u8)c); if (++vx == 80) { vx = 0; vy++; } }
    if (vy == 25) {
        for (int i = 0; i < 80 * 24; i++) vga[i] = vga[i + 80];
        for (int i = 80 * 24; i < 80 * 25; i++) vga[i] = (u16)(vattr << 8 | ' ');
        vy = 24;
    }
}

void kputc(char c) {
    u64 f = irq_save();
    if (c == '\n') serial_putc('\r');
    serial_putc(c);
    vga_putc(c);
    irq_restore(f);
}
void kputs(const char *s) { while (*s) kputc(*s++); }

int serial_getc(void) {
    if (inb(COM1 + 5) & 1) return inb(COM1);
    return -1;
}

static void put_num(u64 v, int base, int width, char pad, int neg) {
    char buf[24]; int i = 0;
    const char *dig = "0123456789abcdef";
    do { buf[i++] = dig[v % (u64)base]; v /= (u64)base; } while (v);
    if (neg) buf[i++] = '-';
    while (i < width) buf[i++] = pad;
    while (i) kputc(buf[--i]);
}

/* Supports %s %c %d %u %x %p with optional width/zero-pad and l/ll. */
void kprintf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (*fmt != '%') { kputc(*fmt); continue; }
        fmt++;
        char pad = ' '; int width = 0, lng = 0, left = 0;
        if (*fmt == '-') { left = 1; fmt++; }
        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l') { lng++; fmt++; }
        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *); if (!s) s = "(null)";
            int n = (int)strlen(s);
            if (!left) for (int i = n; i < width; i++) kputc(' ');
            kputs(s);
            if (left) for (int i = n; i < width; i++) kputc(' ');
            break; }
        case 'c': kputc((char)va_arg(ap, int)); break;
        case 'd': { i64 v = lng ? va_arg(ap, i64) : va_arg(ap, int);
                    put_num((u64)(v < 0 ? -v : v), 10, width, pad, v < 0); break; }
        case 'u': put_num(lng ? va_arg(ap, u64) : va_arg(ap, u32), 10, width, pad, 0); break;
        case 'x': put_num(lng ? va_arg(ap, u64) : va_arg(ap, u32), 16, width, pad, 0); break;
        case 'p': kputs("0x"); put_num((u64)va_arg(ap, void *), 16, 16, '0', 0); break;
        case '%': kputc('%'); break;
        default: kputc('%'); kputc(*fmt); break;
        }
    }
    va_end(ap);
}
