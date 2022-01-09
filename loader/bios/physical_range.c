#include "physical_range.h"
#include "common/bug.h"
#include "common/minmax.h"

void physical_ranges_shatter(const struct physical_range *lhs, const struct physical_range *rhs,
                             struct shatter_result *out, bool invert_priority)
{
    const struct range *l = &lhs->r;
    const struct range *r = &rhs->r;

    // cannot shatter against non-overlapping range
    BUG_ON(!range_overlaps(l, r));

    // cut out the overlapping piece by default
    out->ranges[0] = (struct physical_range) {
        .r =  { l->begin, r->begin },
        .type = lhs->type
    };

    // both ranges have the same type, so we can just merge them
    if (lhs->type == rhs->type) {
        out->ranges[0].r.end = MAX(l->end, r->end);
        return;
    }

    // other range is fully inside this range
    if (r->end <= l->end) {
        out->ranges[2] = (struct physical_range) {
            .r = { r->end, l->end },
            .type = lhs->type
        };
    }

    // we cut out the overlapping piece of the other range and make it our type
    if (lhs->type > rhs->type && !invert_priority) {
        out->ranges[0].r.end = l->end;

        if (l->end <= r->end) {
            out->ranges[1] = (struct physical_range) {
                .r = { out->ranges[0].r.end, r->end },
                .type = rhs->type
            };
        } else { // since we swallowed the other range we don't need this
            out->ranges[2] = (struct physical_range) { 0 };
        }
    } else { // our overlapping piece gets cut out and put into the other range
        out->ranges[1] = *rhs;
    }
}
