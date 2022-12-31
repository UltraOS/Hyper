#pragma once

#include "helpers.h"

#define COMPARE(x, y, op) ((x) op (y) ? (x) : (y))

#define RUNTIME_COMPARE(x, y, x_name, y_name, op) ({ \
        typeof(x) x_name = x;                        \
        typeof(y) y_name = y;                        \
        COMPARE(x_name, y_name, op);                 \
    })

#define DO_COMPARE(x, y, op)  \
    __builtin_choose_expr(__builtin_constant_p(x) && __builtin_constant_p(y),                         \
                          COMPARE(x, y, op),                                                          \
                          RUNTIME_COMPARE(x, y, CONCAT(ux, __COUNTER__), CONCAT(uy, __COUNTER__), op))

#define MIN(x, y) DO_COMPARE(x, y, <)
#define MAX(x, y) DO_COMPARE(x, y, >)
