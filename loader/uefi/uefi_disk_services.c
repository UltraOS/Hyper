#define MSG_FMT(msg) "UEFI-IO: " msg

#include "common/log.h"
#include "common/align.h"
#include "common/string.h"
#include "uefi_disk_services.h"
#include "uefi/globals.h"
#include "uefi/helpers.h"
#include "uefi/structures.h"
#include "disk_services.h"
#include "filesystem/block_cache.h"
#include "allocator.h"
#include "services_impl.h"

struct uefi_disk {
    u64 sectors;
    // 0-based index within its kind (hdN / cdN)
    u32 id;
    // one of enum disk_kind
    u8 kind;
    u8 status;
    EFI_HANDLE handle;
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

static UINTN dp_node_length(EFI_DEVICE_PATH_PROTOCOL *node)
{
    return node->Length[0] | ((UINTN)node->Length[1] << 8);
}

// Size of the device path up to (not including) its terminating END node
static UINTN dp_prefix_size(EFI_DEVICE_PATH_PROTOCOL *dp)
{
    u8 *start = (u8*)dp;

    while (dp->Type != EFI_DEVICE_PATH_TYPE_END) {
        UINTN len = dp_node_length(dp);

        if (len < sizeof(*dp))
            break; // malformed, bail out
        dp = (EFI_DEVICE_PATH_PROTOCOL*)((u8*)dp + len);
    }

    return (u8*)dp - start;
}

static EFI_DEVICE_PATH_PROTOCOL *handle_device_path(EFI_HANDLE handle)
{
    EFI_GUID dp_guid = EFI_DEVICE_PATH_PROTOCOL_GUID;
    EFI_DEVICE_PATH_PROTOCOL *dp = NULL;
    EFI_STATUS ret;

    ret = g_st->BootServices->HandleProtocol(handle, &dp_guid, (void**)&dp);
    if (unlikely_efi_error(ret))
        return NULL;

    return dp;
}

// The subtype of the path's last node if it's a MEDIA node, else -1
static int dp_last_media_subtype(EFI_DEVICE_PATH_PROTOCOL *dp)
{
    int result = -1;

    while (dp->Type != EFI_DEVICE_PATH_TYPE_END) {
        UINTN len = dp_node_length(dp);

        if (len < sizeof(*dp))
            break;

        result = dp->Type == EFI_DEVICE_PATH_TYPE_MEDIA ? dp->SubType : -1;
        dp = (EFI_DEVICE_PATH_PROTOCOL*)((u8*)dp + len);
    }

    return result;
}

// Byte length of the path's last node before the terminating END
static UINTN dp_last_node_length(EFI_DEVICE_PATH_PROTOCOL *dp)
{
    UINTN last = 0;

    while (dp->Type != EFI_DEVICE_PATH_TYPE_END) {
        UINTN len = dp_node_length(dp);

        if (len < sizeof(*dp))
            break;

        last = len;
        dp = (EFI_DEVICE_PATH_PROTOCOL*)((u8*)dp + len);
    }

    return last;
}

static bool dp_subtype_is_partition(int subtype)
{
    return subtype == EFI_DEVICE_PATH_SUBTYPE_HARD_DRIVE ||
           subtype == EFI_DEVICE_PATH_SUBTYPE_CDROM;
}

/*
 * Classify each whole disk as hd/cd: for every handle that's a partition
 * (its device path ends in a HARD_DRIVE/CDROM media node), find its parent
 * disk by device path and mark it accordingly. This doesn't rely on the
 * LogicalPartition flag, which some firmware gets wrong. Disks with no such
 * partition child keep the heuristic guess made earlier.
 */
static void classify_disks(EFI_HANDLE *handles, UINTN handle_count)
{
    UINTN i;

    for (i = 0; i < handle_count; ++i) {
        EFI_DEVICE_PATH_PROTOCOL *dp;
        UINTN parent_len;
        int subtype;
        size_t j;

        dp = handle_device_path(handles[i]);
        if (!dp)
            continue;

        subtype = dp_last_media_subtype(dp);
        if (!dp_subtype_is_partition(subtype))
            continue;

        // The parent disk's path is this partition's path minus the last node
        parent_len = dp_prefix_size(dp) - dp_last_node_length(dp);

        for (j = 0; j < disk_count; ++j) {
            EFI_DEVICE_PATH_PROTOCOL *ddp;

            ddp = handle_device_path(disks[j].handle);
            if (!ddp)
                continue;
            if (dp_prefix_size(ddp) != parent_len)
                continue;
            if (memcmp(ddp, dp, parent_len) != 0)
                continue;

            disks[j].kind = subtype == EFI_DEVICE_PATH_SUBTYPE_CDROM ?
                                DISK_KIND_CD : DISK_KIND_HD;
        }
    }
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
        .kind = d->kind,
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

static void assign_disk_ids(void)
{
    u32 hd_count = 0, cd_count = 0;
    size_t i;

    for (i = 0; i < disk_count; ++i)
        disks[i].id = disks[i].kind == DISK_KIND_CD ? cd_count++
                                                    : hd_count++;
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
        goto out;

    for (i = 0; i < handle_count; ++i) {
        EFI_BLOCK_IO_PROTOCOL *bio = NULL;
        EFI_DISK_IO_PROTOCOL *dio = NULL;
        EFI_DEVICE_PATH_PROTOCOL *dp;
        struct uefi_disk *d;
        void *buf;
        u8 block_shift;

        ret = g_st->BootServices->HandleProtocol(handles[i], &block_io_guid, (void**)&bio);
        if (unlikely_efi_error(ret)) {
            struct string_view err_msg = uefi_status_to_string(ret);
            print_warn("disk[%zu] HandleProtocol(BLOCK_IO) error: %pSV\n", i, &err_msg);
            continue;
        }

        if (unlikely(!bio->Media))
            continue;
        if (!bio->Media->MediaPresent || !bio->Media->LastBlock)
            continue;

        /*
         * iPXE stubs a non-functional Block IO onto its loaded image handle;
         * detect and skip it ("iPXE" magic media id + a 1-byte block size).
         */
        if (bio->Media->MediaId == 0x69505845U && bio->Media->BlockSize == 1)
            continue;

        /*
         * Only whole disks are kept; partitions (device paths ending in a
         * HARD_DRIVE/CDROM media node) are handled by classify_disks(). This is
         * done via the device path rather than the LogicalPartition flag, which
         * some firmware sets incorrectly.
         */
        dp = handle_device_path(handles[i]);
        if (dp && dp_subtype_is_partition(dp_last_media_subtype(dp)))
            continue;

        if (unlikely(__builtin_popcount(bio->Media->BlockSize) != 1)) {
            print_warn("disk[%zu] block size is not a power of two (%u)"
                       ", skipped\n", i, bio->Media->BlockSize);
            continue;
        }

        if (unlikely(bio->Media->BlockSize > PAGE_SIZE)) {
            print_warn("disk[%zu] block size is too large (%u), skipped\n",
                       i, bio->Media->BlockSize);
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
        d->handle = handles[i];
        d->bio = bio;
        d->dio = dio;
        d->status = bio->Media->RemovableMedia ? DISK_STS_REMOVABLE : 0;
        d->sectors = bio->Media->LastBlock + 1;

        /*
         * Heuristic guess, refined by classify_disks() below for disks that
         * expose partition handles.
         */
        d->kind = (bio->Media->ReadOnly && bio->Media->BlockSize > 512) ?
                      DISK_KIND_CD : DISK_KIND_HD;

        block_shift = __builtin_ctz(bio->Media->BlockSize);

        buf = allocate_critical_pages(1);

        block_cache_init(&d->bc, uefi_refill_blocks, d,
                         block_shift,
                         buf, PAGE_SIZE >> block_shift);
        block_cache_enable_direct_io(&d->bc);

        print_info("detected disk: block-size %u, %llu blocks\n",
                   bio->Media->BlockSize, bio->Media->LastBlock + 1);
    }

    classify_disks(handles, handle_count);
    assign_disk_ids();

out:
    g_st->BootServices->FreePool(handles);
}

static void uefi_disk_services_cleanup(void)
{
    size_t i;

    for (i = 0; i < disk_count; ++i)
        block_cache_release(&disks[i].bc);

    g_st->BootServices->FreePool(disks);
    disk_count = 0;
}
DECLARE_CLEANUP_HANDLER(uefi_disk_services_cleanup);

void uefi_disk_services_init(void)
{
    enumerate_disks();
}
