#pragma once

#include "types.h"

/*
 * This header contains string.h functions that are not part of the hyper
 * loader, and that I am not planning to add there to prevent undesired bloat.
 */

static inline int strcmp(const char *lhs, const char *rhs)
{
    size_t i = 0;
    typedef const unsigned char *cucp;

    while (lhs[i] && rhs[i]) {
        if (lhs[i] != rhs[i])
            return *(cucp)&lhs[i] - *(cucp)&rhs[i];

        i++;
    }

    return *(cucp)&lhs[i] - *(cucp)&rhs[i];
}
