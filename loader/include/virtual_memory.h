#pragma once

#include "common/types.h"
#include "common/align.h"
#include "arch/virtual_memory.h"

struct page_table {
    void *root;
    void (*write_slot)(void*, u64);
    u64 (*read_slot)(void*);
    u64 max_table_address;
    u64 entry_address_mask;
    u8 table_width_shift;
    u8 levels;
    u8 entry_width;
    u8 base_shift;
};

static inline ptr_t pt_get_root(struct page_table *pt)
{
    return (ptr_t)pt->root;
}

static inline size_t page_shift(struct page_table *pt)
{
    return pt->base_shift;
}

static inline size_t huge_page_shift(struct page_table *pt)
{
    return page_shift(pt) + pt->table_width_shift;
}

static inline size_t huge_page_size(struct page_table *pt)
{
    return 1ul << huge_page_shift(pt);
}

static inline size_t page_size(struct page_table *pt)
{
    return 1ul << pt->base_shift;
}

#define HUGE_PAGE_ROUND_UP(pt, size)   ALIGN_UP(size, huge_page_size(pt))
#define HUGE_PAGE_ROUND_DOWN(pt, size) ALIGN_DOWN(size, huge_page_size(pt))

void page_table_init(struct page_table *pt, enum pt_type type,
                     u64 max_table_address);

/*
 * Amount of virtual memory covered by a level in a page table.
 * E.g. 1GiB for an i686 PAE PDPT entry (lvl_idx=3 or pt->levels - 1).
 */
u64 pt_level_entry_virtual_coverage(struct page_table *pt, size_t lvl_idx);

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
