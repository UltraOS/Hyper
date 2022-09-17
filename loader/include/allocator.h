#pragma once

#include "common/types.h"
#include "common/attributes.h"
#include "common/constants.h"
#include "common/align.h"
#include "common/helpers.h"
#include "memory_services.h"

#define ALLOCATOR_DEFAULT_CEILING (4ull * GB)
#define ALLOCATOR_DEFAULT_ALLOC_TYPE MEMORY_TYPE_LOADER_RECLAIMABLE

// ALLOCATE_CEILING is implicit if ALLOCATE_PRECISE is not set
#define ALLOCATE_PRECISE  (1 << 0)
#define ALLOCATE_CRITICAL (1 << 1)
#define ALLOCATE_STACK    (1 << 2)

struct allocation_spec {
    union {
        u64 addr;
        u64 ceiling;
    };

    size_t pages;

    u32 flags;
    u32 type;
};

u64 allocate_pages_ex(const struct allocation_spec*);

static ALWAYS_INLINE
void *allocate_pages(size_t count)
{
    struct allocation_spec spec = {
        .pages = count
    };
    return ADDR_TO_PTR(allocate_pages_ex(&spec));
}

static ALWAYS_INLINE
void *allocate_bytes(size_t count)
{
    struct allocation_spec spec = {
        .pages = PAGE_ROUND_UP(count) >> PAGE_SHIFT
    };
    return ADDR_TO_PTR(allocate_pages_ex(&spec));
}

static ALWAYS_INLINE
void *allocate_critical_pages(size_t count)
{
    struct allocation_spec spec = {
        .pages = count,
        .flags = ALLOCATE_CRITICAL,
    };
    return ADDR_TO_PTR(allocate_pages_ex(&spec));
}

static ALWAYS_INLINE
void *allocate_critical_bytes(size_t count)
{
    size_t page_count = PAGE_ROUND_UP(count) >> PAGE_SHIFT;
    return allocate_critical_pages(page_count);
}

void free_pages(void*, size_t);
void free_bytes(void*, size_t);
