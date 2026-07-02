#pragma once

#include "common/types.h"
#include "ultra_protocol.h"

/*
 * Map a physical MMIO range into the current address space and return a pointer
 * to it. The loader only direct-maps RAM, so device memory (framebuffer, UART,
 * ...) must be mapped by the kernel itself.
 *
 * On 64-bit the range is placed at direct_map_base + phys; on i686 (whose
 * direct map only reaches 1 GiB) it's placed in a window carved just below the
 * direct map base instead.
 */
void *mmio_map(struct ultra_boot_context *bctx, u64 phys, size_t size);
