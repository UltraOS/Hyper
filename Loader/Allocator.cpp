#include "Allocator.h"
#include "Common/Logger.h"

namespace allocator {

static MemoryServices* g_backend = nullptr;

MemoryServices* set_backend(MemoryServices* backend)
{
    auto* previous = g_backend;
    g_backend = backend;
    return previous;
}

void* allocate_bytes(size_t count)
{
    auto page_count = page_round_up(count) / page_size;
    return allocate_pages(page_count);
}

void free_bytes(void* address, size_t count)
{
    auto page_count = page_round_up(count) / page_size;
    free_pages(address, page_count);
}

void* allocate_pages(size_t count)
{
    if (!g_backend)
        return nullptr;

    auto* data = g_backend->allocate_pages(count, 4ull * GB, MEMORY_TYPE_LOADER_RECLAIMABLE, TopDown::YES).as_pointer<void>();
    if (!data)
        logger::warning("failed to satisfy allocation of ", count, " pages");

    return data;
}

void free_pages(void* address, size_t count)
{
    if (!g_backend)
        panic("free() called without a valid backend");

    g_backend->free_pages(address, count);
}

}
