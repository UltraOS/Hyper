#include "bios_disk_services.h"
#include "bios_call.h"
#include "common/log.h"
#include "common/string.h"
#include "common/minmax.h"

#undef MSG_FMT
#define MSG_FMT(msg) "BIOS-IO: " msg

#define DISK_BUFFER_CAPACITY 128
static struct disk g_disks_buffer[DISK_BUFFER_CAPACITY];
static size_t g_disk_count = 0;

#define TRANSFER_BUFFER_CAPACITY 4096
static u8 g_transfer_buffer[TRANSFER_BUFFER_CAPACITY];

#define FIRST_DRIVE_INDEX 0x80
#define LAST_DRIVE_INDEX 0xFF

#define BDA_DISK_COUNT_ADDRESS 0x0475

struct PACKED drive_parameters {
    u16 buffer_size;
    u16 flags;
    u32 cylinders;
    u32 heads;
    u32 sectors;
    u64 total_sector_count;
    u16 bytes_per_sector;
    u32 edd_config_parameters;
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

static void fetch_all_disks()
{
    // https://oldlinux.superglobalmegacorp.com/Linux.old/docs/interrupts/int-html/rb-0715.htm
    struct real_mode_regs regs;
    struct drive_parameters drive_params;
    u8 detected_disks = 0, drive_index;
    u8 number_of_disks = *(volatile u8*)BDA_DISK_COUNT_ADDRESS;
    print_info("BIOS-detected disks: %d\n", number_of_disks);

    BUG_ON(number_of_disks == 0);

    for (drive_index = 0x80; drive_index < 0xFF; ++drive_index) {
        memzero(&regs, sizeof(regs));
        memzero(&drive_params, sizeof(drive_params));

        regs.eax = 0x4800;
        regs.edx = drive_index;
        regs.esi = (u32)&drive_params;
        drive_params.buffer_size = sizeof(drive_params);

        bios_call(0x13, &regs, &regs);

        if (is_carry_set(&regs))
            continue;

        if ((regs.eax & 0xFF00) != 0x0000)
            continue;

        if (drive_params.total_sector_count == 0 || drive_params.bytes_per_sector == 0)
            continue;

        if (drive_params.bytes_per_sector != 512 && drive_params.bytes_per_sector != 2048) {
            print_warn("unsupported bytes per sector %hu for drive %X\n",
                       drive_params.bytes_per_sector, drive_index);
            continue;
        }

        print_info("detected drive: %X -> sectors: %llu, bytes per sector: %hu\n",
                   drive_index, drive_params.total_sector_count, drive_params.bytes_per_sector);

        g_disks_buffer[g_disk_count++] = (struct disk) {
            .bytes_per_sector = drive_params.bytes_per_sector,
            .sectors = drive_params.total_sector_count,
            .handle = (void*)((ptr_t)drive_index)
        };

        if (++detected_disks == number_of_disks)
            return;
    }

    print_warn("BIOS reported more disks than was detected? (%d vs %d)\n",
               detected_disks, number_of_disks);
}

static struct disk *disk_from_handle(void *handle)
{
    u32 drive_id = (u32)handle & 0xFF;
    size_t i;
    BUG_ON(drive_id < FIRST_DRIVE_INDEX || drive_id >= LAST_DRIVE_INDEX);

    for (i = 0; i < g_disk_count; ++i) {
        if (g_disks_buffer[i].handle == handle)
            return &g_disks_buffer[i];
    }

    return NULL;
}

static bool check_read(const struct real_mode_regs *regs)
{
    if (is_carry_set(regs) || ((regs->eax & 0xFF00) != 0x0000)) {
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
    u64 last_read_sector = (offset + bytes) / d->bytes_per_sector;
    u64 current_sector = offset / d->bytes_per_sector;
    size_t sectors_to_read = bytes / d->bytes_per_sector;

    as_real_mode_addr((u32)g_transfer_buffer, &tb_addr);
    BUG_ON(bytes == 0);

    if (d->sectors <= last_read_sector)
        panic("BUG! invalid read at %llu with %zu bytes\n", offset, bytes);

    offset -= current_sector * d->bytes_per_sector;

    // round up sectors to read in case read is not aligned to sector size
    if (offset != 0 || (bytes % d->bytes_per_sector))
        sectors_to_read++;

    for (;;) {
        u32 sectors_for_this_read = MIN(sectors_to_read, TRANSFER_BUFFER_CAPACITY / (u32)d->bytes_per_sector);
        u32 bytes_for_this_read = sectors_for_this_read * d->bytes_per_sector;
        size_t bytes_to_copy;

        sectors_to_read -= sectors_for_this_read;

        regs.eax = 0x4200;
        packet.first_block = current_sector;
        packet.blocks_to_transfer = sectors_for_this_read;
        packet.buffer_segment = tb_addr.segment;
        packet.buffer_offset = tb_addr.offset;

        bios_call(0x13, &regs, &regs);
        if (!check_read(&regs))
            return false;

        bytes_to_copy = MIN(bytes_for_this_read, bytes);
        memcpy(byte_buffer, &g_transfer_buffer[offset], bytes_to_copy);

        bytes -= bytes_to_copy;
        if (bytes == 0)
            break;

        BUG_ON(sectors_to_read == 0);
    }

    return true;
}

static struct disk* list_disks(size_t *count)
{
    *count = g_disk_count;
    return g_disks_buffer;
}

static bool read_blocks(void *handle, void *buffer, u64 sector, size_t blocks)
{
    struct disk *d = disk_from_handle(handle);
    BUG_ON(!d);

    return do_read(d, buffer, sector * d->bytes_per_sector, blocks * d->bytes_per_sector);
}

static bool read(void *handle, void *buffer, u64 offset, size_t bytes)
{
    struct disk *d = disk_from_handle(handle);
    BUG_ON(!d);

    return do_read(d, buffer, offset, bytes);
}

static struct disk_services bios_disk_services = {
    .list_disks = list_disks,
    .read = read,
    .read_blocks = read_blocks
};

struct disk_services *disk_services_init()
{
    if (!g_disk_count)
        fetch_all_disks();

    return &bios_disk_services;
}
