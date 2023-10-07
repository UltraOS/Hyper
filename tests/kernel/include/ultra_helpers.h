#pragma once

#include "ultra_protocol.h"

#define I686_DIRECT_MAP_BASE       0xC0000000
#define AMD64_DIRECT_MAP_BASE      0xFFFF800000000000
#define AMD64_LA57_DIRECT_MAP_BASE 0xFF00000000000000
#define AARCH64_48BIT_DIRECT_MAP_BASE 0xFFFF000000000000
#define AARCH64_52BIT_DIRECT_MAP_BASE 0xFFF0000000000000

struct ultra_attribute_header *find_attr(struct ultra_boot_context *ctx,
                                         uint32_t type);
