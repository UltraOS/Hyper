#pragma once

#include "common/types.h"
#include "common/constants.h"

struct block_cache {
    bool (*refill_blocks_cb)(void *user_ptr, void *buf, u64 block, size_t count);

    void *user_ptr;

    void *cache_buf;
    size_t cache_block_cap;
    u64 cache_base;

    u32 nocopy_refs;

    u16 block_size;
    u8 block_shift;

#define BC_EMPTY (1 << 0)
    u8 flags;
};

void block_cache_init(struct block_cache *bc, bool (*refill_blocks_cb)(void*, void*, u64, size_t),
                      void *user_ptr, u8 block_shift, void *cache_buf, size_t buf_block_cap);

/*
 * Refill the cache with blocks starting at 'base_block'
 */
bool block_cache_refill(struct block_cache *bc, u64 base_block);

/*
 * Read data at 'byte_off' with 'count' bytes and store in 'buf'.
 * bc->refill_blocks_cb() is called as needed to satisfy the request.
 */
bool block_cache_read(struct block_cache *bc, void *buf, u64 byte_off, size_t count);

/*
 * Cache data at 'byte_off' with 'count' and return the pointer to the internal
 * buffer where the cached data at 'byte_off' is located.
 */
bool block_cache_take_ref(struct block_cache *bc, void **buf, u64 byte_off, size_t count);
void block_cache_release_ref(struct block_cache *bc);
