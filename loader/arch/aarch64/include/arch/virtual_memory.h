#pragma once

#include "common/types.h"

extern u32 g_aarch64_access_flag_mask;
extern u32 g_aarch64_sh_mask;

#define PAGE_PRESENT   (1ull << 0)

/*
 * This is supposed to be an index into the APTable, but it's located at
 * different offsets depending on whether this is a table or a block
 * descriptor. We currently don't have such abstraction, so just hardcode
 * this to zero.
*/
#define PAGE_READWRITE (0)

#define PAGE_AARCH64_BLOCK_OR_PAGE_DESCRIPTOR (0ull << 1)
#define PAGE_AARCH64_TABLE_DESCRIPTOR (1ull << 1)
#define PAGE_AARCH64_ACCESS_FLAG (1ull << 10)

/*
 * Inner Shareable, so that the Write-Back cacheable RAM mappings stay coherent
 * across cores. Only meaningful in block/page (leaf) descriptors; the SH field
 * is ignored in the intermediate table descriptors that also carry PAGE_NORMAL.
 * AttrIndx stays 0, which the loader programs to Normal Write-Back in MAIR.
 *
 * Applied through g_aarch64_sh_mask because it is only valid for the 48-bit
 * descriptor format: with TCR.DS set (the 52-bit format), descriptor bits
 * [9:8] hold OA[51:50] instead, and shareability comes from TCR.SH{0,1}.
 * page_table_init() picks the right mask for the page table being built.
 */
#define PAGE_AARCH64_SH_INNER (0b11ull << 8)

#define PAGE_NORMAL (PAGE_AARCH64_TABLE_DESCRIPTOR | \
                     g_aarch64_sh_mask | \
                     g_aarch64_access_flag_mask)
#define PAGE_HUGE (PAGE_AARCH64_BLOCK_OR_PAGE_DESCRIPTOR | \
                   g_aarch64_sh_mask | \
                   g_aarch64_access_flag_mask)

enum pt_type {
    PT_TYPE_AARCH64_4K_GRANULE_48_BIT = 4,
    PT_TYPE_AARCH64_4K_GRANULE_52_BIT = 5,
};

static inline size_t pt_depth(enum pt_type pt)
{
    return (size_t)pt;
}

static inline bool pt_is_huge_page(u64 entry)
{
    return (entry & PAGE_AARCH64_TABLE_DESCRIPTOR) == 0;
}
