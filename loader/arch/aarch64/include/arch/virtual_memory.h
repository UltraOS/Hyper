#pragma once

#include "common/types.h"

extern u32 g_aarch64_access_flag_mask;

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

#define PAGE_NORMAL (PAGE_AARCH64_TABLE_DESCRIPTOR | \
                     g_aarch64_access_flag_mask)
#define PAGE_HUGE (PAGE_AARCH64_BLOCK_OR_PAGE_DESCRIPTOR | \
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
