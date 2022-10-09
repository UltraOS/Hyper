#include "virtual_memory.h"
#include "allocator.h"

#include "common/bug.h"
#include "common/constants.h"
#include "common/align.h"
#include "common/string.h"

#define PAGE_PRESENT   (1 << 0)
#define PAGE_READWRITE (1 << 1)
#define PAGE_HUGE      (1 << 7)

#define ENTRIES_PER_TABLE 512

static u64 *table_at(u64 *table, size_t index)
{
    u64 entry;
    u64 *page;

    BUG_ON(index >= ENTRIES_PER_TABLE);
    entry = table[index];

    if (entry & PAGE_PRESENT) {
        BUG_ON(entry & PAGE_HUGE);
        entry &= ~0xFFF;
        return (u64*)((ptr_t)entry);
    }

    page = allocate_pages(1);
    if (!page)
        return NULL;

    memzero(page, PAGE_SIZE);
    table[index] = (u64)((ptr_t)page) | PAGE_READWRITE | PAGE_PRESENT;
    return page;
}

static bool do_map_page(struct page_table *pt, u64 virtual_base, u64 physical_base, bool huge)
{
    u64 *lvl4, *lvl3, *lvl2, *lvl1;
    size_t lvl5_index = (virtual_base >> 48) & (ENTRIES_PER_TABLE - 1);
    size_t lvl4_index = (virtual_base >> 39) & (ENTRIES_PER_TABLE - 1);
    size_t lvl3_index = (virtual_base >> 30) & (ENTRIES_PER_TABLE - 1);
    size_t lvl2_index = (virtual_base >> 21) & (ENTRIES_PER_TABLE - 1);
    size_t lvl1_index = (virtual_base >> 12) & (ENTRIES_PER_TABLE - 1);

    if (pt->levels == 5) {
        lvl4 = table_at(pt->root, lvl5_index);
        if (!lvl4)
            return false;
    } else {
        BUG_ON(pt->levels != 4);
        lvl4 = pt->root;
    }

    lvl3 = table_at(lvl4, lvl4_index);
    if (!lvl3)
        return false;

    lvl2 = table_at(lvl3, lvl3_index);
    if (!lvl2)
        return false;

    if (huge) {
        lvl2[lvl2_index] = physical_base | PAGE_HUGE | PAGE_READWRITE | PAGE_PRESENT;
        return true;
    }

    lvl1 = table_at(lvl2, lvl2_index);
    if (!lvl1)
        return false;

    lvl1[lvl1_index] = physical_base | PAGE_READWRITE | PAGE_PRESENT;
    return true;
}

bool map_pages(const struct page_mapping_spec* spec)
{
    u32 increment;
    u64 current_virt, current_phys;
    size_t i;
    bool huge = false;

    switch (spec->type) {
    case PAGE_TYPE_NORMAL:
        increment = PAGE_SIZE;
        break;
    case PAGE_TYPE_HUGE:
        increment = HUGE_PAGE_SIZE;
        huge = true;
        break;
    default:
        BUG();
    }

    // verify alignment
    BUG_ON(!IS_ALIGNED(spec->virtual_base, increment));
    BUG_ON(!IS_ALIGNED(spec->physical_base, increment));

    current_virt = spec->virtual_base;
    current_phys = spec->physical_base;

    for (i = 0; i < spec->count; ++i) {
        bool ok;

        ok = do_map_page(spec->pt, current_virt, current_phys, huge);
        if (!ok)
            goto error_out;

        current_virt += increment;
        current_phys += increment;
    }

    return true;

error_out:
    if (!spec->critical)
        return false;

    panic("Out of memory while mapping %zu pages at 0x%016llX to phys x%016llX (huge: %d)\n",
          spec->count, spec->virtual_base, spec->physical_base, huge);
}
