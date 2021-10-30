#pragma once

#include "Services.h"
#include "Conversions.h"
#include "StringView.h"

namespace logger
{
    // Sets new backend to use for logging, returns the previous backend
    // or nullptr if none was set.
    VideoServices* set_backend(VideoServices*);

    enum class Mode {
        DEC,
        HEX
    };
    static constexpr auto hex = Mode::HEX;
    static constexpr auto dec = Mode::DEC;

    void set_mode(Mode);
    Mode get_mode();

    void log(Color color, StringView);

    inline void log(Mode mode)
    {
        set_mode(mode);
    }

    inline void log(Color, Mode mode)
    {
        set_mode(mode);
    }

    inline void log(StringView string)
    {
        log(Color::GRAY, string);
    }

    inline void log(const char* string)
    {
        log(Color::GRAY, StringView(string));
    }

    inline void log(Color color, const char* string)
    {
        log(color, StringView(string));
    }

    template <typename T>
    enable_if_t<is_arithmetic_v<T>> log(Color color, T number)
    {
        static constexpr size_t buffer_size = 32;
        char number_buffer[buffer_size];
        size_t chars_written = 0;

        if (get_mode() == Mode::HEX)
            chars_written = to_hex_string(number, number_buffer, buffer_size, false);
        else
            chars_written = to_string(number, number_buffer, buffer_size, false);

        log(color, StringView(number_buffer, chars_written));
    }

    template <typename T>
    enable_if_t<is_arithmetic_v<T>> log(T number)
    {
        log(Color::GRAY, number);
    }

    template <typename T>
    void log(Color color, BasicAddress<T> address)
    {
        static constexpr size_t buffer_size = 32;
        char number_buffer[buffer_size];
        size_t chars_written = to_hex_string(address.raw(), number_buffer, buffer_size, false);

        log(color, StringView(number_buffer, chars_written));
    }

    template <typename T>
    void log(BasicAddress<T> address)
    {
        log(Color::GRAY, address);
    }

    template <typename T>
    enable_if_t<is_pointer_v<T>> log(Color color, T pointer)
    {
        log(color, Address(pointer));
    }

    template <typename T>
    enable_if_t<is_pointer_v<T>> log(T pointer)
    {
        log(Color::GRAY, Address(pointer));
    }

    template <typename... Args>
    void log(Color color, const Args& ... args)
    {
        (log(color, args), ...);
        set_mode(Mode::DEC);
    }

    template <typename... Args>
    void log(const Args& ... args)
    {
        (log(Color::GRAY, args), ...);
        set_mode(Mode::DEC);
    }

    template <typename... Args>
    void info(const Args& ... args)
    {
        log(Color::GRAY, "INFO: ");
        (log(Color::GRAY, args), ...);
        log(Color::GRAY, "\n");
        set_mode(Mode::DEC);
    }

    template <typename... Args>
    void warning(const Args& ... args)
    {
        log(Color::YELLOW, "WARNING: ");
        (log(Color::YELLOW, args), ...);
        log(Color::YELLOW, "\n");
        set_mode(Mode::DEC);
    }

    template <typename... Args>
    void error(const Args& ... args)
    {
        log(Color::RED, "ERROR: ");
        (log(Color::RED, args), ...);
        log(Color::RED, "\n");
        set_mode(Mode::DEC);
    }
}
