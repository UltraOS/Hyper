#pragma once
#include "filesystem.h"

/*
 * Used by the filesystems that allow sparse holes inside files as a space
 * optimization. The entire range is considered zero-filled and no read
 * request is issued to the block device for such block ranges.
 */
#define BLOCK_RANGE_OFF_HOLE 0xFFFFFFFFFFFFFFFF

struct block_range {
    u64 part_byte_off;
    size_t blocks;
};

static inline bool is_block_range_hole(struct block_range *br)
{
    return br->part_byte_off == BLOCK_RANGE_OFF_HOLE;
}

static inline void block_range_make_hole(struct block_range *br)
{
    br->part_byte_off = BLOCK_RANGE_OFF_HOLE;
}

/*
 * Retrieves a contiguous range of blocks in a file at an offset
 * 'file_block_off' up to 'want_blocks' in size (implementations
 * are allowed to return a larger block count).
 * Blocks are calculated & requested in file system block size,
 * disk block size is not taken into account and is handled
 * internally by bulk_read_file.
 */
typedef bool (*file_get_range_t)(struct file*, u64 file_block_off,
                                 size_t want_blocks, struct block_range *out);

bool bulk_read_file(struct file *f, void *buffer, u64 offset, u32 bytes,
                    file_get_range_t get_range);
