/* hog: a pure CPU burner in ring 3. Its budget, not its greed, decides its share. */
#include "colloquy.h"
volatile u64 counter;
int main(void) { for (;;) counter++; }
