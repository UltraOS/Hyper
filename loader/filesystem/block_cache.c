#include "block_cache.h"
#include "common/minmax.h"
#include "common/string.h"

void block_cache_init(struct block_cache *bc, bool (*refill_blocks_cb)(void*, void*, u64, size_t),
                      void *user_ptr, u8 block_shift, void *cache_buf, size_t buf_block_cap)
{
    *bc = (struct block_cache) {
        .refill_blocks_cb = refill_blocks_cb,
        .user_ptr = user_ptr,
        .cache_buf = cache_buf,
        .cache_block_cap = buf_block_cap,
        .block_size = 1 << block_shift,
        .block_shift = block_shift,
        .flags = BC_EMPTY,
    };
}

struct cached_span {
    u64 blocks;
    void *data;
};

static bool cached_span_from_block(struct block_cache *bc, u64 base_block, struct cached_span *out_span)
{
    size_t cache_off;

    if (bc->flags & BC_EMPTY)
        return false;

    if (base_block < bc->cache_base)
        return false;

    cache_off = base_block - bc->cache_base;
    if (cache_off >= bc->cache_block_cap)
        return false;

    out_span->blocks = bc->cache_block_cap - cache_off;
    out_span->data = bc->cache_buf + (cache_off << bc->block_shift);
    return true;
}

static bool cached_range_get_ptr(struct block_cache *bc, void **buf, u64 base_block, size_t count)
{
    struct cached_span cs;

    if (!cached_span_from_block(bc, base_block, &cs))
        return false;
    if (cs.blocks < count)
        return false;

    *buf = cs.data;
    return true;
}

bool block_cache_refill(struct block_cache *bc, u64 base_block)
{
    // Already cached at this base
    if (bc->cache_base == base_block && !(bc->flags & BC_EMPTY))
        return true;

    // Some dangling references still alive
    BUG_ON(bc->nocopy_refs != 0);

    if (!bc->refill_blocks_cb(bc->user_ptr, bc->cache_buf, base_block, bc->cache_block_cap)) {
        bc->flags |= BC_EMPTY;
        return false;
    }

    bc->flags &= ~BC_EMPTY;
    bc->cache_base = base_block;
    return true;
}

struct block_coords {
    u64 base_block;
    size_t first_block_byte_off;
    size_t block_count;
};

static void block_coords_offset_by(struct block_coords *bc, u64 blocks)
{
    bc->block_count -= blocks;
    bc->base_block += blocks;
    bc->first_block_byte_off = 0;
}

static void byte_offsets_to_block_coords(struct block_cache *bc, u64 byte_off, size_t byte_cnt,
                                         struct block_coords *out)
{
    BUG_ON(byte_cnt == 0);

    out->base_block = byte_off >> bc->block_shift;
    out->first_block_byte_off = byte_off & (bc->block_size - 1);

    out->block_count = ALIGN_UP(out->first_block_byte_off + byte_cnt, bc->block_size);
    out->block_count >>= bc->block_shift;
}

struct block_req {
    struct block_coords coords;
    void *buf;
    size_t bytes_to_copy;
};

enum completion_result {
    CR_NONE,
    CR_PARTIAL,
    CR_FULL
};

static enum completion_result block_cache_try_complete_req(struct block_cache *bc, struct block_req *br)
{
    size_t bytes_to_copy;
    struct cached_span cs;
    if (!cached_span_from_block(bc, br->coords.base_block, &cs))
        return CR_NONE;

    cs.blocks = MIN(br->coords.block_count, cs.blocks);
    bytes_to_copy = MIN(cs.blocks << bc->block_shift, br->bytes_to_copy);

    memcpy(br->buf, cs.data + br->coords.first_block_byte_off, bytes_to_copy);

    block_coords_offset_by(&br->coords, cs.blocks);
    br->buf += bytes_to_copy;
    br->bytes_to_copy -= bytes_to_copy;

    return br->bytes_to_copy == 0 ? CR_FULL : CR_PARTIAL;
}

static bool req_exec(struct block_cache *bc, struct block_req *br)
{
    for (;;) {
        enum completion_result res = block_cache_try_complete_req(bc, br);

        if (res == CR_FULL)
            return true;

        if (!block_cache_refill(bc, br->coords.base_block))
            return false;
    }
}

bool block_cache_read(struct block_cache *bc, void *buf, u64 byte_off, size_t count)
{
    struct block_req br = {
        .buf = buf,
        .bytes_to_copy = count
    };
    byte_offsets_to_block_coords(bc, byte_off, count, &br.coords);

    return req_exec(bc, &br);
}

bool block_cache_read_blocks(struct block_cache *bc, void *buf, u64 block, size_t count)
{
    struct block_req br;

    // No reason to make this request go through cache
    if (count > bc->cache_block_cap && (bc->flags & BC_DIRECT_IO)) {
        if (bc->refill_blocks_cb(bc->user_ptr, buf, block, count))
            return true;

        // Attempt a bounce buffer read if the call above fails, since
        // the failure could be caused by the alignment being too low
        // or block count being too high
    }

    br = (struct block_req) {
        .coords = {
            .base_block = block,
            .block_count = count
        },
        .buf = buf,
        .bytes_to_copy = count << bc->block_shift,
    };

    return req_exec(bc, &br);
}

bool block_cache_take_ref(struct block_cache *bc, void **buf, u64 byte_off, size_t count)
{
    struct block_coords c;
    byte_offsets_to_block_coords(bc, byte_off, count, &c);

    // Fast path if this range is already entirely cached
    if (cached_range_get_ptr(bc, buf, c.base_block, c.block_count)) {
        *buf += c.first_block_byte_off;
        goto out;
    }

    if (!block_cache_refill(bc, c.base_block))
        return false;

    *buf = bc->cache_buf + c.first_block_byte_off;

out:
    bc->nocopy_refs++;
    return true;
}

void block_cache_release_ref(struct block_cache *bc)
{
    BUG_ON(bc->nocopy_refs == 0);
    bc->nocopy_refs--;
}
