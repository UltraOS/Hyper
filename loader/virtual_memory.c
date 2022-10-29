#include "virtual_memory.h"
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
    bool huge;
};

static void write_u32(void *ptr, u64 data)
{
    u32 *dword = ptr;
    *dword = data;
}

static void write_u64(void *ptr, u64 data)
{
    u64 *qword = ptr;
    *qword = data;
}

static u64 read_u32(void *ptr) { return *(u32*)ptr; }
static u64 read_u64(void *ptr) { return *(u64*)ptr; }

#define PAGE_PRESENT   (1 << 0)
#define PAGE_READWRITE (1 << 1)
#define PAGE_HUGE      (1 << 7)

static void *get_table_page(u64 max_address)
{
    struct allocation_spec spec = {
        .ceiling = max_address,
        .pages = 1,
    };

    if (!spec.ceiling || spec.ceiling > (4ull * GB))
        spec.ceiling = 4ull * GB;

    return ADDR_TO_PTR(allocate_pages_ex(&spec));
}

void page_table_init(struct page_table *pt, enum pt_type type,
                     u64 max_table_address)
{
    pt->root = get_table_page(max_table_address);
    OOPS_ON(!pt->root);

    pt->levels = pt_depth(type);
    pt->base_shift = PAGE_SHIFT;
    pt->max_table_address = max_table_address;

    memzero(pt->root, PAGE_SIZE);

    switch (type) {
    case PT_TYPE_I386_NO_PAE:
        pt->entry_width = 4;
        pt->table_width_shift = 10;
        break;

    case PT_TYPE_I386_PAE:
    case PT_TYPE_AMD64_4LVL:
    case PT_TYPE_AMD64_5LVL:
        pt->entry_width = 8;
        pt->table_width_shift = 9;
        break;

    default:
        BUG();
    }

    if (pt->entry_width == 8) {
        pt->write_slot = write_u64;
        pt->read_slot = read_u64;
    } else {
        pt->write_slot = write_u32;
        pt->read_slot = read_u32;
    }
}

static size_t get_level_bit_offset(struct page_table *pt, size_t idx)
{
    return pt->base_shift + (pt->table_width_shift * idx);
}

static size_t get_level_index(struct page_table *pt, u64 virtual_address,
                              size_t level)
{
    size_t table_width_mask = (1 << pt->table_width_shift) - 1;
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
    void *page;

    table = get_table_slot(pt, table, idx);
    entry = pt->read_slot(table);

    if (entry & PAGE_PRESENT) {
        BUG_ON(entry & PAGE_HUGE);

        entry &= ~((1 << pt->base_shift) - 1);
        return (void*)((ptr_t)entry);
    }

    page = get_table_page(pt->max_table_address);
    if (!page)
        return NULL;

    memzero((void*)page, PAGE_SIZE);

    entry = (ptr_t)page;
    entry |= PAGE_READWRITE | PAGE_PRESENT;

    pt->write_slot(table, entry);
    return page;
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

    pte_entry |= PAGE_PRESENT | PAGE_READWRITE;
    if (ctx->huge)
        pte_entry |= PAGE_HUGE;

    while (pages_to_map--) {
        pt->write_slot(slot, pte_entry);
        slot += pt->entry_width;
        pte_entry += bytes_per_page;
    }

    return true;
}

bool map_pages(const struct page_mapping_spec* spec)
{
    struct bulk_map_ctx ctx = {
        .pt = spec->pt,
        .physical_base = spec->physical_base,
        .virtual_base = spec->virtual_base,
        .page_count = spec->count,
        .huge = spec->type == PAGE_TYPE_HUGE,
    };

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
