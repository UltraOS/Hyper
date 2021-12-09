#include "VirtualMemory.h"
#include "Allocator.h"

namespace vm {

#define PAGE_PRESENT   (1 << 0)
#define PAGE_READWRITE (1 << 1)
#define PAGE_HUGE      (1 << 7)

#define ENTRIES_PER_TABLE 512

static u64* table_at(u64* table, size_t index)
{
    ASSERT(index < ENTRIES_PER_TABLE);

    u64 entry = table[index];

    if (entry & PAGE_PRESENT) {
        ASSERT(!(entry & PAGE_HUGE));
        entry &= ~0xFFF;
        return reinterpret_cast<u64*>(entry);
    }

    u64* page = reinterpret_cast<u64*>(allocator::allocate_pages(1));
    if (!page)
        return nullptr;

    table[index] = reinterpret_cast<u64>(page) | PAGE_READWRITE | PAGE_PRESENT;

    return page;
}

static bool do_map_page(PageTable* pt, u64 virtual_base, u64 physical_base, bool huge)
{
   u64* lvl4;
   u64* lvl3;
   u64* lvl2;
   u64* lvl1;

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
       ASSERT(pt->levels == 4);
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

bool map_page(PageTable* pt, u64 virtual_base, u64 physical_base)
{
    return do_map_page(pt, virtual_base, physical_base, false);
}

bool map_pages(PageTable* pt, u64 virtual_base, u64 physical_base, size_t pages)
{
    for (size_t i = 0; i < pages; ++i) {
        if (!do_map_page(pt, virtual_base, physical_base, false))
            return false;

        virtual_base += page_size;
        virtual_base += page_size;
    }

    return true;
}

bool map_huge_page(PageTable* pt, u64 virtual_base, u64 physical_base)
{
    return do_map_page(pt, virtual_base, physical_base, true);
}

bool map_huge_pages(PageTable* pt, u64 virtual_base, u64 physical_base, size_t pages)
{
    for (size_t i = 0; i < pages; ++i) {
        if (!do_map_page(pt, virtual_base, physical_base, true))
            return false;

        virtual_base += huge_page_size;
        virtual_base += huge_page_size;
    }

    return true;
}

[[noreturn]] static void on_critical_mapping_failed(u64 virtual_base, u64 physical_base, size_t pages, bool huge)
{
    unrecoverable_error("out of memory while attempting to map {} critical pages at {x} (physical {x}) huge: {}",
                        pages, virtual_base, physical_base, huge);
}

void map_critical_page(PageTable* pt, u64 virtual_base, u64 physical_base)
{
    if (!map_page(pt, virtual_base, physical_base))
        on_critical_mapping_failed(virtual_base, physical_base, 1, false);
}

void map_critical_pages(PageTable* pt, u64 virtual_base, u64 physical_base, size_t pages)
{
    if (!map_pages(pt, virtual_base, physical_base, pages))
        on_critical_mapping_failed(virtual_base, physical_base, pages, false);
}

void map_critical_huge_page(PageTable* pt, u64 virtual_base, u64 physical_base)
{
    if (!map_huge_page(pt, virtual_base, physical_base))
        on_critical_mapping_failed(virtual_base, physical_base, 1, true);
}

bool map_critical_huge_pages(PageTable* pt, u64 virtual_base, u64 physical_base, size_t pages)
{
    if (!map_huge_pages(pt, virtual_base, physical_base, pages))
        on_critical_mapping_failed(virtual_base, physical_base, pages, true);
}

}
