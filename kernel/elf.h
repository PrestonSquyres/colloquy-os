#pragma once
#include "types.h"
#define MAX_MODULES 12
struct module { char name[16]; u64 start, end; u32 budget; int autostart; };
extern struct module modules[MAX_MODULES];
extern int nmodules;
void modules_add(const char *cmdline, u64 start, u64 end);
struct module *module_find(const char *name);
int spawn_program(const char *module_name);   /* -> task id or error */
