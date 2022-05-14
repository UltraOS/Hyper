#pragma once

#include "panic.h"
#include "attributes.h"

#define BUILD_BUG_ON_WITH_MSG(expr, msg) _Static_assert(!(expr), msg)
#define BUILD_BUG_ON(expr) BUILD_BUG_ON_WITH_MSG(expr, "BUILD BUG: " #expr " evaluated to true")

#define BUG() panic("BUG! At %s() in file %s:%d\n", __func__, __FILE__, __LINE__)
#define DIE() panic("Unrecoverable error! At %s() in file %s:%d\n", __func__, __FILE__, __LINE__)

#define BUG_ON(expr)        \
    do {                    \
        if (unlikely(expr)) \
            BUG();          \
    } while (0)

#define DIE_ON(expr)        \
    do {                    \
        if (unlikely(expr)) \
            DIE();          \
    } while (0)

#define DIE_UNLESS(expr)       \
    do {                       \
        if (unlikely(!(expr))) \
            DIE();             \
    } while (0)
