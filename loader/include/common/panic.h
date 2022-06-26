#pragma once

#include "attributes.h"

NORETURN
PRINTF_DECL(1, 2)
void panic(const char *reason, ...);

NORETURN
PRINTF_DECL(1, 2)
void oops(const char *reason, ...);

#define OOPS_ON(expr)                            \
    do {                                         \
        if (unlikely(expr))                      \
            oops(#expr " evaluated to true\n");  \
    } while (0)
