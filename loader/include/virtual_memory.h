#pragma once

#include "common/types.h"

struct page_table {
    u64 *root;
    size_t levels;
};

// maps 4K pages
bool map_page(struct page_table*, u64 virtual_base, u64 physical_base);
bool map_pages(struct page_table*, u64 virtual_base, u64 physical_base, size_t pages);
void map_critical_page(struct page_table*, u64 virtual_base, u64 physical_base);
void map_critical_pages(struct page_table*, u64 virtual_base, u64 physical_base, size_t pages);

// maps 2M pages
bool map_huge_page(struct page_table*, u64 virtual_base, u64 physical_base);
bool map_huge_pages(struct page_table*, u64 virtual_base, u64 physical_base, size_t pages);
void map_critical_huge_page(struct page_table*, u64 virtual_base, u64 physical_base);
void map_critical_huge_pages(struct page_table*, u64 virtual_base, u64 physical_base, size_t pages);
