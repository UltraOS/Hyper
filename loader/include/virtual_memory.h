#pragma once

#include "common/types.h"

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

struct page_table {
    void *root;
    u8 table_width_shift;
    u8 levels;
    u8 entry_width;
    u8 base_shift;
};

void page_table_init(struct page_table *pt, enum pt_type type, void *root_page);

enum page_type {
    // 4K pages
    PAGE_TYPE_NORMAL = 0,

    // 2/4M pages
    PAGE_TYPE_HUGE = 1,
};

struct page_mapping_spec {
    struct page_table *pt;

    u64 virtual_base;
    u64 physical_base;

    size_t count;
    enum page_type type;
    bool critical;
};

bool map_pages(const struct page_mapping_spec*);

// Copy a root table entry at src to dest table entry
void map_copy_root_entry(struct page_table*, u64 src_virtual_address,
                         u64 dest_virtual_address);
