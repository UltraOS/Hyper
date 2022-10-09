#include "virtual_memory.h"
#include "allocator.h"

#include "common/bug.h"
#include "common/constants.h"
#include "common/align.h"
#include "common/string.h"

#define PAGE_PRESENT   (1 << 0)
#define PAGE_READWRITE (1 << 1)
#define PAGE_HUGE      (1 << 7)

void page_table_init(struct page_table *pt, enum pt_type type, void *root_page)
{
    pt->root = root_page;
    pt->levels = pt_depth(type);
    pt->base_shift = PAGE_SHIFT;

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

static void *table_at(struct page_table *pt, void *table, size_t idx)
{
    u64 entry = 0;
    void *page;
    table += idx * pt->entry_width;

    memcpy(&entry, table, pt->entry_width);

    if (entry & PAGE_PRESENT) {
        BUG_ON(entry & PAGE_HUGE);

        entry &= ~((1 << pt->base_shift) - 1);
        return (void*)((ptr_t)entry);
    }

    page = allocate_pages(1);
    if (!page)
        return NULL;

    memzero((void*)page, PAGE_SIZE);

    entry = (ptr_t)page;
    entry |= PAGE_READWRITE | PAGE_PRESENT;

    memcpy(table, &entry, pt->entry_width);
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

static void *get_table_slot(struct page_table *pt, void *table,
                            u64 virtual_address, size_t level)
{
    return table + pt->entry_width * get_level_index(pt, virtual_address, level);
}

static bool do_map_page(struct page_table *pt, u64 virtual_base,
                        u64 physical_base, bool huge)
{
    void *slot;
    u8 this_level = 1 + huge;

    if (!get_pte(pt, virtual_base, this_level, &slot))
        return false;

    slot = get_table_slot(pt, slot, virtual_base, this_level - 1);

    physical_base |= PAGE_PRESENT | PAGE_READWRITE;
    if (huge)
        physical_base |= PAGE_HUGE;

    memcpy(slot, &physical_base, pt->entry_width);
    return true;
}

bool map_pages(const struct page_mapping_spec* spec)
{
    u32 increment;
    u64 current_virt, current_phys;
    size_t i;
    bool huge = false;

    switch (spec->type) {
    case PAGE_TYPE_NORMAL:
        increment = PAGE_SIZE;
        break;
    case PAGE_TYPE_HUGE:
        increment = HUGE_PAGE_SIZE;
        huge = true;
        break;
    default:
        BUG();
    }

    // verify alignment
    BUG_ON(!IS_ALIGNED(spec->virtual_base, increment));
    BUG_ON(!IS_ALIGNED(spec->physical_base, increment));

    current_virt = spec->virtual_base;
    current_phys = spec->physical_base;

    for (i = 0; i < spec->count; ++i) {
        bool ok;

        ok = do_map_page(spec->pt, current_virt, current_phys, huge);
        if (!ok)
            goto error_out;

        current_virt += increment;
        current_phys += increment;
    }

    return true;

error_out:
    if (!spec->critical)
        return false;

    panic("Out of memory while mapping %zu pages at 0x%016llX to phys x%016llX (huge: %d)\n",
          spec->count, spec->virtual_base, spec->physical_base, huge);
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
