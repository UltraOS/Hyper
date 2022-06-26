#include "common/types.h"
#include "common/format.h"
#include "common/conversions.h"
#include "common/string.h"
#include "common/minmax.h"
#include "common/string_view.h"

struct fmt_buf_state {
    char *buffer;
    size_t capacity;
    size_t bytes_written;
};

struct fmt_spec {
    bool is_signed      : 1;
    bool prepend        : 1;
    bool uppercase      : 1;
    bool left_justify   : 1;
    bool alternate_form : 1;
    char pad_char;
    char prepend_char;
    u32 min_width;
    unsigned base;
};

static void write_one(struct fmt_buf_state *fb_state, char c)
{
    if (fb_state->bytes_written < fb_state->capacity)
        fb_state->buffer[fb_state->bytes_written] = c;

    fb_state->bytes_written++;
}

static void write_many(struct fmt_buf_state *fb_state, const char *string, size_t count)
{
    if (fb_state->bytes_written < fb_state->capacity) {
        size_t count_to_write = MIN(count, fb_state->capacity - fb_state->bytes_written);
        memcpy(&fb_state->buffer[fb_state->bytes_written], string, count_to_write);
    }

    fb_state->bytes_written += count;
}

static char hex_char(bool upper, unsigned long long value)
{
    static const char upper_hex[] = "0123456789ABCDEF";
    static const char lower_hex[] = "0123456789abcdef";
    const char *set = upper ? upper_hex : lower_hex;

    return set[value];
}

static void write_padding(struct fmt_buf_state *fb_state, struct fmt_spec *fm, size_t repr_size)
{
    size_t mw = fm->min_width;

    if (mw <= repr_size)
        return;

    mw -= repr_size;

    while (mw--)
        write_one(fb_state, fm->left_justify ? ' ' : fm->pad_char);
}

#define REPR_BUFFER_SIZE 32

static void write_integer(struct fmt_buf_state *fb_state, struct fmt_spec *fm, unsigned long long value)
{
    char repr_buffer[REPR_BUFFER_SIZE];
    size_t index = REPR_BUFFER_SIZE;
    unsigned long long remainder;
    char repr;
    bool negative = false;
    size_t repr_size;

    if (fm->is_signed) {
        long long as_ll = value;

        if (as_ll < 0) {
            value = -as_ll;
            negative = true;
        }
    }

    if (fm->prepend || negative)
        write_one(fb_state, negative ? '-' : fm->prepend_char);

    while (value) {
        remainder = value % fm->base;
        value /= fm->base;

        if (fm->base == 16) {
            repr = hex_char(fm->uppercase, remainder);
        } else if (fm->base == 8 || fm->base == 10) {
            repr = remainder + '0';
        } else {
            repr = '?';
        }

        repr_buffer[--index] = repr;
    }
    repr_size = REPR_BUFFER_SIZE - index;

    if (repr_size == 0) {
        repr_buffer[--index] = '0';
        repr_size = 1;
    }

    if (fm->alternate_form) {
        if (fm->base == 16) {
            repr_buffer[--index] = fm->uppercase ? 'X' : 'x';
            repr_buffer[--index] = '0';
            repr_size += 2;
        } else if (fm->base == 8) {
            repr_buffer[--index] = '0';
            repr_size += 1;
        }
    }

    if (fm->left_justify) {
        write_many(fb_state, &repr_buffer[index], repr_size);
        write_padding(fb_state, fm, repr_size);
    } else {
        write_padding(fb_state, fm, repr_size);
        write_many(fb_state, &repr_buffer[index], repr_size);
    }
}

static bool string_has_at_least(const char *string, size_t characters)
{
    while (*string) {
        if (--characters == 0)
            return true;

        string++;
    }

    return false;
}

static bool consume_digits(const char **string, struct string_view *digit_view)
{
    digit_view->text = *string;
    digit_view->size = 0;

    for (;;) {
        char c = **string;
        if (c < '0' || c > '9')
            return digit_view->size != 0;

        sv_extend_by(digit_view, 1);
        *string += 1;
    }
}

static bool consume(const char **string, const char *token)
{
    size_t token_size = strlen(token);

    if (!string_has_at_least(*string, token_size))
        return false;

    if (!memcmp(*string, token, token_size)) {
        *string += token_size;
        return true;
    }

    return false;
}

static bool is_one_of(char c, const char *list)
{
    for (; *list; list++) {
        if (c == *list)
            return true;
    }

    return false;
}

static bool consume_one_of(const char **string, const char *list, char *consumed_char)
{
    char c = **string;
    if (!c)
        return false;

    if (is_one_of(c, list)) {
        *consumed_char = c;
        *string += 1;
        return true;
    }

    return false;
}

static unsigned base_from_specifier(char specifier)
{
    switch (specifier)
    {
        case 'x':
        case 'X':
            return 16;
        case 'o':
            return 8;
        default:
            return 10;
    }
}

static bool is_uppercase_specifier(char specifier)
{
    return specifier == 'X';
}

static const char *find_next_conversion(const char *fmt, size_t *offset)
{
    *offset = 0;

    while (*fmt) {
        if (*fmt == '%')
            return fmt;

        fmt++;
        *offset += 1;
    }

    return NULL;
}

int vsnprintf(char *buffer, size_t capacity, const char *fmt, va_list vlist)
{
    struct fmt_buf_state fb_state = {
        .buffer = buffer,
        .capacity = capacity,
        .bytes_written = 0
    };

    struct fmt_spec fm = {
        .pad_char = ' ',
        .base = 10
    };

    unsigned long long value;
    const char *next_conversion;
    size_t next_offset;
    char flag;

    struct string_view digits;

    while (*fmt) {
        next_conversion = find_next_conversion(fmt, &next_offset);

        if (next_offset)
            write_many(&fb_state, fmt, next_offset);

        if (!next_conversion)
            break;

        fmt = next_conversion;
        if (consume(&fmt, "%%")) {
            write_one(&fb_state, '%');
            continue;
        }

        // consume %
        fmt++;

        while (consume_one_of(&fmt, "+- 0#", &flag)) {
            switch (flag) {
            case '+':
            case ' ':
                fm.prepend = true;
                fm.prepend_char = flag;
                continue;
            case '-':
                fm.left_justify = true;
                continue;
            case '0':
                fm.pad_char = '0';
                continue;
            case '#':
                fm.alternate_form = true;
                continue;
            default:
                BUG();
            }
        }

        if (consume_digits(&fmt, &digits)) {
            if (!str_to_u32(digits, &fm.min_width))
                return -1;
        }

        if (consume(&fmt, "c")) {
            char c = va_arg(vlist, int);
            write_one(&fb_state, c);
            continue;
        } else if (consume(&fmt, "s")) {
            const char* string = va_arg(vlist, char*);
            write_many(&fb_state, string, strlen(string));
            continue;
        } else if (consume(&fmt, "p")) {
            if (consume(&fmt, "SV")) {
                struct string_view *str = va_arg(vlist, struct string_view*);
                write_many(&fb_state, str->text, str->size);
                continue;
            }

            value = (ptr_t)va_arg(vlist, void*);
            fm.base = 16;
        } else if (consume(&fmt, "hh")) {
            if (consume(&fmt, "d") || consume(&fmt, "i")) {
                value = (signed char)va_arg(vlist, int);
                fm.is_signed = true;
            } else if (consume_one_of(&fmt, "oxXu", &flag)) {
                value = (unsigned char)va_arg(vlist, int);
                fm.base = base_from_specifier(flag);
                fm.uppercase = is_uppercase_specifier(flag);
            } else {
                return -1;
            }
        } else if (consume(&fmt, "h")) {
            if (consume(&fmt, "d") || consume(&fmt, "i")) {
                value = (short)va_arg(vlist, int);
                fm.is_signed = true;
            } else if (consume_one_of(&fmt, "oxXu", &flag)) {
                value = (unsigned short)va_arg(vlist, int);
                fm.base = base_from_specifier(flag);
                fm.uppercase = is_uppercase_specifier(flag);
            } else {
                return -1;
            }
        } else if (consume(&fmt, "ll") || (sizeof(size_t) == sizeof(long long) && consume(&fmt, "z"))) {
            if (consume(&fmt, "d") || consume(&fmt, "i")) {
                value = va_arg(vlist, long long);
                fm.is_signed = true;
            } else if (consume_one_of(&fmt, "oxXu", &flag)) {
                value = va_arg(vlist, unsigned long long);
                fm.base = base_from_specifier(flag);
                fm.uppercase = is_uppercase_specifier(flag);
            } else {
                return -1;
            }
        } else if (consume(&fmt, "l") || (sizeof(size_t) == sizeof(long) && consume(&fmt, "z"))) {
            if (consume(&fmt, "d") || consume(&fmt, "i")) {
                value = va_arg(vlist, long);
                fm.is_signed = true;
            } else if (consume_one_of(&fmt, "oxXu", &flag)) {
                value = va_arg(vlist, unsigned long);
                fm.base = base_from_specifier(flag);
                fm.uppercase = is_uppercase_specifier(flag);
            } else {
                return -1;
            }
        } else {
            if (consume(&fmt, "d") || consume(&fmt, "i")) {
                value = va_arg(vlist, int);
                fm.is_signed = true;
            } else if (consume_one_of(&fmt, "oxXu", &flag)) {
                value = va_arg(vlist, unsigned int);
                fm.base = base_from_specifier(flag);
                fm.uppercase = is_uppercase_specifier(flag);
            } else {
                return -1;
            }
        }

        write_integer(&fb_state, &fm, value);

        fm = (struct fmt_spec) {
            .base = 10,
            .pad_char = ' '
        };
    }

    if (fb_state.capacity) {
        size_t last_char = MIN(fb_state.bytes_written, fb_state.capacity - 1);
        fb_state.buffer[last_char] = '\0';
    }

    return fb_state.bytes_written;
}
