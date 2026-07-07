#define MSG_FMT(msg) "BIOS-IO: " msg

#include "common/format.h"
#include "common/log.h"
#include "common/string.h"
#include "common/string_view.h"
#include "arch/constants.h"
#include "disk_services.h"
#include "bios_call.h"
#include "scratch_buffer.h"
#include "services_impl.h"
#include "filesystem/block_cache.h"

#define DISK_BUFFER_CAPACITY 128

struct bios_disk {
    u64 sectors;
    // BIOS drive number (0x80+), used for INT 13h
    u8 drive;
    // Index of this drive based on its type
    u8 id;
    u8 block_shift;
    u8 status;
};

// Indexed by (drive - FIRST_DRIVE_INDEX); empty slots have a zero drive
static struct bios_disk disks_buffer[DISK_BUFFER_CAPACITY];
static u8 disk_count;

/*
 * The one optical drive BIOS can positively identify (via El Torito); it
 * becomes cd0. There's no reliable way to classify any others, so everything
 * else is treated as a hard disk. See detect_boot_cd().
 */
static struct bios_disk *g_boot_cd;

// The kind + 0-based per-kind (hdN / cdN) index of an enumerated disk
static u32 disk_kind_id(const struct bios_disk *d, u8 *kind)
{
    *kind = d == g_boot_cd ? DISK_KIND_CD : DISK_KIND_HD;
    return d->id;
}

static struct block_cache tb_cache;
static u8 cache_last_disk_id;

#define FIRST_DRIVE_INDEX 0x80
#define LAST_DRIVE_INDEX 0xF0

#define BDA_DISK_COUNT_OFFSET 0x75

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
        sv_terminated_copy(sectors_buf, SV("<unknown>"));
    } else {
        snprintf(sectors_buf, 32, "%llu", sectors);
    }

    print_info("drive: 0x%X -> sectors: %s, bps: %u, removable: %s\n",
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

static void fetch_all_disks(void)
{
    // https://oldlinux.superglobalmegacorp.com/Linux.old/docs/interrupts/int-html/rb-0715.htm
    struct real_mode_regs regs;
    struct drive_parameters drive_params;
    u16 drive_index;
    u8 detected_non_removable_disks = 0;
    u8 number_of_bios_detected_disks;

    number_of_bios_detected_disks = bios_read_bda(BDA_DISK_COUNT_OFFSET, 1);
    print_info("BIOS-detected disks: %d\n", number_of_bios_detected_disks);

    for (drive_index = FIRST_DRIVE_INDEX; drive_index <= LAST_DRIVE_INDEX; ++drive_index) {
        bool is_removable = false, checked_edd = false;
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
            checked_edd = true;
        }

        pretty_print_drive_info(drive_index, drive_params.total_sector_count,
                                drive_params.bytes_per_sector, is_removable);

        /*
         * Removable disks are not reported in BDA_DISK_COUNT_ADDRESS,
         * so we accept any amount
         */
        if (!is_removable) {
            bool should_skip =
                detected_non_removable_disks >= number_of_bios_detected_disks;

            /*
             * Special case VMWare BIOS that doesn't mark ATAPI drives as
             * removable. This can sometimes be worked around by checking the
             * EDD, but it's not present in some guest OS compatibility modes,
             * e.g. Ubuntu 64-bit. There's only one ATAPI drive that is visible
             * via this discovery mechanism, and it has the index 0x9F.
             */
            if (unlikely(should_skip && !checked_edd && drive_index == 0x9F &&
                         drive_params.bytes_per_sector == 2048)) {
                should_skip = false;
                print_warn("allowing unaccounted non-removable drive 0x9F\n");
            }

            if (unlikely(should_skip)) {
                print_warn("skipping unexpected drive 0x%X\n", drive_index);
                continue;
            }

            detected_non_removable_disks++;
        }

        disks_buffer[drive_index - FIRST_DRIVE_INDEX] = (struct bios_disk) {
            .sectors = drive_params.total_sector_count,
            .drive = drive_index,
            .block_shift = __builtin_ctz(drive_params.bytes_per_sector),
            .status = is_removable ? DISK_STS_REMOVABLE : 0
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
        if (!(d->status & DISK_STS_REMOVABLE))
            print_warn("disk 0x%02X read failed, (ret=%u)\n", d->drive, regs->eax);

        return false;
    }

    return true;
}

static void tb_cache_invalidate(void *bc)
{
    ((struct block_cache*)bc)->flags |= BC_EMPTY;
}

static bool bios_refill_blocks(void *dp, void *buffer, u64 block, size_t count)
{
    struct bios_disk *d = dp;

    // https://oldlinux.superglobalmegacorp.com/Linux.old/docs/interrupts/int-html/rb-0708.htm
    struct disk_address_packet packet = {
        .packet_size = sizeof(packet)
    };

    struct real_mode_regs regs = {
        .edx = d->drive,
        .esi = (u32)&packet
    };
    struct real_mode_addr tb_addr;

    scratch_buffer_borrow(SCRATCH_BUFFER_SIZE, tb_cache_invalidate, &tb_cache);

    as_real_mode_addr((u32)buffer, &tb_addr);

    regs.eax = 0x4200;
    packet.first_block = block;
    packet.blocks_to_transfer = count;
    packet.buffer_segment = tb_addr.segment;
    packet.buffer_offset = tb_addr.offset;

    bios_call(0x13, &regs, &regs);
    return check_read(d, &regs);
}

void ds_query_disk(size_t idx, struct disk *out_disk)
{
    SERVICE_FUNCTION();
    BUG_ON(idx >= disk_count);

    struct bios_disk *d = NULL;
    size_t seen = 0;
    u8 i, kind;

    for (i = 0; i < DISK_BUFFER_CAPACITY; ++i) {
        if (!disks_buffer[i].drive)
            continue;
        if (seen++ == idx) {
            d = &disks_buffer[i];
            break;
        }
    }

    BUG_ON(!d);

    *out_disk = (struct disk) {
        .sectors = d->sectors,
        .handle = (void*)((ptr_t)d->drive),
        .id = disk_kind_id(d, &kind),
        .kind = kind,
        .block_shift = d->block_shift,
        .status = d->status
    };
}

static void set_cache_to_disk(struct bios_disk *d)
{
    if (cache_last_disk_id == d->drive)
        return;

    cache_last_disk_id = d->drive;
    tb_cache.user_ptr = d;
    tb_cache.block_shift = d->block_shift;
    tb_cache.block_size = 1 << d->block_shift;
    tb_cache.cache_block_cap = SCRATCH_BUFFER_SIZE >> tb_cache.block_shift;
    tb_cache.flags |= BC_EMPTY;
}

static void switch_to_handle(void *handle)
{
    struct bios_disk *d = get_disk_by_handle(handle);
    set_cache_to_disk(d);
}

bool ds_read_blocks(void *handle, void *buffer, u64 sector, size_t blocks)
{
    SERVICE_FUNCTION();

    switch_to_handle(handle);
    return block_cache_read_blocks(&tb_cache, buffer, sector, blocks);
}

bool ds_read(void *handle, void *buffer, u64 offset, size_t bytes)
{
    SERVICE_FUNCTION();

    switch_to_handle(handle);
    return block_cache_read(&tb_cache, buffer, offset, bytes);
}

u32 ds_get_disk_count(void)
{
    SERVICE_FUNCTION();

    return disk_count;
}

// Captured from dl by the entry stub (bios_entry.asm)
extern u8 g_boot_drive;

/*
 * Boot partition index baked into the stage2 header by the installer, or the
 * unspecified sentinel below when no partition was pinned at install time.
 * See bios_entry.asm.
 */
extern u32 g_boot_partition;
#define BOOT_PARTITION_UNSPECIFIED 0xFFFFFFFFu

// Synthetic drive number set by the PXE boot record (see boot_record.asm)
#define BIOS_PXE_BOOT_DRIVE 0xFF

bool ds_query_boot_device(struct boot_device_info *out_info)
{
    SERVICE_FUNCTION();

    /*
     * The PXE boot record hands us a sentinel drive so a network boot is
     * unambiguous rather than looking like an unknown disk.
     */
    if (g_boot_drive == BIOS_PXE_BOOT_DRIVE) {
        *out_info = (struct boot_device_info) {
            .type = BOOT_DEVICE_TYPE_PXE,
            .partition_id = BOOT_PARTITION_ID_TYPE_NONE,
        };
        return true;
    }

    /*
     * If the boot drive maps to a disk we actually enumerated, that's where we
     * booted from; otherwise we can't name a boot device and the caller falls
     * back to a full scan.
     */
    if (g_boot_drive >= FIRST_DRIVE_INDEX && g_boot_drive <= LAST_DRIVE_INDEX &&
        disks_buffer[g_boot_drive - FIRST_DRIVE_INDEX].drive == g_boot_drive) {
        struct bios_disk *d;

        out_info->type = BOOT_DEVICE_TYPE_DISK;

        d = &disks_buffer[g_boot_drive - FIRST_DRIVE_INDEX];
        out_info->disk_id = disk_kind_id(d, &out_info->disk_kind);

        /*
         * The installer pins a partition by index, but it is optionally omitted
         * by the user, so check for that case.
         */
        if (g_boot_partition != BOOT_PARTITION_UNSPECIFIED) {
            out_info->partition_id = BOOT_PARTITION_ID_TYPE_INDEX;
            out_info->partition_index = g_boot_partition;
        } else {
            out_info->partition_id = BOOT_PARTITION_ID_TYPE_NONE;
        }

        return true;
    }

    return false;
}

// El Torito specification packet returned by INT 13h, AX=4B01h
struct PACKED el_torito_spec_packet {
    u8 size;
    u8 media_type;
    u8 drive_no;
    u8 controller_no;
    u32 image_lba;
    u16 device_spec;
    u16 cache_seg;
    u16 load_seg;
    u16 length_sec512;
    u8 cylinders;
    u8 sectors;
    u8 heads;
    u8 dummy[16];
};

// El Torito media type mask & the "no emulation" value (as in GRUB's biosdisk)
#define EL_TORITO_MEDIA_TYPE_MASK 0x0F
#define EL_TORITO_NO_EMULATION    0x00

/*
 * Ask the BIOS (INT 13h, AX=4B01h) for the El Torito status of the boot drive.
 * On success for a no-emulation CD it reports the CD's *own* drive number in the
 * packet (which may differ from the queried drive), which is how we learn the
 * optical drive's number even when we booted off a hard disk.
 */
static u16 query_cd_drive(void)
{
    struct el_torito_spec_packet packet = {
        .size = sizeof(packet),
        .media_type = 0xFF,
    };
    struct real_mode_regs regs = {
        .eax = 0x4B01,
        .edx = g_boot_drive,
        .esi = (u32)&packet,
    };

    // INT 13h, AX=4B01h: get El Torito emulation status
    bios_call(0x13, &regs, &regs);
    if (is_carry_set(&regs))
        return 0;

    if ((packet.media_type & EL_TORITO_MEDIA_TYPE_MASK) != EL_TORITO_NO_EMULATION)
        return 0;

    return packet.drive_no;
}

static void detect_boot_cd(void)
{
    u16 cd_drive = query_cd_drive();

    if (cd_drive < FIRST_DRIVE_INDEX || cd_drive > LAST_DRIVE_INDEX)
        return;

    // Only mark it if we actually enumerated that drive
    if (disks_buffer[cd_drive - FIRST_DRIVE_INDEX].drive == cd_drive)
        g_boot_cd = &disks_buffer[cd_drive - FIRST_DRIVE_INDEX];
}

static void assign_disk_ids(void)
{
    struct bios_disk *d;
    u8 hd_count = 0;

    for (d = disks_buffer; d < disks_buffer + DISK_BUFFER_CAPACITY; ++d) {
        if (!d->drive)
            continue;

        // cd is always cd0, as we only support one
        d->id = (d == g_boot_cd) ? 0 : hd_count++;
    }
}

void bios_disk_services_init(void)
{
    void *buf;

    fetch_all_disks();
    detect_boot_cd();
    assign_disk_ids();

    buf = scratch_buffer_borrow(
        SCRATCH_BUFFER_SIZE, tb_cache_invalidate, &tb_cache
    );
    block_cache_init(&tb_cache, bios_refill_blocks, NULL, 0, buf, 0);
}
