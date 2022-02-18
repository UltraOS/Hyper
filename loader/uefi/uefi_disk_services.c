#include "uefi_disk_services.h"
#include "uefi_globals.h"
#include "uefi_helpers.h"
#include "structures.h"
#include "common/log.h"

#undef MSG_FMT
#define MSG_FMT(msg) "UEFI-IO: " msg

struct uefi_disk_handle {
    EFI_BLOCK_IO_PROTOCOL *bio;
    EFI_DISK_IO_PROTOCOL *dio;
};

static struct disk *disks;
static size_t disk_count;

static struct disk *uefi_list_disks(size_t *count)
{
    *count = disk_count;
    return disks;
}

static bool uefi_read(void *handle, void *buffer, u64 offset, size_t bytes)
{
    struct uefi_disk_handle *h = handle;
    EFI_STATUS ret;

    BUG_ON(!handle);

    if (!h->dio) {
        print_warn("unable to read blocks, no EFI_DISK_IO_PROTOCOL for disk\n");
        return false;
    }

    ret = h->dio->ReadDisk(h->dio, h->bio->Media->MediaId, offset, bytes, buffer);
    if (unlikely_efi_error(ret)) {
        struct string_view err_msg = uefi_status_to_string(ret);
        print_warn("ReadDisk() failed: %pSV\n", &err_msg);
        return false;
    }

    return true;
}

static bool uefi_read_blocks(void *handle, void *buffer, u64 sector, size_t blocks)
{
    struct uefi_disk_handle *h = handle;
    EFI_STATUS ret;

    BUG_ON(!handle);

    ret = h->bio->ReadBlocks(h->bio, h->bio->Media->MediaId, sector, blocks * h->bio->Media->BlockSize, buffer);
    if (unlikely_efi_error(ret)) {
        struct string_view err_msg = uefi_status_to_string(ret);
        print_warn("ReadDisk() failed: %pSV\n", &err_msg);
        return false;
    }

    return true;
}

static struct disk_services uefi_disk_services = {
    .list_disks = uefi_list_disks,
    .read = uefi_read,
    .read_blocks = uefi_read_blocks
};

static void enumerate_disks()
{
    EFI_HANDLE *handles;
    EFI_GUID block_io_guid = EFI_BLOCK_IO_PROTOCOL_GUID;
    EFI_GUID disk_io_guid = EFI_DISK_IO_PROTOCOL_GUID;
    EFI_STATUS ret;
    UINTN i, handle_count;

    if (unlikely(!uefi_get_protocol_handles(&block_io_guid, &handles, &handle_count))) {
        print_warn("no block-io handles found\n");
        return;
    }

    if (unlikely(!uefi_pool_alloc(EfiLoaderData, sizeof(struct disk_services),
                  handle_count, (void**)&disks)))
        return;

    for (i = 0; i < handle_count; ++i) {
        EFI_BLOCK_IO_PROTOCOL *bio = NULL;
        EFI_DISK_IO_PROTOCOL *dio = NULL;
        struct uefi_disk_handle *dh;

        ret = g_st->BootServices->HandleProtocol(handles[i], &block_io_guid, (void**)&bio);
        if (unlikely_efi_error(ret)) {
            struct string_view err_msg = uefi_status_to_string(ret);
            print_warn("disk[%zu] HandleProtocol(block_io) error: %pSV\n", i, &err_msg);
            continue;
        }

        if (unlikely(!bio->Media))
            continue;
        if (!bio->Media->MediaPresent || bio->Media->LogicalPartition || !bio->Media->LastBlock)
            continue;

        ret = g_st->BootServices->HandleProtocol(handles[i], &disk_io_guid, (void**)&dio);
        if (unlikely_efi_error(ret)) {
            struct string_view err_msg = uefi_status_to_string(ret);
            print_warn("disk[%zu] HandleProtocol(disk_io) error: %pSV\n", i, &err_msg);
        }

        /*
         * Don't reset the drive:
         * - It's slow (even the non-extended version)
         * - It sometimes hangs on buggy firmware
         * - Not very useful overall
         */

        // TODO: I don't like that we have to allocate the handle separately, maybe redesign this
        if (unlikely(!uefi_pool_alloc(EfiLoaderData, sizeof(struct uefi_disk_handle), 1, (void**)&dh)))
            return;

        dh->bio = bio;
        dh->dio = dio;

        disks[disk_count++] = (struct disk) {
            .sectors = bio->Media->LastBlock + 1,
            .bytes_per_sector = bio->Media->BlockSize,
            .handle = dh
        };

        print_info("detected disk: block-size %u, %llu blocks\n",
                   bio->Media->BlockSize, bio->Media->LastBlock + 1);
    }
}

struct disk_services *disk_services_init()
{
    enumerate_disks();

    return &uefi_disk_services;
}
