#pragma once

#include "Services.h"
#include "Conversions.h"
#include "StringView.h"
#include "Serializers.h"

namespace logger
{
    VideoServices* set_backend(VideoServices*);

    SerializeMode set_mode(SerializeMode);
    SerializeMode get_mode();

    Color set_color(Color);

    class ScopedColor {
    public:
        ScopedColor(Color c) : m_saved_color(set_color(c)) { }
        ~ScopedColor() { set_color(m_saved_color); }

    private:
        Color m_saved_color;
    };

    void write(StringView);

    template <typename T>
    void do_log_one(StringView& pattern, const T& arg)
    {
        auto open_brace = pattern.find("{").value();
        auto close_brace = pattern.find("}", open_brace + 1).value();

        auto specifier_view = StringView(pattern.data() + open_brace + 1, pattern.data() + close_brace);
        auto pre_arg_view = StringView(pattern, open_brace);

        if (!pre_arg_view.empty())
            write(pre_arg_view);

        auto old_mode = get_mode();

        SerializeAttributes attributes {};

        if (specifier_view.contains("x") || specifier_view.contains("X"))
            attributes.mode = SerializeMode::HEX;
        else if (specifier_view.contains("d") || specifier_view.contains("D"))
            attributes.mode = SerializeMode::DEC;

        serialize(&write, arg, attributes);

        set_mode(old_mode);

        pattern.offset_by(close_brace + 1);
    }

    template <typename... Args>
    void do_log(StringView pattern, bool newline, Color color, const Args& ... args)
    {
        ScopedColor sc(color);

        (do_log_one(pattern, args), ...);

        if (!pattern.empty())
            write(pattern);

        if (newline)
            write("\n");
    }
}

template <size_t N>
struct PatternString {
    char characters[N];
    static constexpr size_t size = N;

    constexpr PatternString(const char(&literal)[N])
    {
        for (size_t i = 0; i < N; ++i)
            characters[i] = literal[i];
    }

    constexpr size_t argument_count() const
    {
        constexpr size_t invalid_count = 0xFFFFFFFF;
        size_t count = 0;
        bool open_brace = false;

        for (size_t i = 0; i < size; ++i)
        {
            if (characters[i] == '{') {
                if (open_brace)
                    return invalid_count;

                open_brace = true;
            }

            if (characters[i] == '}') {
                if (!open_brace)
                    return invalid_count;

                open_brace = false;
                count++;
            }
        }

        if (open_brace)
            return invalid_count;

        return count;
    }

    constexpr operator StringView() const { return StringView(characters, N - 1); }
};

template <PatternString Pattern, typename... Args>
void validate_pattern_string(const Args& ... args)
{
    static_assert(Pattern.argument_count() == sizeof...(args), "invalid format string");
}

#define log(pattern, ...)                                                      \
    do {                                                                       \
        validate_pattern_string<pattern>(__VA_ARGS__);                         \
        logger::do_log(pattern, false, Color::GRAY __VA_OPT__(,) __VA_ARGS__); \
    } while (0)

#define logln(pattern, ...)                                                   \
    do {                                                                      \
        validate_pattern_string<pattern>(__VA_ARGS__);                        \
        logger::do_log(pattern, true, Color::GRAY __VA_OPT__(,) __VA_ARGS__); \
    } while (0)

#define warnln(pattern, ...)                                                    \
    do {                                                                        \
        validate_pattern_string<pattern>(__VA_ARGS__);                          \
        logger::do_log(pattern, true, Color::YELLOW __VA_OPT__(,) __VA_ARGS__); \
    } while (0)

#define errorln(pattern, ...)                                                \
    do {                                                                     \
        validate_pattern_string<pattern>(__VA_ARGS__);                       \
        logger::do_log(pattern, true, Color::RED __VA_OPT__(,) __VA_ARGS__); \
    } while (0)

#define warn(pattern, ...)                                                       \
    do {                                                                         \
        validate_pattern_string<pattern>(__VA_ARGS__);                           \
        logger::do_log(pattern, false, Color::YELLOW __VA_OPT__(,) __VA_ARGS__); \
    } while (0)

#define error(pattern, ...)                                                   \
    do {                                                                      \
        validate_pattern_string<pattern>(__VA_ARGS__);                        \
        logger::do_log(pattern, false, Color::RED __VA_OPT__(,) __VA_ARGS__); \
    } while (0)
