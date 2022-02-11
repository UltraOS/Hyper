#include "bios_memory_services.h"
#include "physical_range.h"
#include "bios_call.h"
#include "common/minmax.h"
#include "common/bug.h"
#include "common/log.h"
#include "common/string.h"

#undef MSG_FMT
#define MSG_FMT(msg) "BIOS-MM: " msg

#define BUFFER_CAPACITY (PAGE_SIZE / sizeof(struct physical_range))
static struct physical_range entries_buffer[BUFFER_CAPACITY];
static size_t map_key = 0xDEADBEEF;
static size_t entry_count = 0;
static bool released = false;

static void emplace_range(const struct physical_range *r)
{
    if (entry_count >= BUFFER_CAPACITY)
        oops("out memory map of slot capacity");

    entries_buffer[entry_count++] = *r;
}

static void emplace_range_at(size_t index, const struct physical_range *r)
{
    size_t bytes_to_move;
    BUG_ON(index > entry_count);

    if (index == entry_count) {
        emplace_range(r);
        return;
    }

    if (entry_count >= BUFFER_CAPACITY)
        oops("out of memory map slot capacity");

    bytes_to_move = (entry_count - index) * sizeof(struct physical_range);
    ++entry_count;
    memmove(&entries_buffer[index + 1], &entries_buffer[index], bytes_to_move);

    entries_buffer[index] = *r;
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
static void load_e820()
{
    struct physical_range range;
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
                oops("E820 call unsupported by the BIOS");

            // end of list
            break;
        }

        first_call = false;

        if (regs.eax != ASCII_SMAP)
            oops("E820 call failed, invalid signature %u", regs.eax);

        // Restore registers to expected state
        regs.eax = 0xE820;
        regs.edx = ASCII_SMAP;

        if (entry.size_in_bytes == 0) {
            print_warn("E820 returned an empty range, skipped");
            continue;
        }

        if (regs.ecx == sizeof(entry) && !(entry.attributes & 1)) {
            print_warn("E820 attribute reserved bit not set, skipped");
            continue;
        }

        print_info("range: 0x%016llX -> 0x%016llX, type: 0x%02X\n", entry.address,
                   entry.address + entry.size_in_bytes, entry.type);

        range = (struct physical_range) {
            .r = { entry.address, entry.address + entry.size_in_bytes },
            .type = entry.type
        };

        // Don't try to align reserved physical ranges, we're not going to use them anyways
        if (range.type != MEMORY_TYPE_FREE) {
            emplace_range(&range);
            continue;
        }

        range_align(&range.r, PAGE_SIZE);
        emplace_range(&range);
    } while (regs.ebx);
}

static void erase_range_at(size_t index)
{
    size_t bytes_to_move;
    BUG_ON(index >= entry_count);

    if (index == entry_count - 1) {
        --entry_count;
        return;
    }

    bytes_to_move = (entry_count - index - 1) * sizeof(struct physical_range);
    memmove(&entries_buffer[index], &entries_buffer[index + 1], bytes_to_move);

    --entry_count;
}

static bool trivially_mergeable(const struct physical_range *lhs, const struct physical_range *rhs)
{
    return lhs->r.end == rhs->r.begin && lhs->type == rhs->type;
}

static void correct_overlapping_ranges(size_t first_index)
{
    size_t i;

    for (i = first_index; i < entry_count - 1; ++i) {
        struct physical_range *cur = &entries_buffer[i];
        struct physical_range *next = &entries_buffer[i + 1];
        bool tm = trivially_mergeable(cur, next);

        while (range_overlaps(&cur->r, &next->r) || tm) {
            struct shatter_result sr;
            size_t j, k;

            if (tm) {
                cur->r.end = next->r.end;
                erase_range_at(i + 1);
                goto next_range;
            }

            physical_ranges_shatter(cur, next, &sr, false);
            j = i;

            for (k = 0; k < 3; ++k) {
                bool is_free = physical_range_is_free(&sr.ranges[k]);

                if (is_free) {
                    range_align(&sr.ranges[k].r, PAGE_SIZE);

                    if (range_length(&sr.ranges[k].r) < PAGE_SIZE)
                        continue;
                }

                if (j - i == 2) {
                    emplace_range_at(j++, &sr.ranges[k]);
                    break;
                }

                entries_buffer[j++] = sr.ranges[k];
            }

            BUG_ON(j == i);

            // only 1 range ended up being valid, erase the second
            if (j - i == 1) {
                erase_range_at(j);
            } // else we emplaced 2 or more ranges

            /*
             * walk backwards one step, because the type of range[i] could've changed
             * therefore there's a small chance we might be able to merge i and i - 1
             */
            if (i != 0)
                --i;

        next_range:
            if (i >= entry_count - 1)
                return;

            cur = &entries_buffer[i];
            next = cur + 1;
            tm = trivially_mergeable(cur, next);
        }
    }
}

static void allocate_out_of(const struct physical_range *allocated_range, size_t index_of_original, bool invert_priority)
{
    struct shatter_result sr = { 0 };
    size_t i, current_index = index_of_original;

    physical_ranges_shatter(&entries_buffer[index_of_original], allocated_range, &sr, invert_priority);

    for (i = 0; i < 3; i++) {
        if (range_is_empty(&sr.ranges[i].r))
            continue;

        if (physical_range_is_free(&sr.ranges[i]) && range_length(&sr.ranges[i].r) < PAGE_SIZE)
            continue;

        if (current_index == index_of_original) {
            entries_buffer[current_index++] = sr.ranges[i];
        } else {
            emplace_range_at(current_index++, &sr.ranges[i]);
        }
    }

    // we might have some new trivially mergeable ranges after shatter, let's correct them
    correct_overlapping_ranges(index_of_original ? index_of_original - 1 : index_of_original);
}

static u64 allocate_top_down(size_t page_count, u64 upper_limit, u32 type)
{
    u64 bytes_to_allocate = page_count * PAGE_SIZE;
    struct physical_range allocated_range = { 0 };
    size_t i = entry_count;
    u64 range_end;

    if (bytes_to_allocate <= page_count)
        oops("invalid allocation size of %zu pages", page_count);

    if (released)
        oops("use-after-release: allocate_top_down()");

    map_key++;

    while (i-- > 0) {
        struct physical_range *this_range = &entries_buffer[i];

        if (this_range->r.begin >= upper_limit)
            continue;

        if (this_range->type != MEMORY_TYPE_FREE)
            continue;

        range_end = MIN(this_range->r.end, upper_limit);

        // Not enough length after cutoff
        if ((range_end - this_range->r.begin) < bytes_to_allocate)
            continue;

        allocated_range = *this_range;
        break;
    }

    if (range_is_empty(&allocated_range.r))
        return 0;

    allocated_range = (struct physical_range) {
        .r = { range_end - bytes_to_allocate, range_end },
        .type = type
    };
    allocate_out_of(&allocated_range, i, false);

    return allocated_range.begin;
}

static void fail_on_allocation(size_t page_count, u64 lower_limit, u64 upper_limit)
{
    oops("invalid allocate_within() call %zu pages within:\n0x%016llX -> 0x%016llX",
         page_count, lower_limit, upper_limit);
}

static ssize_t first_range_that_contains(u64 value, bool allow_one_above)
{
    ssize_t left = 0;
    ssize_t right = entry_count - 1;

    while (left <= right) {
        ssize_t middle = left + ((right - left) / 2);

        if (entries_buffer[middle].begin < value) {
            left = middle + 1;
        } else if (value < entries_buffer[middle].begin) {
            right = middle - 1;
        } else {
            return middle;
        }
    }

    // Left is always lower bound, right is always lower bound - 1
    if (right >= 0) {
        struct range *r = &entries_buffer[right].r;

        if (r->begin < value && value < r->end)
            return right;
    }

    // Don't return out of bounds range, even if it's lower bound
    if (left == (ssize_t)entry_count)
        left = -1;

    // Either return the lower bound range (aka one after "value") or none
    return allow_one_above ? left : -1;
}

static u64 allocate_within(size_t page_count, u64 lower_limit, u64 upper_limit, u32 type)
{
    u64 bytes_to_allocate = page_count * PAGE_SIZE;
    ssize_t range_index;
    struct range *picked_range = NULL;
    u64 range_begin;
    struct physical_range allocated_range;

    if (bytes_to_allocate <= page_count)
        oops("invalid allocation size of %zu pages", page_count);

    if (released)
        oops("use-after-release: allocate_within()");

    map_key++;

    // invalid input
    if (lower_limit >= upper_limit)
        fail_on_allocation(page_count, lower_limit, upper_limit);

    // search gap is too small
    if (lower_limit + bytes_to_allocate > upper_limit)
        fail_on_allocation(page_count, lower_limit, upper_limit);

    // overflow
    if (lower_limit + bytes_to_allocate < lower_limit)
        fail_on_allocation(page_count, lower_limit, upper_limit);

    range_index = first_range_that_contains(lower_limit, true);
    if (range_index < 0)
        return 0;

    for (; range_index < (ssize_t)entry_count; ++range_index) {
        picked_range = &entries_buffer[range_index].r;
        bool is_bad_range;

        if (physical_range_is_free(&entries_buffer[range_index])) {
            u64 available_gap = MIN(picked_range->end, upper_limit) - MAX(picked_range->begin, lower_limit);
            is_bad_range = available_gap < bytes_to_allocate;
        } else {
            is_bad_range = true;
        }

        if (is_bad_range) {
            if (picked_range->end >= upper_limit)
                return 0;

            if ((upper_limit - picked_range->end) < bytes_to_allocate)
                return 0;

            continue;
        }

        break;
    }

    if (range_index == (ssize_t)entry_count)
        return 0;

    BUG_ON(picked_range == NULL);
    range_begin = MAX(lower_limit, picked_range->begin);
    allocated_range = (struct physical_range) {
        .r = { range_begin, range_begin + bytes_to_allocate },
        .type = type
    };
    allocate_out_of(&allocated_range, range_index, false);

    return allocated_range.begin;
}

static u64 allocate_pages(size_t count, u64 upper_limit, u32 type)
{
    return allocate_top_down(count, upper_limit, type);
}

static u64 allocate_pages_at(u64 address, size_t count, u32 type)
{
    return allocate_within(count, address, address + (PAGE_SIZE * count), type);
}

static void on_invalid_free(u64 address, size_t count)
{
   oops("invalid free at 0x%016llX pages: %zu", address, count);
}

static void free_pages(u64 address, size_t count)
{
    ssize_t range_index;
    struct physical_range freed_range = {
        .r = { address, address + (count * PAGE_SIZE) },
        .type = MEMORY_TYPE_FREE
    };

    if (released)
        oops("use-after-release: free_pages()");

    map_key++;

    range_index = first_range_that_contains(address, false);
    if (range_index < 0)
        on_invalid_free(address, count);

    if (!range_contains(&entries_buffer[range_index].r, &freed_range.r))
        on_invalid_free(address, count);

    allocate_out_of(&freed_range, range_index, true);
}

static size_t copy_map(void *buf, size_t capacity, size_t elem_size,
                       size_t *out_key, entry_convert_func entry_convert)
{
    size_t i;

    if (released)
        oops("use-after-release: copy_map()");

    if (capacity == 0)
        return entry_count;

    BUG_ON(!entry_convert && (elem_size != sizeof(struct memory_map_entry)));

    for (i = 0; i < entry_count; ++i) {
        struct physical_range *pr = &entries_buffer[i];
        struct memory_map_entry entry = {
            .physical_address = pr->begin,
            .size_in_bytes = range_length(&pr->r),
            .type = pr->type
        };

        if (entry_convert) {
            entry_convert(&entry, buf);
        } else {
            memcpy(buf, &entry, sizeof(struct memory_map_entry));
        }

        buf += elem_size;
        --capacity;

        if (capacity == 0 && (i != (entry_count - 1)))
            return entry_count;
    }

    *out_key = map_key;
    return entry_count;
}

static bool handover(size_t key)
{
    if (released)
        oops("use-after-release: handover()");

    if (key != map_key)
        return false;

    released = true;
    return true;
}

#define STAGE2_BASE_PAGE 0x00007000
#define STAGE2_END_PAGE  0x00080000

static void initialize_memory_map()
{
    size_t i;
    u64 res;

    load_e820();

    /*
     * Do an insertion sort over the returned entries.
     * 99% of BIOSes return a sorted memory map, which insertion sort handles at O(N)
     * whereas quicksort would run at O(N^2). Even if it's unsorted, most E820 memory maps
     * only contain like 10-20 entries, so it's not a big deal.
     */
    for (i = 0; i < entry_count; ++i) {
        size_t j = i;

        while (j > 0 && entries_buffer[j].begin < entries_buffer[j - 1].begin) {
            struct physical_range tmp = entries_buffer[j];
            entries_buffer[j] = entries_buffer[j - 1];
            entries_buffer[j - 1] = tmp;
            --j;
        }
    }

    correct_overlapping_ranges(0);

    // Try to allocate ourselves
    res = allocate_pages_at(STAGE2_BASE_PAGE, (STAGE2_END_PAGE - STAGE2_BASE_PAGE) / PAGE_SIZE,
                            MEMORY_TYPE_LOADER_RECLAIMABLE);
    if (res != STAGE2_BASE_PAGE)
        print_warn("failed to mark loader base 0x%08X as allocated\n", STAGE2_BASE_PAGE);
}

static struct memory_services bios_ms = {
    .allocate_pages_at = allocate_pages_at,
    .allocate_pages = allocate_pages,
    .free_pages = free_pages,
    .copy_map = copy_map,
    .handover = handover
};

struct memory_services *memory_services_init()
{
    if (!entry_count)
        initialize_memory_map();

    return &bios_ms;
}
