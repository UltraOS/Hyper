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

static void log_allocation_failure(Address64 address, size_t count, u32 type, bool warning)
{
    auto color = warning ? Color::YELLOW : Color::RED;

    char address_as_string[32];
    if (address) {
        to_hex_string(address.raw(), address_as_string, sizeof(address_as_string));
    } else {
        static constexpr StringView any_address = "<any-address>";
        copy_memory(any_address.data(), address_as_string, any_address.size());
    }

    logger::ScopedColor sc(color);
    logln("Failed to satisfy an allocation at {} with {} pages of type {}",
          address_as_string, count, type);
}

[[noreturn]] static void on_failed_critical_allocation(Address64 address, size_t count, u32 type)
{
    log_allocation_failure(address, count, type, false);
    hang();
}

static void* do_allocate_with_type_at(Address64 address, size_t count, u32 type, bool critical)
{
    if (!g_backend) {
        errorln("attempted to allocate without a valid backend");
        log_allocation_failure(address, count, type, true);
        hang();
    }

    void* result;

    if (!address) {
        result = g_backend->allocate_pages(count, 4ull * GB, type, TopDown::YES).as_pointer<void>();
    } else {
        result = g_backend->allocate_pages_at(address, count, type).as_pointer<void>();
    }

    if (result != nullptr)
        return result;

    if (critical)
        on_failed_critical_allocation(address, count, type);

    log_allocation_failure(address, count, type, critical);
    return result;
}

void* allocate_pages_with_type_at(Address64 address, size_t count, u32 type)
{
    return do_allocate_with_type_at(address, count, type, false);
}

void* allocate_pages_with_type(size_t count, u32 type)
{
    return allocate_pages_with_type_at(nullptr, count, type);
}

void* allocate_pages(size_t count)
{
    return allocate_pages_with_type(count, MEMORY_TYPE_LOADER_RECLAIMABLE);
}

void* allocate_bytes(size_t count)
{
    auto page_count = page_round_up(count) / page_size;
    return allocate_pages(page_count);
}

void* allocate_critical_pages_with_type_at(Address64 address, size_t count, u32 type)
{
    return do_allocate_with_type_at(address, count, type, true);
}

void* allocate_critical_pages_with_type(size_t count, u32 type)
{
    return allocate_critical_pages_with_type_at(nullptr, count, type);
}

void* allocate_critical_pages_at(Address64 address, size_t count)
{
    return allocate_critical_pages_with_type_at(address, count, MEMORY_TYPE_LOADER_RECLAIMABLE);
}

void* allocate_critical_pages(size_t count)
{
    return allocate_critical_pages_with_type(count, MEMORY_TYPE_LOADER_RECLAIMABLE);
}

void* allocate_critical_bytes(size_t count)
{
    auto page_count = page_round_up(count) / page_size;
    return allocate_critical_pages(page_count);
}

void free_pages(void* address, size_t count)
{
    if (!g_backend)
        panic("free() called without a valid backend");

    g_backend->free_pages(address, count);
}

}
