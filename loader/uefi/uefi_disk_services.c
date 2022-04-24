#define MSG_FMT(msg) "UEFI-IO: " msg

#include "common/log.h"
#include "uefi_disk_services.h"
#include "uefi_globals.h"
#include "uefi_helpers.h"
#include "uefi_structures.h"
#include "disk_services.h"
#include "services_impl.h"

struct uefi_disk {
    u64 sectors;
    u32 id;
    u8 block_shift;
    u8 status;
    EFI_BLOCK_IO_PROTOCOL *bio;
    EFI_DISK_IO_PROTOCOL *dio;
};

static struct uefi_disk *disks;
static size_t disk_count;

u32 ds_get_disk_count(void)
{
    SERVICE_FUNCTION();

    return (u32)disk_count;
}

void ds_query_disk(size_t idx, struct disk *out_disk)
{
    SERVICE_FUNCTION();

    struct uefi_disk *d;
    BUG_ON(idx >= disk_count);

    d = &disks[idx];

    *out_disk = (struct disk) {
        .sectors = d->sectors,
        .handle = d,
        .id = d->id,
        .block_shift = d->block_shift,
        .status = d->status
    };
}

bool ds_read(void *handle, void *buffer, u64 offset, size_t bytes)
{
    SERVICE_FUNCTION();

    struct uefi_disk *d = handle;
    EFI_STATUS ret;

    BUG_ON(!handle);

    if (!d->dio) {
        print_warn("unable to read blocks, no EFI_DISK_IO_PROTOCOL for disk\n");
        return false;
    }

    ret = d->dio->ReadDisk(d->dio, d->bio->Media->MediaId, offset, bytes, buffer);
    if (unlikely_efi_error(ret)) {
        struct string_view err_msg = uefi_status_to_string(ret);
        print_warn("ReadDisk() failed: %pSV\n", &err_msg);
        return false;
    }

    return true;
}

bool ds_read_blocks(void *handle, void *buffer, u64 sector, size_t blocks)
{
    SERVICE_FUNCTION();

    struct uefi_disk *d;
    UINT32 media_id, io_align;
    EFI_BLOCK_IO_PROTOCOL *bio;
    EFI_STATUS ret;

    BUG_ON(!handle);
    d = handle;
    bio = d->bio;
    media_id = bio->Media->MediaId;
    io_align = bio->Media->IoAlign;

    if (io_align > 1 && ((ptr_t)buffer % io_align)) {
        print_warn("buffer 0x%016llX is unaligned to minimum IoAlign (%u), attempting a DISK_IO read instead!\n",
                   (ptr_t)buffer, io_align);
        return ds_read(handle, buffer, sector << d->block_shift, blocks << d->block_shift);
    }

    ret = d->bio->ReadBlocks(bio, media_id, sector, blocks << d->block_shift, buffer);
    if (unlikely_efi_error(ret)) {
        struct string_view err_msg = uefi_status_to_string(ret);
        print_warn("ReadDisk() failed: %pSV\n", &err_msg);
        return false;
    }

    return true;
}

static void enumerate_disks(void)
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

    if (unlikely(!uefi_pool_alloc(EfiLoaderData, sizeof(struct uefi_disk),
                 handle_count, (void**)&disks)))
        return;

    for (i = 0; i < handle_count; ++i) {
        EFI_BLOCK_IO_PROTOCOL *bio = NULL;
        EFI_DISK_IO_PROTOCOL *dio = NULL;

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

        if (unlikely(__builtin_popcount(bio->Media->BlockSize) != 1)) {
            print_warn("Skipping a non-power-of-two block size (%u) disk\n", bio->Media->BlockSize);
            continue;
        }

        ret = g_st->BootServices->HandleProtocol(handles[i], &disk_io_guid, (void**)&dio);
        if (unlikely_efi_error(ret)) {
            struct string_view err_msg = uefi_status_to_string(ret);
            print_warn("disk[%zu] HandleProtocol(DISK_IO) error: %pSV\n", i, &err_msg);
        }

        /*
         * Don't reset the drive:
         * - It's slow (even the non-extended version)
         * - It sometimes hangs on buggy firmware
         * - Not very useful overall
         */

        disks[disk_count++] = (struct uefi_disk) {
            .sectors = bio->Media->LastBlock + 1,
            .id = i,
            .block_shift = __builtin_ffs(bio->Media->BlockSize) - 1,
            .status = bio->Media->RemovableMedia ? DISK_STS_REMOVABLE : 0,
            .bio = bio,
            .dio = dio
        };

        print_info("detected disk: block-size %u, %llu blocks\n",
                   bio->Media->BlockSize, bio->Media->LastBlock + 1);
    }
}

void uefi_disk_services_init(void)
{
    enumerate_disks();
}
