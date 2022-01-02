#include "types.h"
#include "format.h"
#include "string.h"
#include "minmax.h"

struct fmt_buf_state {
    char* buffer;
    size_t capacity;
    size_t bytes_written;
};

struct fmt_spec {
    unsigned base;
    bool is_signed;
    bool prepend_sign;
    bool uppercase;
};

static void write_one(struct fmt_buf_state* fb_state, char c)
{
    if (fb_state->bytes_written < fb_state->capacity)
        fb_state->buffer[fb_state->bytes_written] = c;

    fb_state->bytes_written++;
}

static void write_many(struct fmt_buf_state* fb_state, const char* string, size_t count)
{
    if (fb_state->bytes_written < fb_state->capacity) {
        size_t count_to_write = MIN(count, fb_state->capacity - fb_state->bytes_written);
        memcpy(&fb_state->buffer[fb_state->bytes_written], string, count_to_write);
    }

    fb_state->bytes_written += count;
}

static const char upper_hex[] = "0123456789ABCDEF";
static const char lower_hex[] = "0123456789abcdef";

static char hex_char(bool upper, unsigned long long value)
{
    const char* set = upper ? upper_hex : lower_hex;

    return set[value];
}

#define REPR_BUFFER_SIZE 32

static void write_integer(struct fmt_buf_state* fb_state, struct fmt_spec* fm, unsigned long long value)
{
    char repr_buffer[REPR_BUFFER_SIZE];
    size_t index = REPR_BUFFER_SIZE;
    unsigned long long remainder;
    char repr;
    bool negative = false;

    if (fm->is_signed) {
        long long as_ll = value;

        if (as_ll < 0) {
            value = -as_ll;
            negative = true;
        }
    }

    if (fm->prepend_sign || negative)
        write_one(fb_state, negative ? '-' : '+');

    if (!value) {
        write_one(fb_state, '0');
        return;
    }

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

    write_many(fb_state, &repr_buffer[index], REPR_BUFFER_SIZE - index);
}

static bool string_has_at_least(const char* string, size_t characters)
{
    while (*string) {
        if (--characters == 0)
            return true;

        string++;
    }

    return false;
}

static bool compare_memory(const void* left, const void* right, size_t size)
{
    const unsigned char* lhs = left;
    const unsigned char* rhs = right;

    for (size_t i = 0; i < size; ++i) {
        if (lhs[i] != rhs[i])
            return false;
    }

    return true;
}

static bool consume(const char** string, const char* token)
{
    size_t token_size = strlen(token);

    if (!string_has_at_least(*string, token_size))
        return false;

    if (compare_memory(*string, token, token_size)) {
        *string += token_size;
        return true;
    }

    return false;
}

static bool is_one_of(char c, const char* list)
{
    for (; *list; list++) {
        if (c == *list)
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

static const char* find_next_conversion(const char* fmt, size_t* offset)
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

int vsnprintf(char *restrict buffer, size_t capacity, const char *fmt, va_list vlist)
{
    struct fmt_buf_state fb_state = {
        .buffer = buffer,
        .capacity = capacity,
        .bytes_written = 0
    };

    struct fmt_spec fm = {
        .base = 10,
        .is_signed = false,
        .prepend_sign = false
    };

    unsigned long long value;
    const char* next_conversion;
    size_t next_offset;

    while (*fmt) {
        next_conversion = find_next_conversion(fmt, &next_offset);

        if (next_offset)
            write_many(&fb_state, fmt, next_offset);

        if (!next_conversion)
            break;

        fmt = next_conversion;
        if (consume(&fmt, "%%")) {
            write_one(&fb_state, "%");
            continue;
        }

        fmt++;

        fm.prepend_sign = consume(&fmt, "+");

        if (consume(&fmt, "s")) {
            const char* string = va_arg(vlist, char*);
            write_many(&fb_state, string, strlen(string));
            continue;
        }

        if (consume(&fmt, "p")) {
            value = va_arg(vlist, void*);
            fm.base = 16;
        } else if (consume(&fmt, "c")) {
            value = (unsigned char)va_arg(vlist, int);
        } else if (consume(&fmt, "hh")) {
            if (consume(&fmt, "d") || consume(&fmt, "i")) {
                value = va_arg(vlist, signed char);
                fm.is_signed = true;
            } else if (is_one_of(*fmt, "oxXu")) {
                value = va_arg(vlist, unsigned char);
                fm.base = base_from_specifier(*fmt);
                fm.uppercase = is_uppercase_specifier(*fmt);
                fmt++;
            } else {
                return -1;
            }
        } else if (consume(&fmt, "h")) {
            if (consume(&fmt, "d") || consume(&fmt, "i")) {
                value = va_arg(vlist, short);
                fm.is_signed = true;
            } else if (is_one_of(*fmt, "oxXu")) {
                value = va_arg(vlist, unsigned short);
                fm.base = base_from_specifier(*fmt);
                fm.uppercase = is_uppercase_specifier(*fmt);
                fmt++;
            } else {
                return -1;
            }
        } else if (consume(&fmt, "ll") || (sizeof(size_t) == sizeof(long long) && consume(&fmt, "z"))) {
            if (consume(&fmt, "d") || consume(&fmt, "i")) {
                value = va_arg(vlist, long long);
                fm.is_signed = true;
            } else if (is_one_of(*fmt, "oxXu")) {
                value = va_arg(vlist, unsigned long long);
                fm.base = base_from_specifier(*fmt);
                fm.uppercase = is_uppercase_specifier(*fmt);
                fmt++;
            } else {
                return -1;
            }
        } else if (consume(&fmt, "l") || (sizeof(size_t) == sizeof(long) && consume(&fmt, "z"))) {
            if (consume(&fmt, "d") || consume(&fmt, "i")) {
                value = va_arg(vlist, long);
                fm.is_signed = true;
            } else if (is_one_of(*fmt, "oxXu")) {
                value = va_arg(vlist, unsigned long);
                fm.base = base_from_specifier(*fmt);
                fm.uppercase = is_uppercase_specifier(*fmt);
                fmt++;
            } else {
                return -1;
            }
        } else {
            if (consume(&fmt, "d") || consume(&fmt, "i")) {
                value = va_arg(vlist, int);
                fm.is_signed = true;
            } else if (is_one_of(*fmt, "oxXu")) {
                value = va_arg(vlist, unsigned int);
                fm.base = base_from_specifier(*fmt);
                fm.uppercase = is_uppercase_specifier(*fmt);
                fmt++;
            } else {
                return -1;
            }
        }

        write_integer(&fb_state, &fm, value);

        fm = (struct fmt_spec) {
            .base = 10,
            .is_signed = false,
            .prepend_sign = false
        };
    }

    if (fb_state.capacity) {
        size_t last_char = MIN(fb_state.bytes_written, fb_state.capacity - 1);
        fb_state.buffer[last_char] = '\0';
    }

    return fb_state.bytes_written;
}
