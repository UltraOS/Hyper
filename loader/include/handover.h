#pragma once

#include "common/types.h"
#include "common/attributes.h"
#include "common/constants.h"
#include "common/bug.h"
#include "arch/handover_flags.h"

/*
 * Generic handover info structure:
 * entrypoint -> address of the kernel binary entry, possibly higher half
 * stack -> address of the top of the kernel stack, possibly higher half
 * pt_root -> physical address of the root page table page
 * arg0, arg1 -> arguments to pass to the kernel binary entrypoint
 * direct_map_base -> base address in the higher half that direct maps at least
 *                    'handover_get_minimum_map_length()' amount of
 *                    physical memory.
 * flags -> flags that describe the expected system state before 'entrypoint'
 *          is invoked, some are arch-specific.
 *
 * Page table is expected to contain at least two mappings, where both linearly
 * map physical ram from address zero:
 * 0x0000...0000 -> handover_get_minimum_map_length()
 * AND
 * direct_map_base -> handover_get_minimum_map_length()
 */
struct handover_info {
    u64 entrypoint;
    u64 stack;
    u64 pt_root;
    u64 arg0, arg1;

    u64 direct_map_base;

/*
 * If set, unmaps the first table or handover_get_minimum_map_length()
 * worth of pages from the page table root, whichever one is bigger.
 */
#define HO_HIGHER_HALF_ONLY_BIT 0
#define HO_HIGHER_HALF_ONLY (1 << HO_HIGHER_HALF_ONLY_BIT)

/*
 * Arch-specific flags are described in <arch>/include/handover_flags.h
 */
    u32 flags;
};

u64 handover_get_minimum_map_length(u64 direct_map_base, u32 flags);
u64 handover_get_max_pt_address(u64 direct_map_base, u32 flags);

/*
 * Must be executed before calling 'kernel_handover', expects at least
 * the memory services to still be online
 */
void handover_prepare_for(struct handover_info *hi);

NORETURN
void kernel_handover(struct handover_info*);

bool handover_is_flag_supported(u32 flag);
void handover_ensure_supported_flags(u32 flags);
