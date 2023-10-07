#pragma once

#include "common/attributes.h"

void arch_put_byte(char c);

// Optional, arch_put_byte is called per char if not implemented
void arch_write_string(const char *str, size_t count);

// *MUST* shutdown to make tests pass, allowed to hang if on real HW
NORETURN
void arch_hang_or_shutdown(void);
