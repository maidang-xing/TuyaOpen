#include <stdio.h>

#include "asm/cpu.h"

void __assert_func(const char *file, int line, const char *function, const char *expression)
{
    printf("assertion failed: %s:%d (%s): %s\n", file ? file : "?", line,
           function ? function : "?", expression ? expression : "?");
    system_reset();
    for (;;) {
    }
}

int system(const char *command)
{
    (void)command;
    return -1;
}

unsigned int random32(int seed)
{
    (void)seed;
    return (unsigned int)rand32();
}
