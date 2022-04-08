#include "bios_disk_services.h"
#include "bios_call.h"
#include "common/format.h"
#include "common/log.h"
#include "common/string.h"
#include "common/string_view.h"
#include "common/minmax.h"
#include "filesystem/block_cache.h"

#undef MSG_FMT
#define MSG_FMT(msg) "BIOS-IO: " msg

struct bios_disk {
    struct disk d;
    u8 disk_id;
};

#define DISK_BUFFER_CAPACITY 128
static struct bios_disk disks_buffer[DISK_BUFFER_CAPACITY];
static u8 disk_count;

/*
 * Since disks are stored with id -> disk and not contiguously,
 * we record this info here to help speed up disk_query()
 */
static u8 next_buf_idx;
static u8 next_enum_idx = DISK_BUFFER_CAPACITY;

#define TRANSFER_BUFFER_CAPACITY PAGE_SIZE
static u8 transfer_buffer[TRANSFER_BUFFER_CAPACITY];
static struct block_cache tb_cache;
static u8 cache_last_disk_id;

#define FIRST_DRIVE_INDEX 0x80
#define LAST_DRIVE_INDEX 0xFF

#define BDA_DISK_COUNT_ADDRESS 0x0475

#define REMOVABLE_DRIVE (1 << 2)
struct PACKED drive_parameters {
    u16 buffer_size;
    u16 flags;
    u32 cylinders;
    u32 heads;
    u32 sectors;
    u64 total_sector_count;
    u16 bytes_per_sector;
    u16 edd_config_offset;
    u16 edd_config_segment;
    u16 signature;
    u8 device_path_length;
    u8 reserved[3];
    char host_bus[4];
    char interface_type[8];
    u64 interface_path;
    u64 device_path;
    u8 reserved1;
    u8 checksum;
};
BUILD_BUG_ON(sizeof(struct drive_parameters) != 0x42);

struct PACKED disk_address_packet {
    u8 packet_size;
    u8 reserved;
    u16 blocks_to_transfer;
    u16 buffer_offset;
    u16 buffer_segment;
    u64 first_block;
    u64 flat_address;
};
BUILD_BUG_ON(sizeof(struct drive_parameters) != 0x42);

struct PACKED enhanced_disk_drive_parameter_table {
    u16 io_base_address;
    u16 control_port_address;
    u8 drive_flags;
    u8 reserved_1;
    u8 drive_irq;
    u8 multisector_transfer_count;
    u8 dma_control;
    u8 programmed_io_control;

#define DRIVE_OPTION_REMOVABLE (1 << 5)
#define DRIVE_OPTION_ATAPI     (1 << 6)
    u16 drive_options;

    u16 reserved_2;
    u8 extension_revision;
    u8 checksum;
};
BUILD_BUG_ON(sizeof(struct enhanced_disk_drive_parameter_table) != 16);

#define IS_TRANSLATED_DPT(dpt) ((dpt->control_port_address & 0xFF00) == 0xA000)

static void pretty_print_drive_info(u8 drive_idx, u64 sectors, u32 bytes_per_sector, bool is_removable)
{
    char sectors_buf[32];
    if (sectors == 0xFFFFFFFFFFFFFFFF) {
        struct string_view unknown = SV("<unknown>");
        memcpy(sectors_buf, unknown.text, unknown.size + 1);
    } else {
        snprintf(sectors_buf, 32, "%llu", sectors);
    }

    print_info("drive: 0x%X -> sectors: %s, bps: %hu, removable: %s\n",
               drive_idx, sectors_buf, bytes_per_sector,
               is_removable ? "yes" : "no");
}

static bool edpt_is_removable_disk(struct enhanced_disk_drive_parameter_table *edpt)
{
    bool is_atapi, is_removable;

    if (IS_TRANSLATED_DPT(edpt))
        return false; // We don't know

    is_removable = edpt->drive_options & DRIVE_OPTION_REMOVABLE;
    is_atapi = edpt->drive_options & DRIVE_OPTION_ATAPI;

    if (!is_removable && is_atapi) {
        print_warn("ATAPI drive declared non-removable, assuming it is");
        return true;
    }

    return is_removable;
}

#define DRIVE_PARAMS_V2 0x1E

static void fetch_all_disks()
{
    // https://oldlinux.superglobalmegacorp.com/Linux.old/docs/interrupts/int-html/rb-0715.htm
    struct real_mode_regs regs;
    struct drive_parameters drive_params;
    u16 drive_index;
    u8 detected_non_removable_disks = 0;
    u8 number_of_bios_detected_disks = *(volatile u8*)BDA_DISK_COUNT_ADDRESS;
    print_info("BIOS-detected disks: %d\n", number_of_bios_detected_disks);

    for (drive_index = FIRST_DRIVE_INDEX; drive_index <= LAST_DRIVE_INDEX; ++drive_index) {
        bool is_removable = false;
        memzero(&regs, sizeof(regs));
        memzero(&drive_params, sizeof(drive_params));

        regs.eax = 0x4800;
        regs.edx = drive_index;
        regs.esi = (u32)&drive_params;
        drive_params.buffer_size = sizeof(drive_params);
        drive_params.flags = 0;

        bios_call(0x13, &regs, &regs);

        if (is_carry_set(&regs))
            continue;

        if ((regs.eax & 0xFF00) != 0x0000)
            continue;

        if (drive_params.total_sector_count == 0 || drive_params.bytes_per_sector == 0)
            continue;

        if (unlikely(__builtin_popcount(drive_params.bytes_per_sector) != 1)) {
            print_warn("skipping a non-power-of-two block size (%u) disk %X\n",
                       drive_params.bytes_per_sector, drive_index);
            continue;
        }

        if (unlikely(drive_params.bytes_per_sector > PAGE_SIZE)) {
            print_warn("disk %X block size is too large (%u), skipped\n",
                       drive_index, drive_params.bytes_per_sector);
            continue;
        }

        is_removable = drive_params.flags & REMOVABLE_DRIVE;

        // VMWare doesn't report removable device in the main drive parameters, check EDD instead
        if (drive_params.buffer_size >= DRIVE_PARAMS_V2 &&
           (drive_params.edd_config_offset != 0x0000 || drive_params.edd_config_segment != 0x0000) &&
           (drive_params.edd_config_offset != 0xFFFF || drive_params.edd_config_segment != 0xFFFF)) {
            void *edpt = from_real_mode_addr(drive_params.edd_config_segment,
                                             drive_params.edd_config_offset);
            is_removable |= edpt_is_removable_disk(edpt);
        }

        pretty_print_drive_info(drive_index, drive_params.total_sector_count,
                                drive_params.bytes_per_sector, is_removable);

        // Removable disks are not reported in BDA_DISK_COUNT_ADDRESS, so we accept any amount
        if (!is_removable) {
            if (unlikely(detected_non_removable_disks >= number_of_bios_detected_disks)) {
                print_warn("skipping unexpected drive 0x%X\n", drive_index);
                continue;
            }

            detected_non_removable_disks++;
        }

        disks_buffer[drive_index - FIRST_DRIVE_INDEX] = (struct bios_disk) {
            .d = {
                .sectors = drive_params.total_sector_count,
                .handle = (void*)((ptr_t)drive_index),
                .block_shift = __builtin_ffs(drive_params.bytes_per_sector) - 1,
                .status = is_removable ? DISK_STS_REMOVABLE : 0
            },
            .disk_id = drive_index
        };
        disk_count++;
    }
}

static struct bios_disk *get_disk_by_handle(void *handle)
{
    u8 drive_id = (u32)handle & 0xFF;
    BUG_ON(drive_id < FIRST_DRIVE_INDEX);

    return &disks_buffer[drive_id - FIRST_DRIVE_INDEX];
}

static bool check_read(const struct bios_disk *d, const struct real_mode_regs *regs)
{
    if (is_carry_set(regs) || ((regs->eax & 0xFF00) != 0x0000)) {
        // Don't print a warning for removable drives, it's expected
        if (!(d->d.status & DISK_STS_REMOVABLE))
            print_warn("disk 0x%02X read failed, (ret=%u)\n", d->disk_id, regs->eax);

        return false;
    }

    return true;
}

static bool bios_refill_blocks(void *dp, void *buffer, u64 block, size_t count)
{
    struct bios_disk *d = dp;

    // https://oldlinux.superglobalmegacorp.com/Linux.old/docs/interrupts/int-html/rb-0708.htm
    struct disk_address_packet packet = {
        .packet_size = sizeof(packet)
    };

    struct real_mode_regs regs = {
        .edx = d->disk_id,
        .esi = (u32)&packet
    };
    struct real_mode_addr tb_addr;

    as_real_mode_addr((u32)buffer, &tb_addr);

    regs.eax = 0x4200;
    packet.first_block = block;
    packet.blocks_to_transfer = count;
    packet.buffer_segment = tb_addr.segment;
    packet.buffer_offset = tb_addr.offset;

    bios_call(0x13, &regs, &regs);
    return check_read(d, &regs);
}

static void query_disk(size_t idx, struct disk *out_disk)
{
    BUG_ON(idx >= disk_count);
    u8 i = idx == next_enum_idx ? next_buf_idx : 0;

    for (; i < DISK_BUFFER_CAPACITY; ++i) {
        if (disks_buffer[i].disk_id)
            break;
    }

    BUG_ON(i == DISK_BUFFER_CAPACITY);
    next_enum_idx = idx + 1;
    next_buf_idx = i + 1;

    *out_disk = disks_buffer[i].d;
}

static void set_cache_to_disk(struct bios_disk *d)
{
    if (cache_last_disk_id == d->disk_id)
        return;

    cache_last_disk_id = d->disk_id;
    tb_cache.user_ptr = d;
    tb_cache.block_shift = d->d.block_shift;
    tb_cache.block_size = disk_block_size(&d->d);
    tb_cache.cache_block_cap = TRANSFER_BUFFER_CAPACITY >> tb_cache.block_shift;
    tb_cache.flags |= BC_EMPTY;
}

static void switch_to_handle(void *handle)
{
    struct bios_disk *d = get_disk_by_handle(handle);
    BUG_ON(!d);

    set_cache_to_disk(d);
}

static bool read_blocks(void *handle, void *buffer, u64 sector, size_t blocks)
{
    switch_to_handle(handle);
    return block_cache_read_blocks(&tb_cache, buffer, sector, blocks);
}

static bool read(void *handle, void *buffer, u64 offset, size_t bytes)
{
    switch_to_handle(handle);
    return block_cache_read(&tb_cache, buffer, offset, bytes);
}

static struct disk_services bios_disk_services = {
    .query_disk = query_disk,
    .read = read,
    .read_blocks = read_blocks
};

struct disk_services *disk_services_init()
{
    if (!disk_count) {
        fetch_all_disks();
        block_cache_init(&tb_cache, bios_refill_blocks, NULL, 0, transfer_buffer, 0);
    }

    bios_disk_services.disk_count = disk_count;
    return &bios_disk_services;
}
