#include "common/align.h"
#include "common/minmax.h"

#include "filesystem/bulk_read.h"

struct bulk_read_req {
    struct file *f;

    void *buf;
    u64 file_off;
    u32 bytes;

    u16 fs_block_mask;
    u16 disk_block_mask;
};

static inline size_t br_wanted_block_count(struct bulk_read_req *br)
{
    size_t bytes = (br->file_off & br->fs_block_mask) + br->bytes;
    bytes = ALIGN_UP(bytes, br->fs_block_mask + 1);

    return bytes >> file_block_shift(br->f);
}

static bool do_bulk_read(struct bulk_read_req *br, file_get_range_t get_range)
{
    struct filesystem *fs = br->f->fs;
    struct disk *d = &fs->d;
    struct block_range out_range;
    u32 bytes_in_range, file_off_in_block;
    size_t want_blocks;
    u64 file_block;

    while (br->bytes) {
        want_blocks = br_wanted_block_count(br);
        file_off_in_block = br->file_off & br->fs_block_mask;
        file_block = br->file_off >> fs->block_shift;

        if (!get_range(br->f, file_block, want_blocks, &out_range))
            return false;

        BUG_ON(out_range.blocks == 0);
        bytes_in_range = (out_range.blocks << fs->block_shift) - file_off_in_block;
        bytes_in_range = MIN(bytes_in_range, br->bytes);

        if (is_block_range_hole(&out_range)) {
            memset(br->buf, 0, bytes_in_range);
            goto next;
        }

        out_range.part_byte_off += file_off_in_block;

        // Request is unaligned to disk block, do a bounce buffer read
        if ((out_range.part_byte_off & br->disk_block_mask) ||
            (bytes_in_range & br->disk_block_mask))
        {
            u64 full_off = fs->lba_range.begin << d->block_shift;
            full_off += out_range.part_byte_off;

            if (!ds_read(d->handle, br->buf, full_off, bytes_in_range))
                return false;
        } else {
            u64 full_off = fs->lba_range.begin;
            full_off += out_range.part_byte_off >> d->block_shift;

            if (!ds_read_blocks(d->handle, br->buf, full_off, bytes_in_range >> d->block_shift))
                return false;
        }

    next:
        br->buf += bytes_in_range;
        br->file_off += bytes_in_range;
        br->bytes -= bytes_in_range;
    }

    return true;
}

bool bulk_read_file(struct file* f, void *buffer, u64 offset, u32 bytes,
                    file_get_range_t get_range)
{
    struct filesystem *fs = f->fs;
    struct disk *d = &fs->d;
    u64 parts[3];
    u8 block_shift;
    u16 block_mask, block_size;
    size_t i;

    struct bulk_read_req br = {
        .f = f,
        .buf = buffer,
        .file_off = offset,
        .disk_block_mask = (1 << d->block_shift) - 1,
        .fs_block_mask = (1 << fs->block_shift) - 1,
    };

    fs_check_read(f, offset, bytes);

    block_shift = fs->block_shift;
    if (unlikely(fs->block_shift < d->block_shift))
        block_shift = d->block_shift;

    block_mask = (1 << block_shift) - 1;
    block_size = 1 << block_shift;

    // Calculate unaligned head bytes
    parts[0] = offset & block_mask;
    if (parts[0])
        parts[0] = MIN(block_size - parts[0], bytes);

    bytes -= parts[0];

    // Calculate unaligned tail bytes
    parts[2] = bytes & block_mask;
    bytes -= parts[2];

    parts[1] = bytes;

    for (i = 0; i < 3; ++i) {
        if (!parts[i])
            continue;

        br.bytes = parts[i];
        if (!do_bulk_read(&br, get_range))
            return false;
    }

    return true;
}
