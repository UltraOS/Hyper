#include "common/bug.h"
#include "common/string.h"
#include "common/rw_helpers.h"

#include "virtual_memory.h"
#include "virtual_memory_impl.h"

/*
 * We pretend TTBR0 & 1 are actually entries inside an extra page table level
 * for simplicity to make it more like x86.
 */
static u8 unified_pt_depth(enum pt_type type)
{
    return pt_depth(type) + 1;
}

void page_table_init(struct page_table *pt, enum pt_type type,
                     u64 max_table_address)
{
    ptr_t root_page = pt_get_table_page(max_table_address);
    OOPS_ON(!root_page);

    pt->root = ADDR_TO_PTR(root_page);
    pt->levels = unified_pt_depth(type);
    pt->base_shift = PAGE_SHIFT;
    pt->max_table_address = max_table_address;

    // We currently don't support 52-bit OA, so this is the mask
    pt->entry_address_mask = ~(BIT_MASK(48, 64) | BIT_MASK(0, PAGE_SHIFT));

    pt->entry_width = 8;
    pt->table_width_shift = 9;
    pt->write_slot = write_u64;
    pt->read_slot = read_u64;
}

#define LOOKUP_LEVEL_MINUS_1 4
#define LOOKUP_LEVEL_MINUS_1_WIDTH_SHIFT 4

u8 pt_table_width_shift_for_level(struct page_table *pt, size_t idx)
{
    if (pt->levels == unified_pt_depth(PT_TYPE_AARCH64_4K_GRANULE_52_BIT) &&
        idx == LOOKUP_LEVEL_MINUS_1)
        return LOOKUP_LEVEL_MINUS_1_WIDTH_SHIFT;

    return pt->table_width_shift;
}
