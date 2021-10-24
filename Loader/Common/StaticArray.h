#pragma once

#include "Common/Types.h"
#include "Common/Runtime.h"

template <typename T, size_t Size>
class StaticArray {
    static_assert(Size > 0, "StaticArray size cannot be zero");

public:
    [[nodiscard]] constexpr size_t size() const { return Size; }

    const T& at(size_t index) const
    {
        ASSERT(index < size());
        return m_elements[index];
    }

    T& at(size_t index)
    {
        ASSERT(index < size());
        return m_elements[index];
    }

    const T& operator[](size_t index) const { return at(index); }
    T& operator[](size_t index) { return at(index); }

    const T& back() const { return m_elements[size() - 1]; }
    T& back() { return m_elements[size() - 1]; }

    T* begin() { return m_elements; }
    T* end() { return &back() + 1; }

private:
    T m_elements[Size] {};
};
