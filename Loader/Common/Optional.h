#pragma once

#include "Traits.h"
#include "Runtime.h"
#include "Utilities.h"
#include "Allocator.h"

template <typename T>
class Optional {
public:
    constexpr Optional() = default;

    // TODO: should be remove_cvref
    template <typename U = T,
              typename = typename enable_if<!is_same_v<Optional<T>, remove_reference_t<U>>>::type>
    constexpr Optional(U&& value)
        : m_has_value(true)
    {
        new (pointer()) T(forward<U>(value));
    }

    template <typename... Args>
    constexpr Optional(in_place_t, Args&&... args)
        : m_has_value(true)
    {
        new (pointer()) T(forward<Args>(args)...);
    }

    constexpr Optional(const Optional& other)
        : m_has_value(other.has_value())
    {
        if (has_value())
            new (pointer()) T(other.value());
    }

    constexpr Optional(Optional&& other)
        : m_has_value(other.has_value())
    {
        if (has_value())
            new (pointer()) T(move(other.value()));
    }

    template <typename... Args>
    static Optional create(Args&&... args)
    {
        return Optional<T>(in_place, forward<Args>(args)...);
    }

    template <typename... Args>
    T& emplace(Args&&... args)
    {
        if (has_value())
            reset();

        m_has_value = true;
        new (pointer()) T(forward<Args>(args)...);

        return *pointer();
    }

    constexpr Optional& operator=(const Optional& other)
    {
        if (this == &other)
            return *this;

        // non-null = null
        if (has_value() && !other.has_value()) {
            reset();
            return *this;
        }

        // null == non-null
        if (!has_value() && other.has_value()) {
            m_has_value = true;
            new (pointer()) T(other.value());
            return *this;
        }

        // non-null == non-null
        if (has_value() && other.has_value()) {
            value() = other.value();
            return *this;
        }

        // otherwise neither has value, this operator is a no-op
        return *this;
    }

    constexpr Optional& operator=(Optional&& other)
    {
        if (this == &other)
            return *this;

        // non-null = null
        if (has_value() && !other.has_value()) {
            reset();
            return *this;
        }

        // null = non-null
        if (!has_value() && other.has_value()) {
            m_has_value = true;
            new (pointer()) T(move(other.value()));
            return *this;
        }

        // non-null = non-null
        if (has_value() && other.has_value()) {
            value() = move(other.value());
            return *this;
        }

        // otherwise neither has value, this operator is a no-op
        return *this;
    }

    template <typename U>
    constexpr T value_or(U&& default_value) const&
    {
        if (m_has_value)
            return move(*pointer());

        return static_cast<T>(forward<U>(default_value));
    }

    template <typename U>
    constexpr T value_or(U&& default_value) &&
    {
        if (m_has_value)
            return *pointer();

        return static_cast<T>(forward<U>(default_value));
    }

    constexpr T& value() &
    {
        return *pointer();
    }

    constexpr const T& value() const&
    {
        return *pointer();
    }

    constexpr T&& value() &&
    {
        return move(*pointer());
    }

    constexpr const T&& value() const&&
    {
        return move(*pointer());
    }

    constexpr const T* operator->() const
    {
        return pointer();
    }

    constexpr T* operator->()
    {
        return pointer();
    }

    constexpr const T& operator*() const&
    {
        return *pointer();
    }

    constexpr T& operator*() &
    {
        return *pointer();
    }

    constexpr const T&& operator*() const&&
    {
        return move(*pointer());
    }

    constexpr T&& operator*() &&
    {
        return move(*pointer());
    }

    void reset()
    {
        if (m_has_value)
            pointer()->~T();

        m_has_value = false;
    }

    constexpr explicit operator bool() const
    {
        return m_has_value;
    }

    constexpr bool has_value() const
    {
        return m_has_value;
    }

    ~Optional()
    {
        reset();
    }

private:
    T* pointer()
    {
        ASSERT(m_has_value);
        return reinterpret_cast<T*>(m_value);
    }

    const T* pointer() const
    {
        ASSERT(m_has_value);
        return reinterpret_cast<const T*>(m_value);
    }

private:
    alignas(T) u8 m_value[sizeof(T)];
    bool m_has_value { false };
};
