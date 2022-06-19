#define MSG_FMT(msg) "UEFI-IO: " msg

#include "common/log.h"
#include "common/align.h"
#include "uefi_disk_services.h"
#include "uefi_globals.h"
#include "uefi_helpers.h"
#include "uefi_structures.h"
#include "disk_services.h"
#include "filesystem/block_cache.h"
#include "allocator.h"
#include "services_impl.h"

struct uefi_disk {
    u64 sectors;
    u32 id;
    u8 status;
    EFI_BLOCK_IO_PROTOCOL *bio;
    EFI_DISK_IO_PROTOCOL *dio;
    struct block_cache bc;
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
        .block_shift = d->bc.block_shift,
        .status = d->status
    };
}

static void uefi_trace_read_error(struct uefi_disk *d, EFI_STATUS ret, u64 sector,
                                  size_t blocks, bool is_block_io)
{
    struct string_view err_msg = uefi_status_to_string(ret);

    print_warn("%s(%u, %llu, %zu) failed: '%pSV'\n",
               is_block_io ? "ReadBlocks" : "ReadDisk",
               d->id, sector, blocks, &err_msg);
}

static bool uefi_refill_blocks(void *handle, void *buffer, u64 sector, size_t blocks)
{
    struct uefi_disk *d;
    UINT32 media_id, io_align;
    EFI_BLOCK_IO_PROTOCOL *bio;
    EFI_DISK_IO_PROTOCOL *dio;
    u8 block_shift;
    EFI_STATUS ret;

    BUG_ON(!handle);
    d = handle;

    block_shift = d->bc.block_shift;
    bio = d->bio;
    dio = d->dio;
    io_align = bio->Media->IoAlign;
    media_id = bio->Media->MediaId;

    if (io_align && !IS_ALIGNED((ptr_t)buffer, io_align)) {
        print_warn("buffer %p not aligned to %u, attempting a DISK_IO read instead\n",
                   buffer, io_align);

        if (!dio) {
            print_warn("failing the read as DISK_IO is unavailable\n");
            return false;
        }

        ret = dio->ReadDisk(dio, media_id, sector << block_shift, blocks << block_shift, buffer);
        if (unlikely_efi_error(ret)) {
            uefi_trace_read_error(d, ret, sector, blocks, false);
            return false;
        }

        return true;
    }

    ret = bio->ReadBlocks(bio, media_id, sector, blocks << block_shift, buffer);
    if (unlikely_efi_error(ret)) {
        uefi_trace_read_error(d, ret, sector, blocks, true);
        return false;
    }

    return true;
}

bool ds_read(void *handle, void *buffer, u64 offset, size_t bytes)
{
    SERVICE_FUNCTION();
    BUG_ON(!handle);

    struct uefi_disk *d = handle;
    return block_cache_read(&d->bc, buffer, offset, bytes);
}

bool ds_read_blocks(void *handle, void *buffer, u64 sector, size_t blocks)
{
    SERVICE_FUNCTION();
    BUG_ON(!handle);

    struct uefi_disk *d = handle;
    return block_cache_read_blocks(&d->bc, buffer, sector, blocks);
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
        struct uefi_disk *d;
        void *buf;
        u8 block_shift;

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

        d = &disks[disk_count++];
        d->bio = bio;
        d->dio = dio;
        d->id = i;
        d->status = bio->Media->RemovableMedia ? DISK_STS_REMOVABLE : 0;
        d->sectors = bio->Media->LastBlock + 1;
        block_shift = __builtin_ctz(bio->Media->BlockSize);

        buf = allocate_critical_pages_with_type(1, MEMORY_TYPE_LOADER_RECLAIMABLE);

        block_cache_init(&d->bc, uefi_refill_blocks, d,
                         block_shift,
                         buf, PAGE_SIZE >> block_shift);
        block_cache_enable_direct_io(&d->bc);

        print_info("detected disk: block-size %u, %llu blocks\n",
                   bio->Media->BlockSize, bio->Media->LastBlock + 1);
    }
}

void uefi_disk_services_init(void)
{
    enumerate_disks();
}
