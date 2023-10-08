#pragma once

#include "common/attributes.h"
#include "common/types.h"

extern bool g_should_shutdown;

// Optional, should be implemented if needed
void arch_test_ctl_init(struct ultra_boot_context *bctx);

void arch_put_byte(char c);

// Optional, arch_put_byte is called per char if not implemented
void arch_write_string(const char *str, size_t count);

// *MUST* shutdown to make tests pass, allowed to hang if on real HW
NORETURN
void arch_hang_or_shutdown(void);
