#include "bios_disk_services.h"
#include "bios_call.h"
#include "common/format.h"
#include "common/log.h"
#include "common/string.h"
#include "common/string_view.h"
#include "common/minmax.h"

#undef MSG_FMT
#define MSG_FMT(msg) "BIOS-IO: " msg

#define DISK_BUFFER_CAPACITY 128
static struct disk disks_buffer[DISK_BUFFER_CAPACITY];
static size_t disk_count = 0;

#define TRANSFER_BUFFER_CAPACITY 4096u
static u8 transfer_buffer[TRANSFER_BUFFER_CAPACITY];

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
    u8 detected_non_removable_disks = 0, drive_index;
    u8 number_of_bios_detected_disks = *(volatile u8*)BDA_DISK_COUNT_ADDRESS;
    print_info("BIOS-detected disks: %d\n", number_of_bios_detected_disks);

    for (drive_index = 0x80; drive_index < 0xFF; ++drive_index) {
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

        disks_buffer[disk_count++] = (struct disk) {
            .sectors = drive_params.total_sector_count,
            .handle = (void*)((ptr_t)drive_index),
            .block_shift = __builtin_ffs(drive_params.bytes_per_sector) - 1,
            .status = is_removable ? DISK_STS_REMOVABLE : 0
        };
    }
}

static struct disk *disk_from_handle(void *handle)
{
    u32 drive_id = (u32)handle & 0xFF;
    size_t i;
    BUG_ON(drive_id < FIRST_DRIVE_INDEX || drive_id >= LAST_DRIVE_INDEX);

    for (i = 0; i < disk_count; ++i) {
        if (disks_buffer[i].handle == handle)
            return &disks_buffer[i];
    }

    return NULL;
}

static bool check_read(const struct disk *d, const struct real_mode_regs *regs)
{
    if (is_carry_set(regs) || ((regs->eax & 0xFF00) != 0x0000)) {
        // Don't print a warning for removable drives, it's expected
        if (!(d->status & DISK_STS_REMOVABLE))
            print_warn("disk read failed, (ret=%u)\n", regs->eax);

        return false;
    }

    return true;
}

static bool do_read(const struct disk *d, void *buffer, u64 offset, size_t bytes)
{
    // https://oldlinux.superglobalmegacorp.com/Linux.old/docs/interrupts/int-html/rb-0708.htm
    struct disk_address_packet packet = {
        .packet_size = sizeof(packet)
    };

    struct real_mode_regs regs = {
        .edx = (u32)d->handle & 0xFF,
        .esi = (u32)&packet
    };
    struct real_mode_addr tb_addr;

    u8* byte_buffer = buffer;
    u64 last_read_sector = (offset + bytes) >> d->block_shift;
    u64 current_sector = offset >> d->block_shift;
    size_t sectors_to_read = bytes >> d->block_shift;

    as_real_mode_addr((u32)transfer_buffer, &tb_addr);
    BUG_ON(bytes == 0);

    if (d->sectors <= last_read_sector)
        panic("BUG! invalid read at %llu with %zu bytes\n", offset, bytes);

    offset -= current_sector << d->block_shift;

    // round up sectors to read in case read is not aligned to sector size
    if (offset != 0 || (bytes % (1ul << d->block_shift)))
        sectors_to_read++;

    for (;;) {
        u32 sectors_for_this_read = MIN(sectors_to_read, TRANSFER_BUFFER_CAPACITY >> d->block_shift);
        u32 bytes_for_this_read = sectors_for_this_read << d->block_shift;
        size_t bytes_to_copy;

        sectors_to_read -= sectors_for_this_read;

        regs.eax = 0x4200;
        packet.first_block = current_sector;
        packet.blocks_to_transfer = sectors_for_this_read;
        packet.buffer_segment = tb_addr.segment;
        packet.buffer_offset = tb_addr.offset;

        bios_call(0x13, &regs, &regs);
        if (!check_read(d, &regs))
            return false;

        bytes_to_copy = MIN(bytes_for_this_read, bytes);
        memcpy(byte_buffer, &transfer_buffer[offset], bytes_to_copy);

        bytes -= bytes_to_copy;
        if (bytes == 0)
            break;

        BUG_ON(sectors_to_read == 0);
    }

    return true;
}

static void query_disk(size_t idx, struct disk *out_disk)
{
    BUG_ON(idx >= disk_count);
    *out_disk = disks_buffer[idx];
}

static bool read_blocks(void *handle, void *buffer, u64 sector, size_t blocks)
{
    struct disk *d = disk_from_handle(handle);
    BUG_ON(!d);

    return do_read(d, buffer, sector << d->block_shift, blocks << d->block_shift);
}

static bool read(void *handle, void *buffer, u64 offset, size_t bytes)
{
    struct disk *d = disk_from_handle(handle);
    BUG_ON(!d);

    return do_read(d, buffer, offset, bytes);
}

static struct disk_services bios_disk_services = {
    .query_disk = query_disk,
    .read = read,
    .read_blocks = read_blocks
};

struct disk_services *disk_services_init()
{
    if (!disk_count)
        fetch_all_disks();

    bios_disk_services.disk_count = disk_count;
    return &bios_disk_services;
}
