#pragma once

#include "Traits.h"

template <typename T>
enable_if_t<is_integral_v<T>, size_t> to_string(T number, char* string, size_t max_size, bool null_terminate = true)
{
    bool is_negative = false;
    size_t required_size = 0;

    if (number == 0) {
        if (max_size >= (null_terminate ? 2 : 1)) {
            string[0] = '0';
            if (null_terminate)
                string[1] = '\0';
            return null_terminate ? 2 : 1;
        }

        return 0;
    } else if (number < 0) {
        is_negative = true;
        number = -number;
        ++required_size;
    }

    T copy = number;

    while (copy) {
        copy /= 10;
        ++required_size;
    }

    if (required_size + !!null_terminate > max_size)
        return 0;

    T modulos = 0;
    for (size_t divisor = 0; divisor < required_size - !!is_negative; ++divisor) {
        modulos = number % 10;
        number /= 10;
        string[required_size - (divisor + 1)] = static_cast<char>(modulos + '0');
    }

    if (is_negative)
        string[0] = '-';

    if (null_terminate)
        string[required_size] = '\0';

    return required_size;
}

template <typename T>
enable_if_t<is_integral_v<T>, size_t> to_hex_string(T number, char* string, size_t max_size, bool null_terminate = true)
{
    constexpr auto required_length = sizeof(number) * 2 + 2; // '0x' + 2 chars per hex byte

    if (max_size < required_length + !!null_terminate)
        return 0;

    string[0] = '0';
    string[1] = 'x';

    if (null_terminate)
        string[required_length] = '\0';

    static constexpr auto hex_digits = "0123456789ABCDEF";

    size_t j = 0;
    for (size_t i = 0; j < sizeof(number) * 2 * 4; ++i) {
        string[required_length - i - 1] = hex_digits[(number >> j) & 0x0F];
        j += 4;
    }

    return required_length;
}