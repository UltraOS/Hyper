#pragma once

#include "common/types.h"
#include "memory_services.h"

u32 allocator_set_default_alloc_type(u32 type);

void *allocate_pages_with_type_at(u64, size_t, u32 type);
void *allocate_pages_with_type(size_t, u32 type);
void *allocate_pages_at(u64, size_t);
void *allocate_pages(size_t);
void *allocate_bytes(size_t);

// never fails, panics if unable to satisfy allocation
void *allocate_critical_pages_with_type_at(u64, size_t, u32 type);
void *allocate_critical_pages_with_type(size_t, u32);
void *allocate_critical_pages_at(u64, size_t);
void *allocate_critical_pages(size_t);
void *allocate_critical_bytes(size_t);

void free_pages(void*, size_t);
void free_bytes(void*, size_t);
