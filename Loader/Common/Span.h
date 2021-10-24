#pragma once

#include "Types.h"
#include "Runtime.h"

template <typename T>
class Span {
public:
    Span() = default;

    Span(T* data, size_t size)
        : m_data(data)
        , m_size(size)
    {
    }

    template <size_t N>
    Span(T (&array)[N])
        : m_data(array)
        , m_size(N)
    {
    }

    size_t size() const { return m_size; }

    T* data() { return m_data; }
    const T* data() const { return m_data; }

    T& at(size_t index)
    {
        ASSERT(index < m_size);
        return m_data[index];
    }

    const T& at(size_t index) const
    {
        ASSERT(index < m_size);
        return m_data[index];
    }

    T& operator[](size_t index) { return at(index); }
    const T& operator[](size_t index) const { return at(index); }

    T& back()
    {
        ASSERT(!empty());
        return m_data[m_size - 1];
    }

    const T& back() const
    {
        ASSERT(!empty());
        return m_data[m_size - 1];
    }

    T& front()
    {
        ASSERT(!empty());
        return m_data[0];
    }

    const T& front() const
    {
        ASSERT(!empty());
        return m_data[0];
    }

    T* begin() { return m_data; }
    const T* begin() const { return m_data; }

    T* end() { return m_data + m_size; }
    const T* end() const { return m_data + m_size; }

    bool empty() { return m_size == 0; }

    void reset_with(T* data, size_t size)
    {
        m_data = data;
        m_size = size;
    }

    void reset()
    {
        m_data = nullptr;
        m_size = 0;
    }

    void expand_by(size_t size)
    {
        m_size += size;
    }

private:
    T* m_data;
    size_t m_size;
};
