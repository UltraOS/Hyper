#pragma once

#include "Common/Types.h"

namespace vm {

struct PageTable {
    u64* root;
    size_t levels;
};

// maps 4K pages
bool map_page(PageTable*, u64 virtual_base, u64 physical_base);
bool map_pages(PageTable*, u64 virtual_base, u64 physical_base, size_t pages);
void map_critical_page(PageTable*, u64 virtual_base, u64 physical_base);
void map_critical_pages(PageTable*, u64 virtual_base, u64 physical_base, size_t pages);

// maps 2M pages
bool map_huge_page(PageTable*, u64 virtual_base, u64 physical_base);
bool map_huge_pages(PageTable*, u64 virtual_base, u64 physical_base, size_t pages);
void map_critical_huge_page(PageTable*, u64 virtual_base, u64 physical_base);
void map_critical_huge_pages(PageTable*, u64 virtual_base, u64 physical_base, size_t pages);

}
