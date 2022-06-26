#pragma once

#include "common/string.h"

struct guid {
    u32 data1;
    u16 data2;
    u16 data3;
    u8  data4[8];
};

static inline int guid_compare(const struct guid *lhs, const struct guid *rhs)
{
    return memcmp(lhs, rhs, sizeof(*rhs));
}
