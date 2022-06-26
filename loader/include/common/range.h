#pragma once

#include "types.h"

struct range {
    u64 begin;
    u64 end;
};

static inline void range_advance_begin(struct range *range, u64 by)
{
    range->begin += by;
}

static inline void range_set_length(struct range *range, u64 length)
{
    range->end = range->begin + length;
}

static inline u64 range_length(struct range *range)
{
    return range->end - range->begin;
}
