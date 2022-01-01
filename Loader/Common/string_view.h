#pragma once

#include "common/types.h"
#include "common/bug.h"
#include "common/string.h"

struct string_view {
    const char *text;
    size_t size;
};

#define SV(str)                                     \
    __builtin_constant_p(str) ?                     \
    (struct string_view) { str, sizeof(str) - 1 } : \
    (struct string_view) { str, strlen(str) }

bool sv_equals(struct string_view lhs, struct string_view rhs);
bool sv_starts_with(struct string_view str, struct string_view prefix);
ssize_t sv_find(struct string_view str, struct string_view needle, size_t starting_at);

static inline bool sv_empty(struct string_view str)
{
    return str.size == 0;
}

static inline bool sv_contains(struct string_view str, struct string_view needle)
{
    return sv_find(str, needle, 0) >= 0;
}

static inline void sv_offset_by(struct string_view *str, size_t value)
{
    BUG_ON(str->size < value);
    str->text += value;
    str->size -= value;
}

static inline void sv_extend_by(struct string_view *str, size_t value)
{
    BUG_ON(!str->text);
    str->size += value;
}

static inline void sv_clear(struct string_view *str)
{
    str->text = NULL;
    str->size = 0;
}

static inline bool sv_pop_one(struct string_view *str, char *c)
{
    if (sv_empty(*str))
        return false;

    *c = str->text[0];
    sv_offset_by(str, 1);
    return true;
}
