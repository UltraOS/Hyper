#include "allocator.h"
#include "common/log.h"
#include "common/format.h"
#include "common/string.h"
#include "common/constants.h"
#include "common/string_view.h"

static struct memory_services *memory_backend;
static u32 default_alloc_type = MEMORY_TYPE_RESERVED;

u32 allocator_set_default_alloc_type(u32 type)
{
    u32 prev = default_alloc_type;
    default_alloc_type = type;
    return prev;
}

struct memory_services *allocator_set_backend(struct memory_services *b)
{
    struct memory_services *prev = memory_backend;
    memory_backend = b;
    return prev;
}

#define ANY_ADDRESS "<any-address>"

static void log_allocation_failure(u64 address, size_t count, u32 type, bool warning)
{
    enum log_level lvl = warning ? LOG_LEVEL_WARN : LOG_LEVEL_ERR;
    char address_as_string[32];

    if (address) {
        snprintf(address_as_string, sizeof(address_as_string), "0x%016llX", address);
    } else {
        struct string_view any_address_str = SV(ANY_ADDRESS);
        memcpy(address_as_string, any_address_str.text, any_address_str.size + 1);
    }

    printlvl(lvl, "failed to satisfy an allocation at %s with %zu pages of type 0x%08X\n",
             address_as_string, count, type);
}

NORETURN
static void on_failed_critical_allocation(u64 address, size_t count, u32 type)
{
    log_allocation_failure(address, count, type, false);
    loader_abort();
}

static void *do_allocate_with_type_at(u64 address, size_t count, u32 type, bool critical)
{
    void *result;

    BUG_ON(!memory_backend);

    if (!address) {
        result = (void*)((ptr_t)memory_backend->allocate_pages(count, 4ull * GB, type));
    } else {
        result = (void*)((ptr_t)memory_backend->allocate_pages_at(address, count, type));
    }

    if (result) {
#ifdef MEM_DEBUG_SPRAY
        size_t i;
        u32 *mem = result;

        for (i = 0 ; i < ((count * PAGE_SIZE) / 4); ++i)
            mem[i] = 0xDEADBEEF;
#endif

        return result;
    }

    if (critical)
        on_failed_critical_allocation(address, count, type);

    log_allocation_failure(address, count, type, !critical);
    return result;
}

void *allocate_pages_with_type_at(u64 address, size_t count, u32 type)
{
    return do_allocate_with_type_at(address, count, type, false);
}

void *allocate_pages_with_type(size_t count, u32 type)
{
    return allocate_pages_with_type_at(0, count, type);
}

void *allocate_pages(size_t count)
{
    return allocate_pages_with_type(count, default_alloc_type);
}

void *allocate_bytes(size_t count)
{
    size_t page_count = PAGE_ROUND_UP(count) / PAGE_SIZE;
    return allocate_pages(page_count);
}

void *allocate_critical_pages_with_type_at(u64 address, size_t count, u32 type)
{
    return do_allocate_with_type_at(address, count, type, true);
}

void *allocate_critical_pages_with_type(size_t count, u32 type)
{
    return allocate_critical_pages_with_type_at(0, count, type);
}

void *allocate_critical_pages_at(u64 address, size_t count)
{
    return allocate_critical_pages_with_type_at(address, count, default_alloc_type);
}

void *allocate_critical_pages(size_t count)
{
    return allocate_critical_pages_with_type(count, default_alloc_type);
}

void *allocate_critical_bytes(size_t count)
{
    size_t page_count = PAGE_ROUND_UP(count) / PAGE_SIZE;
    return allocate_critical_pages(page_count);
}

void free_pages(void *address, size_t count)
{
    BUG_ON(!memory_backend);
    memory_backend->free_pages((ptr_t)address, count);
}

void free_bytes(void *address, size_t count)
{
    size_t page_count = PAGE_ROUND_UP(count) / PAGE_SIZE;
    free_pages(address, page_count);
}
