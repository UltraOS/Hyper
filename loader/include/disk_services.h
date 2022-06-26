#pragma once

#include "common/types.h"

#define DISK_STS_REMOVABLE (1 << 0)

struct disk {
    u64 sectors;
    void *handle;
    u32 id;

    u8 block_shift;
    u8 status;
};

static inline u32 disk_block_size(const struct disk *d)
{
    return 1 << d->block_shift;
}

/*
 * Number of disks that can be queried.
 */
u32 ds_get_disk_count(void);

/*
 * Retrieves information about a disk at idx.
 * idx -> disk to retrieve.
 * out_disk -> pointer to data that receives disk information.
 */
void ds_query_disk(size_t idx, struct disk *out_disk);

/*
 * Reads byte aligned data from disk.
 * handle -> one of disk handles returned by list_disks.
 * buffer -> first byte of the buffer that receives data.
 * offset -> byte offset of where to start reading.
 * bytes -> number of bytes to read.
 * Returns true if data was read successfully, false otherwise.
 */
bool ds_read(void *handle, void *buffer, u64 offset, size_t bytes);

/*
 * Reads sectors from a disk.
 * handle -> one of disk handles returned by list_disks.
 * buffer -> first byte of the buffer that receives data.
 * sector -> first sector from which data is read.
 * count -> number of sectors to read.
 * Returns true if data was read successfully, false otherwise.
 */
bool ds_read_blocks(void *handle, void *buffer, u64 sector, size_t blocks);
