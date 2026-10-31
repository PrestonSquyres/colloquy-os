#pragma once
#include "types.h"
extern int shell_id, clock_id, scribe_id, hog_id;
extern volatile u64 hog_counter;
void clock_main(void);
void scribe_main(void);
void hog_main(void);
void shell_main(void);
