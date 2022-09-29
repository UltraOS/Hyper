#define MSG_FMT(msg) "BIOS-MM: " msg

#include "common/minmax.h"
#include "common/bug.h"
#include "common/log.h"
#include "common/string.h"
#include "bios_memory_services.h"
#include "memory_services.h"
#include "services_impl.h"
#include "bios_call.h"

#define BUFFER_CAPACITY (PAGE_SIZE / sizeof(struct memory_map_entry))
static struct memory_map_entry entries_buffer[BUFFER_CAPACITY];
static size_t entry_count = 0;

static void mme_emplace_at(size_t idx, struct memory_map_entry *me)
{
    BUG_ON(idx > entry_count);

    if (entry_count >= BUFFER_CAPACITY)
        oops("out of memory map slot capacity\n");

    mme_insert(entries_buffer, me, idx, entry_count++);
}

static void mme_emplace(struct memory_map_entry *me)
{
    mme_emplace_at(entry_count, me);
}

// 'SMAP'
#define ASCII_SMAP 0x534d4150

struct e820_entry {
    u64 address;
    u64 size_in_bytes;
    u32 type;
    u32 attributes;
};

// https://uefi.org/specs/ACPI/6.4/15_System_Address_Map_Interfaces/int-15h-e820h---query-system-address-map.html
static void load_e820(void)
{
    struct memory_map_entry me;
    struct e820_entry entry;
    struct real_mode_regs regs = {
        .eax = 0xE820,
        .ecx = sizeof(entry),
        .edx = ASCII_SMAP,
        .edi = (u32)&entry
    };
    bool first_call = true;

    do {
        bios_call(0x15, &regs, &regs);

        if (is_carry_set(&regs)) {
            if (first_call)
                oops("E820 call unsupported by the BIOS\n");

            // end of list
            break;
        }

        first_call = false;

        if (regs.eax != ASCII_SMAP)
            oops("E820 call failed, invalid signature %u\n", regs.eax);

        // Restore registers to expected state
        regs.eax = 0xE820;
        regs.edx = ASCII_SMAP;

        if (entry.size_in_bytes == 0) {
            print_warn("E820 returned an empty range, skipped\n");
            continue;
        }

        if (regs.ecx == sizeof(entry) && !(entry.attributes & 1)) {
            print_warn("E820 attribute reserved bit not set, skipped\n");
            continue;
        }

        print_info("range: 0x%016llX -> 0x%016llX, type: 0x%02X\n", entry.address,
                   entry.address + entry.size_in_bytes, entry.type);

        me = (struct memory_map_entry) {
            .physical_address = entry.address,
            .size_in_bytes = entry.size_in_bytes,
            .type = entry.type
        };
        mme_align_if_needed(&me);

        if (mme_is_valid(&me))
            mme_emplace(&me);
    } while (regs.ebx);
}

static void allocate_out_of(size_t mme_idx, struct memory_map_entry *new_mme)
{
    struct memory_map_entry *me = &entries_buffer[mme_idx];
    size_t insert_idx = mme_idx;
    u64 me_end = mme_end(me);
    u64 new_end = mme_end(new_mme);
    bool before_valid, after_valid;

    struct memory_map_entry range_before = {
        .physical_address = me->physical_address,
        .size_in_bytes = new_mme->physical_address - me->physical_address,
        .type = me->type
    };
    before_valid = mme_is_valid(&range_before);

    struct memory_map_entry range_after = {
        .physical_address = new_end,
        .size_in_bytes = me_end - new_end,
        .type = me->type
    };
    after_valid = mme_is_valid(&range_after);

    // New map entry is always either fully inside this one or equal to it
    BUG_ON(me->physical_address > new_mme->physical_address || me_end < new_end);
    BUG_ON(me->type == new_mme->type);

    if (before_valid) {
        struct memory_map_entry *me_after = NULL;

        entries_buffer[insert_idx++] = range_before;

        if (mme_idx != (entry_count - 1) && !after_valid)
            me_after = &entries_buffer[mme_idx + 1];

        /*
         * Attempt to merge the allocated piece with the range after to avoid
         * an extra memmove and map entry count increase. This works surprisingly
         * often, since most allocations are done top-down.
         */
        if (me_after && me_after->type == new_mme->type &&
            mme_end(new_mme) == me_after->physical_address)
        {
            me_after->physical_address = new_mme->physical_address;
            me_after->size_in_bytes += new_mme->size_in_bytes;
        } else {
            mme_emplace_at(insert_idx++, new_mme);
        }
    } else {
        entries_buffer[insert_idx++] = *new_mme;
    }

    if (after_valid)
        mme_emplace_at(insert_idx, &range_after);

    /*
     * This check is safe because of the invariant that the map is always
     * compressed before this call.
     * - If there was a valid range before we know for sure that we
     *   can't compress anything.
     * - If there was a valid range after, same thing applies.
     * - If there wasn't a valid range after we have an optimized branch
     *   in the before_valid branch for merging in this case.
     */
    if (!before_valid) {
        mme_idx = mme_idx ? mme_idx - 1 : 0;
        entry_count = mm_fixup(entries_buffer + mme_idx, entry_count - mme_idx, 0, 0);
        entry_count += mme_idx;
    }
}

static u64 allocate_top_down(size_t page_count, u64 upper_limit, u32 type)
{
    u64 range_end, bytes_to_allocate = page_count * PAGE_SIZE;
    u64 allocated_end = 0;
    struct memory_map_entry allocated_mme;
    size_t i = entry_count;

    if (bytes_to_allocate <= page_count)
        oops("invalid allocation size of %zu pages\n", page_count);

    while (i-- > 0) {
        struct memory_map_entry *me = &entries_buffer[i];

        if (me->physical_address >= upper_limit)
            continue;

        if (me->type != MEMORY_TYPE_FREE)
            continue;

        range_end = MIN(mme_end(me), upper_limit);

        // Not enough length after cutoff
        if ((range_end - me->physical_address) < bytes_to_allocate)
            continue;

        allocated_end = range_end;
        break;
    }

    if (!allocated_end)
        return 0;

    allocated_mme = (struct memory_map_entry) {
        .physical_address = allocated_end - bytes_to_allocate,
        .size_in_bytes = bytes_to_allocate,
        .type = type
    };
    allocate_out_of(i, &allocated_mme);

    return allocated_mme.physical_address;
}

static u64 allocate_within(size_t page_count, u64 lower_limit, u64 upper_limit, u32 type)
{
    u64 range_begin, bytes_to_allocate = page_count * PAGE_SIZE;
    ssize_t mme_idx;
    struct memory_map_entry *picked_mme = NULL;
    struct memory_map_entry allocated_mme;

    if (bytes_to_allocate <= page_count)
        oops("invalid allocation size of %zu pages\n", page_count);

    // invalid input
    if (lower_limit >= upper_limit)
        goto out_invalid_allocation;

    // search gap is too small
    if (lower_limit + bytes_to_allocate > upper_limit)
        goto out_invalid_allocation;

    // overflow
    if (lower_limit + bytes_to_allocate < lower_limit)
        goto out_invalid_allocation;

    mme_idx = mm_find_first_that_contains(entries_buffer, entry_count, lower_limit, true);
    if (mme_idx < 0)
        return 0;

    for (; mme_idx < (ssize_t)entry_count; ++mme_idx) {
        picked_mme = &entries_buffer[mme_idx];
        u64 end = mme_end(picked_mme);
        bool is_bad_range;

        if (picked_mme->type == MEMORY_TYPE_FREE) {
            u64 available_gap = MIN(end, upper_limit) - MAX(picked_mme->physical_address, lower_limit);
            is_bad_range = available_gap < bytes_to_allocate;
        } else {
            is_bad_range = true;
        }

        if (is_bad_range) {
            if (end >= upper_limit)
                return 0;

            if ((upper_limit - end) < bytes_to_allocate)
                return 0;

            continue;
        }

        break;
    }

    if (mme_idx == (ssize_t)entry_count)
        return 0;

    range_begin = MAX(lower_limit, picked_mme->physical_address);
    allocated_mme = (struct memory_map_entry) {
        .physical_address = range_begin,
        .size_in_bytes = bytes_to_allocate,
        .type = type
    };
    allocate_out_of(mme_idx, &allocated_mme);

    return allocated_mme.physical_address;

out_invalid_allocation:
    oops("invalid allocate_within() call %zu pages within:\n0x%016llX -> 0x%016llX\n",
         page_count, lower_limit, upper_limit);
}

u64 ms_allocate_pages(size_t count, u64 upper_limit, u32 type)
{
    SERVICE_FUNCTION();
    OOPS_ON(type <= MEMORY_TYPE_MAX);

    return allocate_top_down(count, upper_limit, type);
}

u64 ms_allocate_pages_at(u64 address, size_t count, u32 type)
{
    SERVICE_FUNCTION();
    OOPS_ON(type <= MEMORY_TYPE_MAX);

    return allocate_within(count, address, address + (PAGE_SIZE * count), type);
}

void ms_free_pages(u64 address, size_t count)
{
    SERVICE_FUNCTION();

    ssize_t mme_idx;
    struct memory_map_entry freed_mme = {
        .physical_address = address,
        .size_in_bytes = count * PAGE_SIZE,
        .type = MEMORY_TYPE_FREE
    };

    mme_idx = mm_find_first_that_contains(entries_buffer, entry_count,
                                          address, false);
    if (mme_idx < 0)
        oops("invalid free at 0x%016llX pages: %zu\n", address, count);

    allocate_out_of(mme_idx, &freed_mme);
}

size_t services_release_resources(void *buf, size_t capacity, size_t elem_size,
                                  mme_convert_t entry_convert)
{
    SERVICE_FUNCTION();
    size_t i;

    entry_count = mm_fixup(buf, entry_count, 0, FIXUP_IF_DIRTY);
    if (capacity < entry_count)
        return entry_count;

    /*
     * The buffer is finally large enough, we can now destroy loader
     * reclaimable memory if the protocol doesn't support it and
     * transform it into MEMORY_TYPE_FREE safely as services are now
     * disabled.
     */
    entry_count = mm_fixup(buf, entry_count, 0, FIXUP_NO_PRESERVE_LOADER_RECLAIM);

    BUG_ON(!entry_convert && (elem_size != sizeof(struct memory_map_entry)));

    for (i = 0; i < entry_count; ++i) {
        struct memory_map_entry *me = &entries_buffer[i];

        if (entry_convert) {
            entry_convert(me, buf);
        } else {
            memcpy(buf, me, sizeof(*me));
        }

        buf += elem_size;
    }

    services_offline = true;
    return entry_count;
}

#define STAGE2_BASE_PAGE 0x00007000
#define STAGE2_END_PAGE  0x00080000

static void initialize_memory_map(void)
{
    u64 res;

    load_e820();
    entry_count = mm_fixup(entries_buffer, entry_count, BUFFER_CAPACITY,
                           FIXUP_UNSORTED | FIXUP_OVERLAP_RESOLVE);

    // Try to allocate ourselves
    res = ms_allocate_pages_at(STAGE2_BASE_PAGE, (STAGE2_END_PAGE - STAGE2_BASE_PAGE) / PAGE_SIZE,
                               MEMORY_TYPE_LOADER_RECLAIMABLE);
    if (res != STAGE2_BASE_PAGE)
        print_warn("failed to mark loader base 0x%08X as allocated\n", STAGE2_BASE_PAGE);
}

void mm_foreach_entry(mme_foreach_t func, void *user)
{
    size_t i;
    BUG_ON(entry_count == 0);

    for (i = 0; i < entry_count; ++i) {
        if (!func(user, &entries_buffer[i]))
            break;
    }
}

void bios_memory_services_init(void)
{
    initialize_memory_map();
}
