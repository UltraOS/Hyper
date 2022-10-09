#pragma once

#include "common/types.h"

struct page_table {
    u64 *root;
    size_t levels;
};

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
