#pragma once

#include "types.h"

struct range {
    u64 begin;
    u64 end;
};

void range_align_start(struct range *range, size_t alignment);
void range_align_length(struct range *range, size_t alignment);
void range_align(struct range *range, size_t alignment);

static inline void range_advance_begin(struct range *range, size_t by)
{
    range->begin += by;
}

static inline bool range_contains(const struct range *lhs, const struct range *rhs)
{
    return rhs->begin >= lhs->begin && rhs->end <= lhs->end;
}

static inline bool range_overlaps(const struct range *lhs, const struct range *rhs)
{
    return rhs->begin >= lhs->begin && rhs->begin < lhs->end;
}

static inline bool range_is_empty(const struct range *range)
{
    return (range->end - range->begin) == 0;
}

static inline void range_set_length(struct range *range, size_t length)
{
    range->end = range->begin + length;
}

static inline size_t range_length(struct range *range)
{
    return range->end - range->begin;
}
