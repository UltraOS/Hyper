#pragma once

#include "Runtime.h"
#include "Traits.h"

template <typename T>
remove_reference_t<T>&& move(T&& value)
{
    return static_cast<remove_reference_t<T>&&>(value);
}

template <typename T>
T&& forward(remove_reference_t<T>& value)
{
    return static_cast<T&&>(value);
}

template <typename T>
T&& forward(remove_reference_t<T>&& value)
{
    return static_cast<T&&>(value);
}

template <typename T>
void swap(T& l, T& r)
{
    T tmp = move(l);
    l = move(r);
    r = move(tmp);
}

template <typename T = void>
struct Less {
    constexpr bool operator()(const T& l, const T& r) const
    {
        return l < r;
    }
};

template <>
struct Less<void> {
    using is_transparent = void;

    template <typename L, typename R>
    constexpr auto operator()(L&& l, R&& r) const -> decltype(static_cast<L&&>(l) < static_cast<R&&>(r))
    {
        return static_cast<L&&>(l) < static_cast<R&&>(r);
    }
};

template <typename ItrT, typename U, typename Comparator = Less<>>
ItrT lower_bound(ItrT begin, ItrT end, const U& key, Comparator comparator = Comparator())
{
    ASSERT(begin <= end);

    if (begin == end)
        return end;

    auto* lower_bound = end;

    ssize_t left = 0;
    ssize_t right = end - begin - 1;

    while (left <= right) {
        ssize_t pivot = left + (right - left) / 2;

        auto& pivot_value = begin[pivot];

        if (comparator(key, pivot_value)) {
            lower_bound = &pivot_value;
            right = pivot - 1;
            continue;
        }

        if (comparator(pivot_value, key)) {
            left = pivot + 1;
            continue;
        }

        return &begin[pivot];
    }

    return lower_bound;
}

template <typename T, typename Comparator = Less<T>>
void insertion_sort(T* begin, T* end, Comparator comparator = Comparator())
{
    ASSERT(begin <= end);

    // 1 element or empty array, doesn't make sense to sort
    if (end - begin < 2)
        return;

    size_t array_size = end - begin;

    for (size_t i = 0; i < array_size; ++i) {
        auto j = i;

        while (j > 0 && comparator(begin[j], begin[j - 1])) {
            swap(begin[j], begin[j - 1]);
            --j;
        }
    }
}

inline void set_memory(void* ptr, size_t size, u8 value)
{
    auto* byte_ptr = reinterpret_cast<u8*>(ptr);

    for (size_t i = 0; i < size; ++i) {
        byte_ptr[i] = value;
    }
}

inline void zero_memory(void* ptr, size_t size)
{
    set_memory(ptr, size, 0);
}

inline void copy_memory(const void* source, void* destination, size_t size)
{
    const u8* byte_src = reinterpret_cast<const u8*>(source);
    u8* byte_dst = reinterpret_cast<u8*>(destination);

    while (size--)
        *byte_dst++ = *byte_src++;
}

inline void move_memory(const void* source, void* destination, size_t size)
{
    const u8* byte_src = reinterpret_cast<const u8*>(source);
    u8* byte_dst = reinterpret_cast<u8*>(destination);

    if (source < destination) {
        byte_src += size;
        byte_dst += size;

        while (size--)
            *--byte_dst = *--byte_src;
    } else
        copy_memory(source, destination, size);
}

template <typename T>
constexpr const T& max(const T& l, const T& r)
{
    return l < r ? r : l;
}

template <typename T>
constexpr const T& min(const T& l, const T& r)
{
    return l < r ? l : r;
}

inline Address real_mode_address(u16 segment, u16 offset)
{
    return (static_cast<u32>(segment) << 4) | offset;
}

struct RealModeAddress {
    u16 segment;
    u16 offset;
};

inline RealModeAddress as_real_mode_address(Address address)
{
    ASSERT(address < 1 * MB);

    u16 offset = address & 0xF;
    u16 segment = (address & 0xFFFF0) >> 4;

    return { segment, offset };
}

static constexpr size_t page_size = 4096;
static constexpr uint64_t page_alignment_mask = 0xFFFFFFFFFFFFF000;

static constexpr uint64_t page_round_down(uint64_t size)
{
    return size & ~page_alignment_mask ? size & page_alignment_mask : size;
}

static constexpr uint64_t page_round_up(uint64_t size)
{
    if (size == 0)
        return page_size;

    return size & ~page_alignment_mask ? (size + page_size) & page_alignment_mask : size;
}

template <typename T>
enable_if_t<is_integral_v<T>, T> ceiling_divide(T l, T r)
{
    return !!l + ((l - !!l) / r);
}

template <typename To, typename From>
enable_if_t<
    sizeof(To) == sizeof(From) &&
    is_trivially_constructible_v<To> &&
    is_trivially_copyable_v<To> &&
    is_trivially_copyable_v<From>,
    To
>
bit_cast(const From& value)
{
    To to;
    copy_memory(&value, &to, sizeof(value));
    return to;
}
