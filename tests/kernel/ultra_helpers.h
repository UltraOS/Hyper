#pragma once

#include "ultra_protocol.h"

struct ultra_attribute_header *find_attr(struct ultra_boot_context *ctx,
                                         uint32_t type);
