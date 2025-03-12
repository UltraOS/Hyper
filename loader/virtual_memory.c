#include "virtual_memory.h"
#include "virtual_memory_impl.h"
#include "allocator.h"

#include "common/bug.h"
#include "common/constants.h"
#include "common/align.h"
#include "common/string.h"
#include "common/minmax.h"

struct bulk_map_ctx {
    struct page_table *pt;
    u64 physical_base, virtual_base;
    size_t page_count;
    u64 page_attributes;
    bool huge;
};

ptr_t pt_get_table_page(u64 max_address)
{
    void *ptr;
    struct allocation_spec spec = {
        .ceiling = max_address,
        .pages = 1,
    };

    if (!spec.ceiling || spec.ceiling > (4ull * GB))
        spec.ceiling = 4ull * GB;

    ptr = ADDR_TO_PTR(allocate_pages_ex(&spec));
    if (unlikely(ptr == NULL))
        return 0;

    memzero(ptr, PAGE_SIZE);
    return (ptr_t)ptr;
}

static size_t get_level_bit_offset(struct page_table *pt, size_t idx)
{
    return pt->base_shift + (pt->table_width_shift * idx);
}

WEAK
u8 pt_table_width_shift_for_level(struct page_table *pt, size_t idx)
{
    UNUSED(idx);
    return pt->table_width_shift;
}

static size_t get_level_index(struct page_table *pt, u64 virtual_address,
                              size_t level)
{
    u8 width_shift;
    size_t table_width_mask;

    width_shift = pt_table_width_shift_for_level(pt, level);

    table_width_mask = (1 << width_shift) - 1;
    u64 table_selector = virtual_address >> get_level_bit_offset(pt, level);

    return table_selector & table_width_mask;
}

static void *get_table_slot(struct page_table *pt, void *table, size_t idx)
{
    return table + pt->entry_width * idx;
}

static void *table_at(struct page_table *pt, void *table, size_t idx)
{
    u64 entry;

    table = get_table_slot(pt, table, idx);
    entry = pt->read_slot(table);

    if (entry & PAGE_PRESENT) {
        BUG_ON(pt_is_huge_page(entry));

        entry = entry & pt->entry_address_mask;
        return ADDR_TO_PTR(entry);
    }

    entry = pt_get_table_page(pt->max_table_address);
    if (!entry)
        return NULL;

    pt->write_slot(table, entry | PAGE_READWRITE | PAGE_PRESENT | PAGE_NORMAL);
    return ADDR_TO_PTR(entry);
}

#define PTE_HANDLE_LEVEL(level)                                     \
    {                                                               \
        size_t lvl_idx = get_level_index(pt, virtual_base, level);  \
                                                                    \
        cur_root = table_at(pt, cur_root, lvl_idx);                 \
        if (!cur_root)                                              \
            return false;                                           \
                                                                    \
        if ((level) == want_level)                                  \
            goto out;                                               \
    }

static bool get_pte(struct page_table *pt, u64 virtual_base,
                    size_t want_level, void **out_entry)
{
    void *cur_root = pt->root;

    if (want_level == pt->levels)
        goto out;

    switch (pt->levels) {
    case 6: PTE_HANDLE_LEVEL(5)
            FALLTHROUGH;
    case 5: PTE_HANDLE_LEVEL(4)
            FALLTHROUGH;
    case 4: PTE_HANDLE_LEVEL(3)
            FALLTHROUGH;
    case 3: PTE_HANDLE_LEVEL(2)
            FALLTHROUGH;
    case 2: PTE_HANDLE_LEVEL(1)
            break;
    default:
        BUG();
    }

out:
    *out_entry = cur_root;
    return true;
}

static bool bulk_map_pages(struct bulk_map_ctx *ctx)
{
    void *slot;
    struct page_table *pt = ctx->pt;
    size_t slot_idx, pages_to_map, bytes_per_page;
    u64 bytes_mapped, pte_entry = ctx->physical_base;
    u8 this_level = 1 + ctx->huge;

    bytes_per_page = ctx->huge ? huge_page_size(pt) : page_size(pt);

    BUG_ON(!IS_ALIGNED(ctx->virtual_base, bytes_per_page));
    BUG_ON(!IS_ALIGNED(ctx->physical_base, bytes_per_page));

    if (!get_pte(pt, ctx->virtual_base, this_level, &slot))
        return false;

    slot_idx = get_level_index(pt, ctx->virtual_base, this_level - 1);
    slot = get_table_slot(pt, slot, slot_idx);

    pages_to_map = MIN(ctx->page_count, (1 << pt->table_width_shift) - slot_idx);
    ctx->page_count -= pages_to_map;

    bytes_mapped = pages_to_map;
    bytes_mapped *= bytes_per_page;
    ctx->virtual_base += bytes_mapped;
    ctx->physical_base += bytes_mapped;

    pte_entry |= ctx->page_attributes;

    while (pages_to_map--) {
        pt->write_slot(slot, pte_entry);
        slot += pt->entry_width;
        pte_entry += bytes_per_page;
    }

    return true;
}

bool map_pages(const struct page_mapping_spec *spec)
{
    struct bulk_map_ctx ctx = {
        .pt = spec->pt,
        .physical_base = spec->physical_base,
        .virtual_base = spec->virtual_base,
        .page_count = spec->count,
        .page_attributes = PAGE_READWRITE | PAGE_PRESENT,
    };

    switch (spec->type) {
    case PAGE_TYPE_NORMAL:
        ctx.page_attributes |= PAGE_NORMAL;
        break;
    case PAGE_TYPE_HUGE:
        ctx.page_attributes |= PAGE_HUGE;
        ctx.huge = true;
        break;
    default:
        BUG();
    }

    while (ctx.page_count) {
        bool ok;

        ok = bulk_map_pages(&ctx);
        if (!ok)
            goto error_out;
    }

    return true;

error_out:
    if (!spec->critical)
        return false;

    panic("Out of memory while mapping %zu pages at 0x%016llX to phys 0x%016llX (huge: %d)\n",
          spec->count, spec->virtual_base, spec->physical_base, ctx.huge);
}

void map_copy_root_entry(struct page_table* pt, u64 src_virtual_address,
                         u64 dest_virtual_address)
{
    size_t src_idx, dst_idx;

    src_idx = get_level_index(pt, src_virtual_address, pt->levels - 1);
    dst_idx = get_level_index(pt, dest_virtual_address, pt->levels - 1);

    memcpy(pt->root + dst_idx * pt->entry_width,
           pt->root + src_idx * pt->entry_width,
           pt->entry_width);
}

u64 pt_level_entry_virtual_coverage(struct page_table *pt, size_t lvl_idx)
{
    return 1ull << get_level_bit_offset(pt, lvl_idx);
}

u64 pt_get_root_pte_at(struct page_table *pt, u64 virtual_address)
{
    size_t idx;
    u64 ret = 0;

    idx = get_level_index(pt, virtual_address, pt->levels - 1);
    memcpy(&ret, pt->root + idx * pt->entry_width, pt->entry_width);

    return (u64)(ret & pt->entry_address_mask);
}
