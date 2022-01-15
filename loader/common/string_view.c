#include "string_view.h"
#include "ctype.h"

bool sv_equals(struct string_view lhs, struct string_view rhs)
{
    if (lhs.size != rhs.size)
        return false;

    for (size_t i = 0; i < lhs.size; ++i) {
        if (lhs.text[i] != rhs.text[i])
            return false;
    }

    return true;
}

bool sv_equals_caseless(struct string_view lhs, struct string_view rhs)
{
    if (lhs.size != rhs.size)
        return false;

    for (size_t i = 0; i < lhs.size; ++i) {
        if (tolower(lhs.text[i]) != tolower(rhs.text[i]))
            return false;
    }

    return true;
}

bool sv_starts_with(struct string_view str, struct string_view prefix)
{
    size_t i;

    if (prefix.size > str.size)
        return false;
    if (prefix.size == 0)
        return true;

    for (i = 0; i < prefix.size; ++i) {
        if (str.text[i] != prefix.text[i])
            return false;
    }

    return true;
}

ssize_t sv_find(struct string_view str, struct string_view needle, size_t starting_at)
{
    size_t i, j, k;

    BUG_ON(starting_at > str.size);

    if (needle.size > (str.size - starting_at))
        return -1;
    if (sv_empty(needle))
        return 0;

    for (i = starting_at; i < str.size - needle.size + 1; ++i) {
        if (str.text[i] != needle.text[0])
            continue;

        j = i;
        k = 0;

        while (k < needle.size) {
            if (str.text[j++] != needle.text[k])
                break;

            k++;
        }

        if (k == needle.size)
            return i;
    }

    return -1;
}
