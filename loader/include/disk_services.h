#pragma once

#include "common/types.h"

#define DISK_STS_REMOVABLE (1 << 0)

enum disk_kind {
    DISK_KIND_HD,
    DISK_KIND_CD,
};

struct disk {
    u64 sectors;
    void *handle;

    // 0-based index within its kind (i.e. hdN / cdN)
    u32 id;
    u8 kind;

    u8 block_shift;
    u8 status;
};

static inline u32 disk_block_size(const struct disk *d)
{
    return 1 << d->block_shift;
}

enum boot_device_type {
    // Booted from a disk; 'disk_id' identifies it
    BOOT_DEVICE_TYPE_DISK,
    // Booted over the network (PXE)
    BOOT_DEVICE_TYPE_PXE,
};

enum boot_partition_id_type {
    // Couldn't determine it (whole-disk boot, or no partition info at all)
    BOOT_PARTITION_ID_TYPE_NONE,
    // A 0-based fs partition index (BIOS: baked into stage2 by the installer)
    BOOT_PARTITION_ID_TYPE_INDEX,
    // An absolute start LBA (UEFI: read from the loaded image device path)
    BOOT_PARTITION_ID_TYPE_LBA,
};

struct boot_device_info {
    enum boot_device_type type;

    // The kind and id (matching struct disk) if BOOT_DEVICE_TYPE_DISK
    u32 disk_id;
    u8 disk_kind;

    /*
     * How to locate the boot partition, and its locator; only meaningful when
     * type == BOOT_DEVICE_TYPE_DISK.
     */
    enum boot_partition_id_type partition_id;
    union {
        u32 partition_index;
        u64 partition_lba;
    };
};

/*
 * Retrieves information about the device the loader was booted from.
 */
bool ds_query_boot_device(struct boot_device_info *out_info);

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
