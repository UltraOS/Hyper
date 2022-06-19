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

    // verify alignment
    BUG_ON((virtual_base & (huge ? (HUGE_PAGE_SIZE - 1) : (PAGE_SIZE - 1))) != 0);
    BUG_ON((physical_base & (huge ? (HUGE_PAGE_SIZE - 1) : (PAGE_SIZE - 1))) != 0);

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

bool map_page(struct page_table *pt, u64 virtual_base, u64 physical_base)
{
    return do_map_page(pt, virtual_base, physical_base, false);
}

bool map_pages(struct page_table *pt, u64 virtual_base, u64 physical_base, size_t pages)
{
    size_t i;

    for (i = 0; i < pages; ++i) {
        if (!do_map_page(pt, virtual_base, physical_base, false))
            return false;

        virtual_base  += PAGE_SIZE;
        physical_base += PAGE_SIZE;
    }

    return true;
}

bool map_huge_page(struct page_table *pt, u64 virtual_base, u64 physical_base)
{
    return do_map_page(pt, virtual_base, physical_base, true);
}

bool map_huge_pages(struct page_table *pt, u64 virtual_base, u64 physical_base, size_t pages)
{
    size_t i;

    for (i = 0; i < pages; ++i) {
        if (!do_map_page(pt, virtual_base, physical_base, true))
            return false;

        virtual_base  += HUGE_PAGE_SIZE;
        physical_base += HUGE_PAGE_SIZE;
    }

    return true;
}

NORETURN
static void on_critical_mapping_failed(u64 virtual_base, u64 physical_base, size_t pages, bool huge)
{
    panic("Out of memory while attempting to map %zu critical pages at 0x%llX (physical 0x%llX) huge: %d\n",
          pages, virtual_base, physical_base, huge);
}

void map_critical_page(struct page_table *pt, u64 virtual_base, u64 physical_base)
{
    if (!map_page(pt, virtual_base, physical_base))
        on_critical_mapping_failed(virtual_base, physical_base, 1, false);
}

void map_critical_pages(struct page_table *pt, u64 virtual_base, u64 physical_base, size_t pages)
{
    if (!map_pages(pt, virtual_base, physical_base, pages))
        on_critical_mapping_failed(virtual_base, physical_base, pages, false);
}

void map_critical_huge_page(struct page_table *pt, u64 virtual_base, u64 physical_base)
{
    if (!map_huge_page(pt, virtual_base, physical_base))
        on_critical_mapping_failed(virtual_base, physical_base, 1, true);
}

void map_critical_huge_pages(struct page_table *pt, u64 virtual_base, u64 physical_base, size_t pages)
{
    if (!map_huge_pages(pt, virtual_base, physical_base, pages))
        on_critical_mapping_failed(virtual_base, physical_base, pages, true);
}
