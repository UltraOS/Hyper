#pragma once

#include "virtual_memory.h"

ptr_t pt_get_table_page(u64 max_address);

u8 pt_table_width_shift_for_level(struct page_table *pt, size_t idx);
