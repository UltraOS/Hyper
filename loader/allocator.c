#include "allocator.h"
#include "common/log.h"
#include "common/format.h"
#include "common/string.h"
#include "common/align.h"
#include "common/string_view.h"
#include "services.h"
#include "memory_services.h"

static u32 default_alloc_type = MEMORY_TYPE_RESERVED;

u32 allocator_set_default_alloc_type(u32 type)
{
    u32 prev = default_alloc_type;
    default_alloc_type = type;
    return prev;
}

#define ANY_ADDRESS "<any-address>"

static void allocation_did_fail(const struct allocation_spec *spec)
{
    u32 type = spec->type ?: default_alloc_type;
    bool is_critical = spec->flags & ALLOCATE_CRITICAL;
    enum log_level lvl = is_critical ? LOG_LEVEL_ERR : LOG_LEVEL_WARN;
    char address_as_string[32];

    if (spec->flags & ALLOCATE_PRECISE) {
        snprintf(address_as_string, sizeof(address_as_string), "0x%016llX",
                 spec->addr);
    } else {
        struct string_view any_address_str = SV(ANY_ADDRESS);
        memcpy(address_as_string, any_address_str.text, any_address_str.size + 1);
    }

    printlvl(lvl,
        "failed to satisfy an allocation at %s with %zu pages of type 0x%08X\n",
        address_as_string, spec->pages, type);

    if (is_critical)
        loader_abort();
}

u64 allocate_pages_ex(const struct allocation_spec *spec)
{
    u64 result;
    u32 type = spec->type ?: default_alloc_type;

    if (spec->flags & ALLOCATE_PRECISE) {
        result = ms_allocate_pages_at(spec->addr, spec->pages, type);
    } else {
        u64 ceiling = spec->ceiling ?: ALLOCATOR_DEFAULT_CEILING;
        result = ms_allocate_pages(spec->pages, ceiling, type);
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

    allocation_did_fail(spec);
    return 0;
}

void free_pages(void *address, size_t count)
{
    ms_free_pages((ptr_t)address, count);
}

void free_bytes(void *address, size_t count)
{
    size_t page_count = PAGE_ROUND_UP(count) / PAGE_SIZE;
    free_pages(address, page_count);
}
