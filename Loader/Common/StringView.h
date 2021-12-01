#pragma once

#include "Types.h"
#include "Optional.h"

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

    [[nodiscard]] constexpr const char* data() const { return m_string; }
    [[nodiscard]] constexpr const char* begin() const { return m_string; }
    [[nodiscard]] constexpr const char* end() const { return m_string + m_size; }


    [[nodiscard]] constexpr size_t size() const { return m_size; }
    [[nodiscard]] constexpr bool empty() const { return m_size == 0; }

    [[nodiscard]] constexpr char front() const { return m_string[0]; }
    [[nodiscard]] constexpr char back() const { return m_string[m_size ? m_size - 1 : m_size]; }

    [[nodiscard]] constexpr const char& at(size_t i) const { return m_string[i]; }
    [[nodiscard]] const char& operator[](size_t i) const { return at(i); }

    [[nodiscard]] bool starts_with(StringView rhs) const
    {
        if (rhs.size() > size())
            return false;
        if (rhs.empty())
            return true;

        for (size_t i = 0; i < rhs.size(); ++i) {
            if (at(i) != rhs.at(i))
                return false;
        }

        return true;
    }

    Optional<size_t> find(StringView str, size_t starting_at = 0)
    {
        ASSERT(starting_at <= size());

        if (str.size() > (size() - starting_at))
            return {};
        if (str.empty())
            return 0;

        for (size_t i = starting_at; i < size() - str.size() + 1; ++i) {
            if (at(i) != str.at(0))
                continue;

            size_t j = i;
            size_t k = 0;

            while (k < str.size()) {
                if (at(j++) != str.at(k))
                    break;

                k++;
            }

            if (k == str.size())
                return i;
        }

        return {};
    }

    bool contains(StringView string) { return find(string).has_value(); }

    void offset_by(size_t value)
    {
        ASSERT(m_size >= value);

        m_string += value;
        m_size -= value;
    }

    void extend_by(size_t value)
    {
        ASSERT(m_string != nullptr);
        m_size += value;
    }

private:
    const char* m_string { nullptr };
    size_t m_size { 0 };
};

inline bool operator==(StringView lhs, StringView rhs)
{
    if (lhs.size() != rhs.size())
        return false;

    for (size_t i = 0; i < lhs.size(); ++i) {
        if (lhs[i] != rhs[i])
            return false;
    }

    return true;
}

inline bool operator!=(StringView lhs, StringView rhs)
{
    return !operator==(lhs, rhs);
}

inline bool is_upper(char c)
{
    return c >= 'A' && c <= 'Z';
}

inline char to_lower(char c)
{
    static constexpr i32 offset_to_lower = 'a' - 'A';
    static_assert(offset_to_lower > 0, "Negative lower to upper offset");

    if (is_upper(c))
        return c + offset_to_lower;

    return c;
}

inline void to_lower(char* str, size_t size)
{
    for (size_t i = 0; i < size; ++i)
        str[i] = to_lower(str[i]);
}

template <size_t N>
inline void to_lower(char (&array)[N])
{
    to_lower(array, N);
}

