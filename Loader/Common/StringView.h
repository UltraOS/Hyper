#pragma once

#include "Types.h"

constexpr inline size_t length_of(const char* ptr)
{
    size_t length = 0;

    while (*(ptr++))
        ++length;

    return length;
}

class StringView {
public:
    constexpr StringView() = default;

    constexpr StringView(const char* string)
        : m_string(string)
        , m_size(length_of(string))
    {
    }

    constexpr StringView(const char* begin, const char* end)
        : m_string(begin)
        , m_size(end - begin)
    {
    }

    constexpr StringView(const char* string, size_t length)
        : m_string(string)
        , m_size(length)
    {
    }

    constexpr StringView(StringView string, size_t length)
        : m_string(string.data())
        , m_size(length)
    {
    }

    template <size_t N>
    static constexpr StringView from_char_array(char (&array)[N])
    {
        return StringView(array, N);
    }

    [[nodiscard]] constexpr const char* begin() const { return data(); }

    [[nodiscard]] constexpr const char* end() const { return data() + m_size; }

    [[nodiscard]] constexpr const char* data() const { return m_string; }

    [[nodiscard]] constexpr size_t size() const { return m_size; }

    [[nodiscard]] constexpr bool empty() const { return m_size == 0; }

    [[nodiscard]] constexpr const char& at(size_t i) const { return data()[i]; }
    [[nodiscard]] const char& operator[](size_t i) const { return at(i); }

private:
    const char* m_string { nullptr };
    size_t m_size { 0 };
};