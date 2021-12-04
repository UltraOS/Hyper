#pragma once

#include "StringView.h"

using write_callback_t = void(*)(StringView);

enum class SerializeMode {
    DEC,
    HEX
};
static constexpr auto hex = SerializeMode::HEX;
static constexpr auto dec = SerializeMode::DEC;

struct SerializeAttributes {
    SerializeMode mode;
};

template <typename T>
enable_if_t<is_arithmetic_v<T>> serialize(write_callback_t write_cb, T number, const SerializeAttributes& attributes)
{
    static constexpr size_t buffer_size = 32;
    char number_buffer[buffer_size];
    size_t chars_written = 0;

    if (attributes.mode == SerializeMode::HEX)
        chars_written = to_hex_string(number, number_buffer, buffer_size, false);
    else
        chars_written = to_string(number, number_buffer, buffer_size, false);

    write_cb(StringView(number_buffer, chars_written));
}

template <typename T>
void serialize(write_callback_t write_cb, BasicAddress<T> address, const SerializeAttributes&)
{
    static constexpr size_t buffer_size = 32;
    char number_buffer[buffer_size];
    size_t chars_written = to_hex_string(address.raw(), number_buffer, buffer_size, false);

    write_cb(StringView(number_buffer, chars_written));
}

template <typename T>
enable_if_t<is_pointer_v<T>> serialize(write_callback_t write_cb, T pointer, const SerializeAttributes& attributes)
{
    serialize(write_cb, Address(pointer), attributes);
}

inline void serialize(write_callback_t write_cb, const char* string, const SerializeAttributes&)
{
    write_cb(string);
}

inline void serialize(write_callback_t write_cb, StringView string_view, const SerializeAttributes&)
{
    write_cb(string_view);
}
