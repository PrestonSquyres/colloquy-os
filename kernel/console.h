#pragma once
#include "types.h"
void console_init(void);
void kputc(char c);
void kputs(const char *s);
void kprintf(const char *fmt, ...);
int serial_getc(void); /* -1 if nothing waiting */
