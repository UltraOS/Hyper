#pragma once

#include "attributes.h"

NORETURN
PRINTF_DECL(1, 2)
void panic(const char *reason, ...);

NORETURN
PRINTF_DECL(1, 2)
void oops(const char *reason, ...);
