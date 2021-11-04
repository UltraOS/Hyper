#include "BIOSMemoryServices.h"

#include "Common/StaticArray.h"
#include "Common/Range.h"
#include "Common/Logger.h"
#include "Common/Runtime.h"
#include "Common/Utilities.h"

#include "BIOSCall.h"

struct PhysicalRange : public LongRange {
    uint64_t type { MEMORY_TYPE_INVALID };

    PhysicalRange() = default;

    PhysicalRange(Address64 start, uint64_t size_in_bytes, uint64_t type)
        : LongRange(start, size_in_bytes)
        , type(type)
    {
    }

    [[nodiscard]] bool is_free() const { return type == MEMORY_TYPE_FREE; }
    [[nodiscard]] bool is_reserved() const { return !is_free(); }

    bool operator<(const PhysicalRange& other) const
    {
        return LongRange::operator<(static_cast<const BasicRange&>(other));
    }

    bool operator==(const PhysicalRange& other) const
    {
        return LongRange::operator==(other) && type == other.type;
    }

    bool operator!=(const PhysicalRange& other) const
    {
        return !operator==(other);
    }

    [[nodiscard]] PhysicalRange aligned_to(size_t alignment) const
    {
        PhysicalRange new_range {};
        new_range.reset_with(BasicRange::aligned_to(alignment));
        new_range.type = type;

        return new_range;
    }

    // Splits/merges two overlapping physical ranges while taking their types into account
    [[nodiscard]] StaticArray<PhysicalRange, 3>
    shatter_against(const PhysicalRange& other, bool invert_priority = false) const
    {
        StaticArray<PhysicalRange, 3> ranges {};

        // cannot shatter against non-overlapping range
        ASSERT(contains(other.begin()));

        // cut out the overlapping piece by default
        ranges[0] = *this;
        ranges[0].set_end(other.begin());

        // both ranges have the same type, so we can just merge them
        if (type == other.type) {
            ranges[0].set_end(end() > other.end() ? end() : other.end());
            return ranges;
        }

        // other range is fully inside this range
        if (other.end() <= end()) {
            ranges[2].reset_with_two_pointers(other.end(), end());
            ranges[2].type = type;
        }

        // we cut out the overlapping piece of the other range and make it our type
        if (type > other.type && !invert_priority) {
            ranges[0].set_end(end());

            if (end() <= other.end()) {
                ranges[1] = other;
                ranges[1].set_begin(ranges[0].end());
            } else { // since we swallowed the other range we don't need this
                ranges[2] = {};
            }
        } else { // our overlapping piece gets cut out and put into the other range
            ranges[1] = other;
        }

        return ranges;
    }
};

static constexpr size_t buffer_capacity = page_size / sizeof(PhysicalRange);
static PhysicalRange g_entries_buffer[buffer_capacity];

BIOSMemoryServices BIOSMemoryServices::create()
{
    auto instance = BIOSMemoryServices(g_entries_buffer, buffer_capacity);

    return instance;
}

BIOSMemoryServices::BIOSMemoryServices(PhysicalRange* buffer, size_t capacity)
    : m_buffer(buffer)
    , m_capacity(capacity)
{
    load_e820();
    sort();
    correct_overlapping_ranges();
}

void BIOSMemoryServices::load_e820()
{
    // https://uefi.org/specs/ACPI/6.4/15_System_Address_Map_Interfaces/int-15h-e820h---query-system-address-map.html
    struct E820Entry {
        u64 address;
        u64 size_in_bytes;
        u32 type;
        u32 attributes;
    } entry {};

    static constexpr u32 ascii_smap = 0x534d4150; // 'SMAP'

    RealModeRegisterState registers {};
    registers.eax = 0xE820;
    registers.edi = reinterpret_cast<u32>(&entry);
    registers.ecx = sizeof(entry);
    registers.edx = ascii_smap;

    static constexpr u32 e820_address_range_free_memory = 1;
    static constexpr u32 e820_address_range_reserved = 2;
    static constexpr u32 e820_address_range_acpi = 3;
    static constexpr u32 e820_address_range_nvs = 4;

    bool first_call = true;

    do {
        bios_call(0x15, &registers, &registers);

        if (registers.is_carry_set()) {
            if (first_call)
                panic("E820 call unsupported by the BIOS");

            // end of list
            break;
        }

        first_call = false;

        if (registers.eax != ascii_smap) {
            logger::error("E820 call failed, invalid signature ", registers.eax);
            hang();
        }

        // Restore registers to expected state
        registers.eax = 0xE820;
        registers.edx = ascii_smap;

        if (entry.size_in_bytes == 0) {
            logger::warning("E820 returned an empty range, skipped");
            continue;
        }

        if (registers.ecx == sizeof(entry) && !(entry.attributes & 1)) {
            logger::warning("E820 attribute reserved bit not set, skipped");
            continue;
        }

        logger::info(logger::hex, "range: ", entry.address, " -> ",
                     entry.address + entry.size_in_bytes, " type: ", entry.type);

        uint64_t converted_type = MEMORY_TYPE_INVALID;

        switch (entry.type) {
        case e820_address_range_free_memory:
            converted_type = MEMORY_TYPE_FREE;
            break;
        case e820_address_range_acpi:
            converted_type = MEMORY_TYPE_RECLAIMABLE;
            break;
        case e820_address_range_nvs:
            converted_type = MEMORY_TYPE_NVS;
            break;
        case e820_address_range_reserved:
        default: // we don't care about all other types and consider them reserved
            converted_type = MEMORY_TYPE_RESERVED;
        }

        PhysicalRange new_range(
                entry.address,
                entry.size_in_bytes,
                converted_type);

        // Don't try to align reserved physical ranges, we're not going to use them anyways
        if (new_range.type != MEMORY_TYPE_FREE) {
            emplace_range(new_range);
            continue;
        }

        new_range = new_range.aligned_to(page_size);
        new_range.set_length(page_round_down(new_range.length()));
        emplace_range(new_range);
    } while (registers.ebx);
}

void BIOSMemoryServices::sort()
{
    // 99% of BIOSes return a sorted memory map, which insertion sort handles at O(N)
    // whereas quicksort would run at O(N^2). Even if it's unsorted, most E820 memory maps
    // only contain like 10-20 entries, so it's not a big deal.
    insertion_sort(m_buffer, m_buffer + m_size);
}

void BIOSMemoryServices::correct_overlapping_ranges(size_t hint)
{
    ASSERT(m_size != 0);

    auto trivially_mergeable = [](const PhysicalRange& l, const PhysicalRange& r) {
        return l.end() == r.begin() && l.type == r.type;
    };

    for (size_t i = hint; i < m_size - 1; ++i) {
        while (i < m_size - 1 && (m_buffer[i].overlaps(m_buffer[i + 1]) || trivially_mergeable(m_buffer[i], m_buffer[i + 1]))) {
            if (trivially_mergeable(m_buffer[i], m_buffer[i + 1])) {
                m_buffer[i].set_end(m_buffer[i + 1].end());
                erase_range_at(i + 1);
                continue;
            }

            auto new_ranges = m_buffer[i].shatter_against(m_buffer[i + 1]);

            bool is_valid_range[3] {};
            for (size_t j = 0; j < 3; ++j) {
                if (new_ranges[j].is_free()) {
                    new_ranges[j] = new_ranges[j].aligned_to(page_size);
                    new_ranges[j].set_length(page_round_down(new_ranges[j].length()));
                }

                is_valid_range[j] = new_ranges[j] && (new_ranges[j].length() >= page_size || new_ranges[j].is_reserved());
            }

            auto j = i;

            for (size_t k = 0; k < 3; ++k) {
                if (!is_valid_range[k])
                    continue;

                if (j - i == 2) {
                    emplace_range_at(j++, new_ranges[k]);
                    break;
                }

                m_buffer[j++] = new_ranges[k];
            }

            if (j == i) {
                logger::error("Couldn't merge range:\n", m_buffer[i], "\nwith\n", m_buffer[i + 1]);
                hang();
            }

            // only 1 range ended up being valid, erase the second
            if (j - i == 1) {
                erase_range_at(j);
            } // else we emplaced 2 or more ranges

            // walk backwards one step, because the type of range[i] could've changed
            // therefore there's a small chance we might be able to merge i and i - 1
            if (i != 0)
                --i;
        }
    }
}

PhysicalRange& BIOSMemoryServices::at(size_t index)
{
    ASSERT(index < m_size);

    return m_buffer[index];
}

[[nodiscard]] PhysicalRange* BIOSMemoryServices::begin() const
{
    return m_buffer;
}

[[nodiscard]] PhysicalRange* BIOSMemoryServices::end() const
{
    return m_buffer + m_size;
}

void BIOSMemoryServices::emplace_range(const PhysicalRange& range)
{
    if (m_size >= m_capacity)
        panic("MemoryServices: out of slot capacity");

    m_buffer[m_size++] = range;
}

void BIOSMemoryServices::emplace_range_at(size_t index, const PhysicalRange& range)
{
    ASSERT(index <= m_size);

    if (index == m_size) {
        emplace_range(range);
        return;
    }

    if (m_size >= m_capacity)
        panic("MemoryServices: out of slot capacity");

    size_t bytes_to_move = (m_size - index) * sizeof(PhysicalRange);
    ++m_size;
    move_memory(&at(index), &at(index + 1), bytes_to_move);

    at(index) = range;
}

void BIOSMemoryServices::erase_range_at(size_t index)
{
    ASSERT(index < m_size);

    if (index == m_size - 1) {
        --m_size;
        return;
    }

    size_t bytes_to_move = (m_size - index - 1) * sizeof(PhysicalRange);
    move_memory(&at(index + 1), &at(index), bytes_to_move);

    --m_size;
}

void BIOSMemoryServices::allocate_out_of(const PhysicalRange& allocated, size_t index_of_original, bool invert_priority)
{
    auto new_ranges = m_buffer[index_of_original].shatter_against(allocated, invert_priority);

    auto current_index = index_of_original;

    for (auto& new_range : new_ranges) {
        bool range_is_valid = new_range && (new_range.length() >= page_size || new_range.is_reserved());

        if (!range_is_valid)
            continue;

        if (current_index == index_of_original) {
            at(current_index++) = new_range;
        } else {
            emplace_range_at(current_index++, new_range);
        }
    }

    // we might have some new trivially mergeable ranges after shatter, let's correct them
    correct_overlapping_ranges(index_of_original ? index_of_original - 1 : index_of_original);
}

Address64 BIOSMemoryServices::allocate_top_down(size_t page_count, Address64 upper_limit, u32 type)
{
    if (m_released)
        on_use_after_release("allocate_top_down()");

    m_key++;

    auto bytes_to_allocate = page_count * page_size;

    PhysicalRange picked_range {};
    Address64 range_end {};
    size_t i = m_size;

    while (i-- > 0) {
        auto& range = m_buffer[i];

        if (range.begin() >= upper_limit)
            continue;

        if (range.type != MEMORY_TYPE_FREE)
            continue;

        range_end = min(range.end(), upper_limit);

        // Not enough length after cutoff
        if ((range_end - range.begin()) < bytes_to_allocate)
            continue;

        picked_range = range;
        break;
    }

    if (!picked_range)
        return nullptr;

    PhysicalRange allocated_range(range_end - bytes_to_allocate, bytes_to_allocate, type);
    allocate_out_of(allocated_range, i);

    return allocated_range.begin();
}

Address64 BIOSMemoryServices::allocate_within(size_t page_count, Address64 lower_limit, Address64 upper_limit, u32 type)
{
    if (m_released)
        on_use_after_release("allocate_within()");

    m_key++;

    auto fail_on_allocation = [&]() {
        logger::error("invalid allocate_within() call ", page_count,
                      " pages within:\n", lower_limit, " -> ", upper_limit);
        hang();
    };

    auto bytes_to_allocate = page_count * page_size;

    // invaid input
    if (lower_limit >= upper_limit)
        fail_on_allocation();

    // search gap is too small
    if (lower_limit + bytes_to_allocate > upper_limit)
        fail_on_allocation();

    // overflow
    if (lower_limit + bytes_to_allocate < lower_limit)
        fail_on_allocation();

    auto should_look_further = [&](const PhysicalRange& current_range) -> bool {
        if (current_range.end() >= upper_limit)
            return false;

        auto bytes_left_in_gap = upper_limit - current_range.end();

        if (bytes_left_in_gap < bytes_to_allocate)
            return false;

        return true;
    };

    auto picked_range = lower_bound(begin(), end(), lower_limit);

    if (picked_range == end() || picked_range->begin() != lower_limit) {
        if (picked_range == begin())
            return nullptr;

        --picked_range;
    }

    for (; picked_range != end(); ++picked_range) {
        bool is_bad_range = false;

        if (picked_range->type != MEMORY_TYPE_FREE) {
            is_bad_range = true;
        } else {
            auto range_end = min(picked_range->end(), upper_limit);
            auto range_begin = max(picked_range->begin(), lower_limit);
            is_bad_range = (range_end - range_begin) < bytes_to_allocate;
        }

        if (is_bad_range) {
            if (should_look_further(*picked_range))
                continue;

            return nullptr;
        }

        break;
    }

    PhysicalRange allocated_range(
            max(lower_limit, picked_range->begin()),
            bytes_to_allocate,
            type);

    allocate_out_of(allocated_range, picked_range - begin());

    return allocated_range.begin();
}

Address64 BIOSMemoryServices::allocate_pages(size_t count, Address64 upper_limit, u32 type, TopDown top_down)
{
    if (top_down == TopDown::YES)
        return allocate_top_down(count, upper_limit, type);

    return allocate_within(count, 1 * MB, upper_limit, type);
}

Address64 BIOSMemoryServices::allocate_pages_at(Address64 address, size_t count, u32 type)
{
    return allocate_within(count, address, address + (page_size * count), type);
}

void BIOSMemoryServices::free_pages(Address64 address, size_t count)
{
    if (m_released)
        on_use_after_release("free_pages()");

    m_key++;

    auto fail_on_invalid_free = [&] ()
    {
        logger::error("MemoryServices: invalid free at ", address, " pages: ", count);
        hang();
    };

    auto this_range = lower_bound(begin(), end(), address);
    if (this_range == end() || this_range->begin() != address) {
        if (this_range == begin())
            fail_on_invalid_free();

        --this_range;
    }

    auto bytes_to_free = count * page_size;

    if (!this_range->contains(LongRange(address, bytes_to_free)))
        fail_on_invalid_free();

    PhysicalRange allocated_range(
            address,
            bytes_to_free,
            MEMORY_TYPE_FREE);

    allocate_out_of(allocated_range, this_range - begin(), true);
}

size_t BIOSMemoryServices::copy_map(MemoryMapEntry* into_buffer, size_t capacity_in_bytes, size_t& out_key)
{
    if (m_released)
        on_use_after_release("copy_map()");

    auto bytes_total = m_size * sizeof(PhysicalRange);

    if (capacity_in_bytes < bytes_total) {
        out_key = 0;
        return bytes_total;
    }

    copy_memory(m_buffer, into_buffer, bytes_total);
    out_key = m_key;

    return bytes_total;
}

bool BIOSMemoryServices::handover(size_t key)
{
    if (m_released)
        on_use_after_release("handover()");

    if (key != m_key)
        return false;

    m_released = true;
    return true;
}

void BIOSMemoryServices::on_use_after_release(StringView function)
{
    logger::error("MemoryServices: ", function, " called after handover");
    hang();
}
