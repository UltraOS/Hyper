#include "range.h"

void range_align_start(struct range *range, size_t alignment)
{
    u64 remainder = range->begin % alignment;
    u64 aligned_begin = remainder ? range->begin + (alignment - remainder) : range->begin;

    if (aligned_begin < range->begin || aligned_begin >= range->end) {
        *range = (struct range) { 0 };
        return;
    }

    range->begin = aligned_begin;
}

void range_align_length(struct range *range, size_t alignment)
{
    range_set_length(range, range_length(range) & ~(alignment - (u64)1));
}

void range_align(struct range *range, size_t alignment)
{
    range_align_start(range, alignment);
    range_align_length(range, alignment);
}
