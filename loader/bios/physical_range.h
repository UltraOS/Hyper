#pragma once

#include "common/types.h"
#include "common/range.h"
#include "services.h"

struct physical_range {
    union {
        struct range r;
        struct {
            u64 begin;
            u64 end;
        };
    };

    u64 type;
};

struct shatter_result {
    struct physical_range ranges[3];
};

void physical_ranges_shatter(const struct physical_range *lhs, const struct physical_range *rhs,
                             struct shatter_result *out, bool invert_priority);


static inline bool physical_range_is_free(const struct physical_range *range)
{
    return range->type == MEMORY_TYPE_FREE;
}

static inline bool physical_range_is_reserved(const struct physical_range *range)
{
    return range->type == MEMORY_TYPE_RESERVED;
}
