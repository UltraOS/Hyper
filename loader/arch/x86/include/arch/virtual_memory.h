#pragma once

#include "common/types.h"

#define PAGE_PRESENT   (1 << 0)
#define PAGE_READWRITE (1 << 1)
#define PAGE_NORMAL    (0 << 7)
#define PAGE_HUGE      (1 << 7)

enum pt_type {
    PT_TYPE_I386_NO_PAE = 2,
    PT_TYPE_I386_PAE    = 3,
    PT_TYPE_AMD64_4LVL  = 4,
    PT_TYPE_AMD64_5LVL  = 5,
};

static inline size_t pt_depth(enum pt_type pt)
{
    return (size_t)pt;
}

static inline bool pt_is_huge_page(u64 entry)
{
    return (entry & PAGE_HUGE) == PAGE_HUGE;
}
