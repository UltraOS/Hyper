#include "common/bug.h"
#include "common/string.h"
#include "common/rw_helpers.h"

#include "virtual_memory.h"
#include "virtual_memory_impl.h"

void page_table_init(struct page_table *pt, enum pt_type type,
                     u64 max_table_address)
{
    pt->root = pt_get_table_page(max_table_address);
    OOPS_ON(!pt->root);

    pt->levels = pt_depth(type);
    pt->base_shift = PAGE_SHIFT;
    pt->max_table_address = max_table_address;

    // 52 is the maximum supported number of physical bits
    pt->entry_address_mask = ~(BIT_MASK(52, 64) | BIT_MASK(0, PAGE_SHIFT));

    memzero(pt->root, PAGE_SIZE);

    switch (type) {
    case PT_TYPE_I386_NO_PAE:
        pt->entry_width = 4;
        pt->table_width_shift = 10;
        break;

    case PT_TYPE_I386_PAE:
    case PT_TYPE_AMD64_4LVL:
    case PT_TYPE_AMD64_5LVL:
        pt->entry_width = 8;
        pt->table_width_shift = 9;
        break;

    default:
        BUG();
    }

    if (pt->entry_width == 8) {
        pt->write_slot = write_u64;
        pt->read_slot = read_u64;
    } else {
        pt->write_slot = write_u32_u64;
        pt->read_slot = read_u32_zero_extend;
    }
}
