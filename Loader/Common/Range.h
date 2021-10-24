#pragma once

#include "Runtime.h"

template <typename AddrT>
class BasicRange {
public:
    BasicRange() = default;

    BasicRange(AddrT start, size_t length)
        : m_begin(start)
        , m_end(start + length)
    {
        ASSERT(m_begin <= m_end);
    }

    static BasicRange from_two_pointers(AddrT begin, AddrT end)
    {
        ASSERT(begin <= end);

        BasicRange r;
        r.m_begin = begin;
        r.m_end = end;

        return r;
    }

    static BasicRange create_empty_at(AddrT address)
    {
        BasicRange r;
        r.m_begin = address;
        r.m_end = address;

        return r;
    }

    AddrT begin() const
    {
        return m_begin;
    }

    AddrT end() const
    {
        return m_end;
    }

    size_t length() const
    {
        return m_end - m_begin;
    }

    void set_length(size_t length)
    {
        m_end = m_begin + length;
    }

    void set_begin(AddrT begin)
    {
        m_begin = begin;
    }

    void set_end(AddrT end)
    {
        m_end = end;
    }

    void extend_by(size_t length)
    {
        set_end(m_end + length);
    }

    void advance_begin_by(size_t length)
    {
        m_begin += length;
    }

    bool contains(AddrT address) const
    {
        return address >= begin() && address < end();
    }

    bool contains(const BasicRange& other) const
    {
        return contains(other.begin()) && other.end() <= end();
    }

    bool overlaps(const BasicRange& other) const
    {
        if (empty() || other.empty())
            return false;

        return contains(other.begin()) || other.contains(begin());
    }

    BasicRange clipped_with_begin(AddrT address) const
    {
        ASSERT(contains(address) || address == end());

        return from_two_pointers(address, m_end);
    }

    BasicRange clipped_with_end(AddrT address) const
    {
        ASSERT(contains(address) || address == end());

        return from_two_pointers(m_begin, address);
    }

    BasicRange constrained_by(AddrT lowest, AddrT highest) const
    {
        ASSERT(lowest < highest);

        if (empty())
            return *this;

        BasicRange new_range = *this;

        // nothing we can do
        if (m_begin > highest)
            return from_two_pointers(m_begin, m_begin);

        // same as above
        if (m_end < lowest)
            return from_two_pointers(m_end, m_end);

        if (m_begin < lowest) {
            auto bytes_under = lowest - m_begin;
            new_range.set_begin(min(AddrT(m_begin + bytes_under), m_end));
        }

        if (m_end > highest) {
            auto bytes_over = m_end - highest;
            new_range.set_end(max(AddrT(m_end - bytes_over), m_begin));
        }

        return new_range;
    }

    void reset_with(AddrT begin, size_t length)
    {
        m_begin = begin;
        m_end = begin + length;
    }

    void reset_with(BasicRange range)
    {
        m_begin = range.m_begin;
        m_end = range.m_end;
    }

    void reset_with_two_pointers(AddrT begin, AddrT end)
    {
        ASSERT(begin <= end);

        m_begin = begin;
        m_end = end;
    }

    template <typename T>
    T* as_pointer()
    {
        return m_begin.template as_pointer<T>();
    }

    bool operator<(const BasicRange& other) const
    {
        return m_begin < other.m_begin;
    }

    bool operator==(const BasicRange& other) const
    {
        return m_begin == other.m_begin && m_end == other.m_end;
    }

    bool operator!=(const BasicRange& other) const
    {
        return !this->operator==(other);
    }

    bool empty() const
    {
        return m_begin == m_end;
    }

    void reset()
    {
        m_begin = static_cast<AddrT>(nullptr);
        m_end = static_cast<AddrT>(nullptr);
    }

    explicit operator bool() const
    {
        return !empty();
    }

    bool is_aligned_to(size_t alignment) const
    {
        return (m_begin % alignment) == 0;
    }

    void align_to(size_t alignment)
    {
        auto remainder = m_begin % alignment;
        auto aligned_begin = remainder ? AddrT(m_begin + (alignment - remainder)) : m_begin;

        ASSERT(aligned_begin < m_end);
        ASSERT(aligned_begin >= m_begin);

        m_begin = aligned_begin;
    }

    BasicRange aligned_to(size_t alignment) const
    {
        auto remainder = m_begin % alignment;
        auto aligned_begin = remainder ? AddrT(m_begin + (alignment - remainder)) : m_begin;

        // Overflow or not enough length to handle alignment
        if (aligned_begin < m_begin || aligned_begin >= m_end) {
            return {};
        }

        return from_two_pointers(aligned_begin, m_end);
    }

    template <typename StreamT>
    friend StreamT& operator<<(StreamT&& logger, const BasicRange& range)
    {
        logger << range.begin() << " -> " << range.end() << " length: " << range.length();

        return logger;
    }

    friend bool operator<(const BasicRange& l, AddrT r)
    {
        return l.begin() < r;
    }

    friend bool operator<(AddrT l, const BasicRange& r)
    {
        return l < r.begin();
    }

private:
    AddrT m_begin = AddrT();
    AddrT m_end = AddrT();
};

using Range = BasicRange<Address>;
using ShortRange = BasicRange<Address32>;
using LongRange = BasicRange<Address64>;
