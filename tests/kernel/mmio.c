#include "mmio.h"
#include "common/types.h"
#include "common/align.h"
#include "common/string.h"
#include "arch/constants.h"
#include "ultra_protocol.h"
#include "ultra_helpers.h"
#include "test_ctl.h"

static u64 g_direct_map_base;

static void *phys_to_virt(u64 phys)
{
    /* All RAM (including the page tables) is mapped at direct_map_base + phys */
    return (void*)(ptr_t)(g_direct_map_base + phys);
}

/*
 * Page-table pages are bump-allocated from the largest FREE region in the
 * memory map, and accessed through the direct map like any other RAM.
 */
static u64 g_alloc_next;
static u64 g_alloc_end;

static void pt_alloc_init(struct ultra_boot_context *bctx)
{
    struct ultra_memory_map_attribute *mm;
    size_t i, count;
    u64 best_base = 0, best_size = 0;

    mm = (struct ultra_memory_map_attribute*)
        find_attr(bctx, ULTRA_ATTRIBUTE_MEMORY_MAP);
    count = ULTRA_MEMORY_MAP_ENTRY_COUNT(mm->header);

    for (i = 0; i < count; ++i) {
        struct ultra_memory_map_entry *e = &mm->entries[i];

        if (e->type == ULTRA_MEMORY_TYPE_FREE && e->size > best_size) {
            best_base = e->physical_address;
            best_size = e->size;
        }
    }

    if (!best_size)
        test_fail("no free memory to build MMIO page tables\n");

    g_alloc_next = ALIGN_UP(best_base, PAGE_SIZE);
    g_alloc_end = best_base + best_size;
}

static u64 alloc_table_page(void)
{
    u64 page = g_alloc_next;

    if (page + PAGE_SIZE > g_alloc_end)
        test_fail("ran out of memory for MMIO page tables\n");

    g_alloc_next += PAGE_SIZE;
    memzero(phys_to_virt(page), PAGE_SIZE);
    return page;
}

#if defined(__i386__) || defined(__x86_64__)

#define X86_PRESENT (1ull << 0)
#define X86_RW      (1ull << 1)
#define X86_PCD     (1ull << 4)
#define X86_PS      (1ull << 7)

static u64 x86_root_table(u8 depth)
{
    ptr_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));

    if (depth == 2)       // i686 non-PAE, 4K aligned page directory
        return cr3 & 0xFFFFF000u;
    if (depth == 3)       // i686 PAE, 32 byte aligned PDPT
        return cr3 & 0xFFFFFFE0u;

    return (u64)cr3 & 0x000FFFFFFFFFF000ull; // long mode
}

static u64 x86_addr_mask(bool is32)
{
    return is32 ? 0xFFFFF000ull : 0x000FFFFFFFFFF000ull;
}

static u64 x86_read_entry(u64 table, size_t idx, bool is32)
{
    void *t = phys_to_virt(table);
    return is32 ? ((volatile u32*)t)[idx] : ((volatile u64*)t)[idx];
}

static void x86_write_entry(u64 table, size_t idx, bool is32, u64 val)
{
    void *t = phys_to_virt(table);

    if (is32)
        ((volatile u32*)t)[idx] = (u32)val;
    else
        ((volatile u64*)t)[idx] = val;
}

static void x86_level_index(u8 depth, size_t level, unsigned *shift, u64 *mask)
{
    if (depth == 2) {                     // non-PAE: page dir, page table
        *shift = level == 0 ? 22 : 12;
        *mask = 0x3FF;
        return;
    }

    *shift = 12 + 9 * (depth - 1 - level);
    // The PAE PDPT is only 4 entries wide
    *mask = (level == 0 && depth == 3) ? 0x3 : 0x1FF;
}

static void arch_map_page(u8 depth, u64 va, u64 phys)
{
    bool is32 = depth == 2;
    u64 table = x86_root_table(depth);
    unsigned shift;
    u64 mask;
    size_t level, idx;

    // Walk/populate the intermediate levels
    for (level = 0; level + 1 < depth; ++level) {
        u64 ent;

        x86_level_index(depth, level, &shift, &mask);
        idx = (va >> shift) & mask;
        ent = x86_read_entry(table, idx, is32);

        if (ent & X86_PRESENT) {
            /*
             * A huge page here means the loader mapped this supposedly-device
             * VA as RAM; descending would treat the mapped memory as a page
             * table and corrupt it.
             */
            if (ent & X86_PS) {
                test_fail("VA 0x%016llX is covered by a level %zu huge page "
                          "(0x%016llX)\n", va, level, ent);
            }

            table = ent & x86_addr_mask(is32);
            continue;
        }

        ent = alloc_table_page();
        x86_write_entry(table, idx, is32, ent | X86_PRESENT | X86_RW);
        table = ent;
    }

    // Install the leaf, cache-disabled for MMIO
    x86_level_index(depth, depth - 1, &shift, &mask);
    idx = (va >> shift) & mask;
    x86_write_entry(table, idx, is32, phys | X86_PRESENT | X86_RW | X86_PCD);
}

static void arch_flush_tlb(void)
{
    ptr_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

/* i686's direct map only reaches 1 GiB; grow a window down from its base */
static ptr_t g_i686_window;

static ptr_t arch_reserve_va(u64 page_phys, size_t pages)
{
    if (sizeof(void*) == 8)
        return (ptr_t)(g_direct_map_base + page_phys);

    if (!g_i686_window)
        g_i686_window = (ptr_t)g_direct_map_base;

    g_i686_window -= pages << PAGE_SHIFT;
    return g_i686_window;
}

#elif defined(__aarch64__)

#define AARCH64_VALID     (1ull << 0)
#define AARCH64_TABLE     (1ull << 1) // table (levels 0-2) or page (level 3)
#define AARCH64_AF        (1ull << 10)
#define AARCH64_ADDR_MASK 0x0000FFFFFFFFF000ull

static u64 arch_root_table(void)
{
    u64 ttbr;

    // The direct map lives in the top half of the address space, i.e. TTBR1
    asm volatile("mrs %0, ttbr1_el1" : "=r"(ttbr));
    return ttbr & AARCH64_ADDR_MASK;
}

static void arch_map_page(u8 depth, u64 va, u64 phys)
{
    u64 table = arch_root_table();
    size_t level, idx;

    for (level = 0; level + 1 < depth; ++level) {
        unsigned shift = 12 + 9 * (depth - 1 - level);
        volatile u64 *t = phys_to_virt(table);
        u64 ent;

        idx = (va >> shift) & 0x1FF;
        ent = t[idx];

        if (ent & AARCH64_VALID) {
            /*
             * A block descriptor here means the loader mapped this
             * supposedly-device VA as RAM; descending would treat the mapped
             * memory as a page table and corrupt it.
             */
            if (!(ent & AARCH64_TABLE)) {
                test_fail("VA 0x%016llX is covered by a level %zu block "
                          "descriptor (0x%016llX)\n", va, level, ent);
            }

            table = ent & AARCH64_ADDR_MASK;
            continue;
        }

        ent = alloc_table_page();
        t[idx] = ent | AARCH64_VALID | AARCH64_TABLE;
        table = ent;
    }

    // Level 3 page descriptor: Normal-NC (AttrIndx 0), matching the loader
    idx = (va >> 12) & 0x1FF;
    ((volatile u64*)phys_to_virt(table))[idx] =
        (phys & AARCH64_ADDR_MASK) | AARCH64_VALID | AARCH64_TABLE | AARCH64_AF;
}

static void arch_flush_tlb(void)
{
    asm volatile("dsb ish; tlbi vmalle1; dsb ish; isb" ::: "memory");
}

static ptr_t arch_reserve_va(u64 page_phys, size_t pages)
{
    (void)pages;
    return (ptr_t)(g_direct_map_base + page_phys);
}

#else
#error "Unsupported architecture"
#endif

void *mmio_map(struct ultra_boot_context *bctx, u64 phys, size_t size)
{
    static bool alloc_ready;
    struct ultra_platform_info_attribute *pi;
    u64 page_phys = ALIGN_DOWN(phys, PAGE_SIZE);
    u64 offset = phys - page_phys;
    size_t i, pages = PAGE_ROUND_UP(size + offset) >> PAGE_SHIFT;
    ptr_t va;

    pi = (struct ultra_platform_info_attribute*)
        find_attr(bctx, ULTRA_ATTRIBUTE_PLATFORM_INFO);
    g_direct_map_base = pi->higher_half_base;

    if (!alloc_ready) {
        pt_alloc_init(bctx);
        alloc_ready = true;
    }

    va = arch_reserve_va(page_phys, pages);

    for (i = 0; i < pages; ++i) {
        arch_map_page(pi->page_table_depth, va + (i << PAGE_SHIFT),
                      page_phys + ((u64)i << PAGE_SHIFT));
    }

    arch_flush_tlb();
    return (void*)(va + offset);
}
