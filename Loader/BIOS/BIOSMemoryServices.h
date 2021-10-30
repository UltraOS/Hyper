#pragma once

#include "Services.h"

struct PhysicalRange;

class BIOSMemoryServices final : public MemoryServices {
public:
    Address64 allocate_pages(size_t count, Address64 upper_limit, u32 type, TopDown) override;
    Address64 allocate_pages_at(Address64 address, size_t count, u32 type) override;

    void free_pages(Address64 address, size_t count) override;

    size_t copy_map(MemoryMapEntry* into_buffer, size_t capacity_in_bytes, size_t& out_key) override;
    bool handover(size_t key) override;

    static BIOSMemoryServices create();

private:
    BIOSMemoryServices(PhysicalRange*, size_t capacity);

    void allocate_out_of(const PhysicalRange& allocated, size_t index_of_original, bool invert_priority = false);

    Address64 allocate_top_down(size_t page_count, Address64 upper_limit, u32 type);
    Address64 allocate_within(size_t page_count, Address64 lower_limit, Address64 upper_limit, u32 type);

    void emplace_range(const PhysicalRange&);
    void emplace_range_at(size_t index, const PhysicalRange&);
    PhysicalRange& at(size_t index);

    void erase_range_at(size_t index);

    void load_e820();
    void sort();
    void correct_overlapping_ranges(size_t hint = 0);

    [[nodiscard]] PhysicalRange* begin() const;
    [[nodiscard]] PhysicalRange* end() const;

    [[noreturn]] void on_use_after_release(StringView function);

private:
    size_t m_key { 0xDEADBEEF };
    PhysicalRange* m_buffer { nullptr };
    size_t m_capacity { 0 };
    size_t m_size { 0 };
    bool m_released { false };
};
