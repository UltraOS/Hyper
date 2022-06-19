#include "common/string.h"
#include "common/log.h"
#include "common/minmax.h"
#include "common/constants.h"
#include "common/bug.h"

#include "memory_services.h"
#include "services_impl.h"

#define MC_DEBUG 0

static u32 known_standard_mask = 0xFFFFFFFF;
static bool map_is_dirty = true;

#define MAKE_KNOWN_MASK(type)              (1u << (type))
#define KNOWS_MEMORY_TYPE_LOADER_RECLAIM   MAKE_KNOWN_MASK(31)

static bool mask_is_set(u64 type_mask)
{
    return (known_standard_mask & type_mask) != 0;
}

void mm_declare_known_mm_types(u64 *types)
{
    u32 new_mask = 0;

    for (;;) {
        u64 type = *types++;

        if (type == MEMORY_TYPE_INVALID)
            break;

        if (type == MEMORY_TYPE_LOADER_RECLAIMABLE) {
            new_mask |= KNOWS_MEMORY_TYPE_LOADER_RECLAIM;
            continue;
        }

        BUG_ON(type > MEMORY_TYPE_MAX);
        new_mask |= MAKE_KNOWN_MASK(type);
    }

    if (new_mask != known_standard_mask) {
        map_is_dirty = true;
        known_standard_mask = new_mask;
    }

    // These must always be set
    BUG_ON(!mask_is_set(MAKE_KNOWN_MASK(MEMORY_TYPE_FREE) |
                        MAKE_KNOWN_MASK(MEMORY_TYPE_RESERVED)));
}

static u64 mme_resolve_type(struct memory_map_entry *entry)
{
    if (entry->type >= MEMORY_TYPE_PROTO_SPECIFIC_BASE)
        return entry->type;

    if (entry->type == MEMORY_TYPE_LOADER_RECLAIMABLE) {
        if (mask_is_set(KNOWS_MEMORY_TYPE_LOADER_RECLAIM))
            return entry->type;

        return MEMORY_TYPE_FREE;
    }

    BUG_ON(entry->type > MEMORY_TYPE_MAX);

    if (mask_is_set(MAKE_KNOWN_MASK(entry->type)))
        return entry->type;

    return MEMORY_TYPE_RESERVED;
}

/*
 * Overlap resolution between memory map entries:
 * - The winning range is determined by its type.
 * - The higher type value always wins.
 *
 * Note that this only applies to the unaltered memory map as returned
 * by the firmware. The map is also expected to be sorted beforehand.
 * ---------------------------------------
 * RHS wins:
 * 1. LHS gets a part before RHS, if any.
 * 2. RHS stays as is.
 * 3. LHS gets a part after RHS, if any.
 * ----------------------------------------
 * LHS wins:
 * 1. LHS stays as is.
 * 2. RHS gets a part after LHS, if any.
 * ---------------------------------------
 * If a free range ends up being under a page in size after overlap resolution,
 * it gets removed from the memory map entirely.
 */

struct overlap_resolution {
    struct memory_map_entry entries[3];
    u8 entry_count;
};

bool mme_is_valid(struct memory_map_entry *me)
{
    if (!me->size_in_bytes)
        return false;

    if (me->type != MEMORY_TYPE_FREE)
        return true;

    return me->size_in_bytes >= PAGE_SIZE;
}

void mme_align_if_needed(struct memory_map_entry *me)
{
    u64 aligned_begin, aligned_size;

    if (me->type != MEMORY_TYPE_FREE)
        return;

    aligned_begin = ALIGN_DOWN(me->physical_address, PAGE_SIZE);

    aligned_size = me->size_in_bytes;
    if (me->physical_address != aligned_begin)
        aligned_size -= MIN(me->size_in_bytes, me->physical_address - aligned_begin);

    aligned_size = ALIGN_DOWN(aligned_size, PAGE_SIZE);

    me->physical_address = aligned_begin;
    me->size_in_bytes = aligned_size;
}

static void do_resolve_rhs_win(struct memory_map_entry *lhs, struct memory_map_entry *rhs,
                               struct overlap_resolution *res)
{
    u64 lhs_end = mme_end(lhs);
    u64 rhs_end = mme_end(rhs);

    res->entries[0] = (struct memory_map_entry) {
        .physical_address = lhs->physical_address,
        .size_in_bytes = rhs->physical_address - lhs->physical_address,
        .type = lhs->type
    };

    res->entries[2] = (struct memory_map_entry) {
        .physical_address = rhs_end,
        .size_in_bytes = (lhs_end > rhs_end) ? lhs_end - rhs_end : 0,
        .type = lhs->type
    };

    mme_align_if_needed(&res->entries[0]);
    mme_align_if_needed(&res->entries[2]);

    res->entry_count = 3;

    if (!mme_is_valid(&res->entries[0])) {
        res->entries[0] = *rhs;
        res->entry_count--;
    } else {
        res->entries[1] = *rhs;
    }

    if (!mme_is_valid(&res->entries[2])) {
        res->entry_count--;
    } else if (res->entry_count != 3) {
        res->entries[res->entry_count - 1] = res->entries[2];
    }
}

static void do_resolve_lhs_win(struct memory_map_entry *lhs, struct memory_map_entry *rhs,
                               struct overlap_resolution *res)
{
    u64 lhs_end = mme_end(lhs);
    u64 rhs_end = mme_end(rhs);

    res->entries[0] = (struct memory_map_entry) {
        .physical_address = lhs->physical_address,
        .size_in_bytes = lhs->size_in_bytes,
        .type = lhs->type
    };

    res->entries[1] = (struct memory_map_entry) {
        .physical_address = lhs_end,
        .size_in_bytes = (lhs_end < rhs_end) ? rhs_end - lhs_end : 0,
        .type = rhs->type
    };
    mme_align_if_needed(&res->entries[1]);

    res->entry_count = 2;

    if (!mme_is_valid(&res->entries[1]))
        res->entry_count--;
}

static void do_resolve_overlap(struct memory_map_entry *lhs, struct memory_map_entry *rhs,
                               struct overlap_resolution *res)
{
    return (rhs->type < lhs->type) ? do_resolve_lhs_win(lhs, rhs, res) :
                                     do_resolve_rhs_win(lhs, rhs, res);
}

void mme_insert(struct memory_map_entry *buf, struct memory_map_entry *me,
                size_t idx, size_t count)
{
    size_t bytes_to_move;
    BUG_ON(idx > count);

    if (idx == count)
        goto range_place;

    bytes_to_move = (count - idx) * sizeof(*me);
    memmove(&buf[idx + 1], &buf[idx], bytes_to_move);

range_place:
    buf[idx] = *me;
}

struct fixup_result {
    bool lhs_type_changed;
    size_t new_count;
};

ssize_t mm_find_first_that_contains(struct memory_map_entry *buf, u64 count,
                                    u64 value, bool allow_one_above)
{
    ssize_t left = 0;
    ssize_t right = count - 1;

    while (left <= right) {
        ssize_t middle = left + ((right - left) / 2);

        if (buf[middle].physical_address < value) {
            left = middle + 1;
        } else if (value < buf[middle].physical_address) {
            right = middle - 1;
        } else {
            return middle;
        }
    }

    // Left is always lower bound, right is always lower bound - 1
    if (right >= 0) {
        struct memory_map_entry *me = &buf[right];

        if (me->physical_address < value && value < mme_end(me))
            return right;
    }

    // Don't return out of bounds range, even if it's lower bound
    if (left == (ssize_t)count)
        left = -1;

    // Either return the lower bound range (aka one after "value") or none
    return allow_one_above ? left : -1;
}

// returns the number of new ranges inserted
static int mme_insert_try_merge(struct memory_map_entry *buf, struct memory_map_entry *me,
                                size_t count, size_t cap)
{
    ssize_t res = mm_find_first_that_contains(buf, count, me->physical_address, true);
    struct memory_map_entry *target_me;
    u64 this_end = mme_end(me);
    u64 target_end;

    if (res < 0) {
        res = count;
        goto out_no_merge;
    }

    target_me = &buf[res];
    target_end = mme_end(target_me);

    /*
     * There's a small chance that we might be able to merge the entry
     * with target, thus avoiding memmove and entry count increase.
     */
    if (me->physical_address < target_me->physical_address) {
        // This range overlaps target
        if (target_me->physical_address <= this_end && target_me->type == me->type) {
            target_me->physical_address = me->physical_address;
            target_me->size_in_bytes = MAX(this_end, target_end) - target_me->physical_address;
            return 0;
        }

        if (res == 0)
            goto out_no_merge;

        // previous range might overlap this range
        target_me = &buf[--res];
        target_end = mme_end(target_me);

        if (me->physical_address <= target_end && me->type == target_me->type) {
            target_me->size_in_bytes = MAX(this_end, target_end) - target_me->physical_address;
            return 0;
        }

        // Nothing to merge, insert this range at lower bound.
        ++res;
    } else if (target_me->type == me->type) {
        target_me->size_in_bytes = MAX(this_end, target_end) - target_me->physical_address;
        return 0;
    }

// Slow path, nothing to merge. Insert the extra range & increase count.
out_no_merge:
    OOPS_ON(count >= cap);
    mme_insert(buf, me, res, count);
    return 1;
}

static void mm_overlap_fixup(struct memory_map_entry *buf, size_t lhs_idx, size_t rhs_idx,
                             size_t count, size_t cap, struct fixup_result *res)
{
    struct memory_map_entry *lhs = &buf[lhs_idx];
    struct memory_map_entry *rhs = &buf[rhs_idx];
    struct overlap_resolution or;

    /*
     * Overlaps between loader/protocol allocated memory are a fatal error.
     * This basically implies a bug in firmware allocator or some memory corruption.
     */
    BUG_ON(lhs->type > MEMORY_TYPE_MAX || rhs->type > MEMORY_TYPE_MAX);

    do_resolve_overlap(lhs, rhs, &or);

    if (MC_DEBUG) {
        int i;
        print_info("resolved overlap with %d range(s):\n", or.entry_count);

        for (i = 0; i < or.entry_count; ++i) {
            print_info("entry[%d]: " MM_ENT_FMT "\n", i,
                       MM_ENT_PRT(&or.entries[i]));
        }

        print_info("\n");
    }

    res->new_count = count - 1;

    if (or.entries[0].type != lhs->type)
        res->lhs_type_changed = true;

    *lhs = or.entries[0];

    if (or.entry_count >= 2) {
        *rhs = or.entries[1];
        res->new_count++;
    }

    if (or.entry_count == 3) {
        res->new_count += mme_insert_try_merge(buf + rhs_idx, &or.entries[2],
                                               count - rhs_idx, cap - rhs_idx);
    }
}

#define MM_FIXUP_DIE_ON_OVERLAP 0

static size_t mm_do_fixup(struct memory_map_entry *buf, size_t count, size_t buf_cap)
{
    size_t i, j;
    struct memory_map_entry *this, *next;
    u64 this_end;

    j = 0;
    i = 1;

    while (i < count) {
        this = &buf[j];
        next = &buf[i];
        this_end = mme_end(this);

        if (this_end > next->physical_address) {
            struct fixup_result fr;

            print_warn("detected overlapping physical ranges:\n"
                       MM_ENT_FMT"\n"MM_ENT_FMT"\n",
                       MM_ENT_PRT(this), MM_ENT_PRT(next));
            DIE_UNLESS(buf_cap != MM_FIXUP_DIE_ON_OVERLAP);

            mm_overlap_fixup(buf, j, i, count, buf_cap, &fr);

            // Both ranges collapsed into one, skip rhs
            if (fr.new_count < count)
                ++i;

            count = fr.new_count;
            if (fr.lhs_type_changed && j != 0) {
                --j;
                --i;
                buf[i] = buf[j + 1];
            }

            continue;
        }

        this->type = mme_resolve_type(this);
        next->type = mme_resolve_type(next);
        ++i;

        if (this->type != next->type || this_end != next->physical_address) {
            ++j;

            // This range is just after
            if (j == i - 1)
                continue;

            buf[j] = *next;
            continue;
        }

        print_dbg(MC_DEBUG, "merging ranges:\n"MM_ENT_FMT"\n"MM_ENT_FMT"\n",
                  MM_ENT_PRT(this), MM_ENT_PRT(next));

        this->size_in_bytes += next->size_in_bytes;

        print_dbg(MC_DEBUG, "merged as: "MM_ENT_FMT"\n\n",
                  MM_ENT_PRT(this));
    }

    return j + 1;
}

size_t mm_fixup(struct memory_map_entry *buf, size_t count, size_t cap, u8 flags)
{
    u64 known_mask_prev;
    size_t ret;
    bool merge_reclaim = flags & FIXUP_NO_PRESERVE_LOADER_RECLAIM;
    BUG_ON(count == 0);

    if (flags & FIXUP_UNSORTED)
        mm_sort(buf, count);

    if ((flags & FIXUP_IF_DIRTY) && !map_is_dirty)
        return count;

    // This is a no-op
    if (merge_reclaim && mask_is_set(KNOWS_MEMORY_TYPE_LOADER_RECLAIM) && !map_is_dirty)
        return count;

    known_mask_prev = known_standard_mask;
    if (!merge_reclaim)
        known_standard_mask |= KNOWS_MEMORY_TYPE_LOADER_RECLAIM;

    if (!(flags & FIXUP_OVERLAP_RESOLVE))
        cap = MM_FIXUP_DIE_ON_OVERLAP;

    ret = mm_do_fixup(buf, count, cap);
    known_standard_mask = known_mask_prev;
    map_is_dirty = false;

    return ret;
}

void mm_sort(struct memory_map_entry *buf, size_t count)
{
    size_t i, j;

    for (i = 0; i < count; ++i) {
        j = i;

        while (j) {
            struct memory_map_entry *rhs = &buf[j];
            struct memory_map_entry *lhs = &buf[j - 1];

            if (lhs->physical_address <= rhs->physical_address)
                break;

            struct memory_map_entry tmp = *rhs;
            *rhs = *lhs;
            *lhs = tmp;
            --j;
        }
    }
}

inline const char *mme_type_to_str(struct memory_map_entry *me)
{
    switch (me->type) {
    case MEMORY_TYPE_INVALID:
        return "<invalid>";
    case MEMORY_TYPE_FREE:
        return "free";
    case MEMORY_TYPE_RESERVED:
        return "reserved";
    case MEMORY_TYPE_ACPI_RECLAIMABLE:
        return "ACPI-reclaim";
    case MEMORY_TYPE_NVS:
        return "NVS";
    case MEMORY_TYPE_UNUSABLE:
        return "unusable";
    case MEMORY_TYPE_DISABLED:
        return "disabled";
    case MEMORY_TYPE_PERSISTENT:
        return "persistent";
    case MEMORY_TYPE_LOADER_RECLAIMABLE:
        return "loader-reclaim";
    default:
        BUG_ON(me->type < MEMORY_TYPE_PROTO_SPECIFIC_BASE);
        return "<proto-specific>";
    }
}
