#pragma once

#include "common/types.h"

struct ultra_boot_context;

void fb_tty_init(struct ultra_boot_context *bctx);
void fb_tty_write(const char *str, size_t count);
