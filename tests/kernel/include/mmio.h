#pragma once

#include "common/types.h"
#include "ultra_protocol.h"

enum mmio_type {
    // Strongly-ordered device memory, e.g. UART/MMIO registers
    MMIO_DEVICE,
    // Non-cacheable "normal" memory, e.g. a linear framebuffer
    MMIO_WC,
};

/*
 * Map a physical MMIO range into the current address space and return a pointer
 * to it. The loader only direct-maps RAM, so device memory (framebuffer, UART,
 * ...) must be mapped by the kernel itself.
 *
 * 'type' selects the memory type: MMIO_DEVICE for device registers, MMIO_WC for
 * a linear framebuffer. On aarch64 this picks the MAIR AttrIndx guaranteed by
 * the loader (1 = Device-nGnRnE, 2 = Normal Non-cacheable); on x86 both map as
 * cache-disabled.
 *
 * On 64-bit the range is placed at direct_map_base + phys; on i686 (whose
 * direct map only reaches 1 GiB) it's placed in a window carved just below the
 * direct map base instead.
 */
void *mmio_map(struct ultra_boot_context *bctx, u64 phys, size_t size,
               enum mmio_type type);
