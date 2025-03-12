#include "common/bug.h"
#include "common/string.h"
#include "common/rw_helpers.h"

#include "virtual_memory.h"
#include "virtual_memory_impl.h"

void page_table_init(struct page_table *pt, enum pt_type type,
                     u64 max_table_address)
{
    ptr_t root_page = pt_get_table_page(max_table_address);
    OOPS_ON(!root_page);

    pt->root = ADDR_TO_PTR(root_page);
    pt->levels = pt_depth(type);
    pt->base_shift = PAGE_SHIFT;
    pt->max_table_address = max_table_address;

    // 52 is the maximum supported number of physical bits
    pt->entry_address_mask = ~(BIT_MASK(52, 64) | BIT_MASK(0, PAGE_SHIFT));

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

    /*
     * 32-bit PAE paging is a bit strange in that the root table consists of
     * only four pointers, which have really strange semantics:
     *
     * 1. On intel, they're cached in shadow registers as soon as CR3 is loaded
     *    with a new table. What this means is, modifications to the root table
     *    won't be picked up until a full CR3 flush occurs.
     * 2. The WRITE bit for the root table entries is reserved, only the
     *    PRESENT bit must be set.
     *
     * The semantics above make it really annoying to deal with lazy allocation
     * of the PAE tables, so let's pre-populate all root table slots right away.
     */
    if (type == PT_TYPE_I386_PAE) {
        size_t i;
        ptr_t entry;
        void *table = pt->root;

        for (i = 0; i < 4; ++i) {
            entry = pt_get_table_page(pt->max_table_address);
            OOPS_ON(!entry);

            pt->write_slot(table, entry | PAGE_PRESENT);
            table += pt->entry_width;
        }
    }
}
