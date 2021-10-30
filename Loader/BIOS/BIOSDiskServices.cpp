#include "BIOSDiskServices.h"

#include "Common/Logger.h"
#include "Common/Utilities.h"
#include "BIOSCall.h"

static constexpr size_t disk_buffer_capacity = 128;
static Disk g_disks_buffer[disk_buffer_capacity];

static constexpr size_t transfer_buffer_capacity = 2048;
static u8 g_transfer_buffer[transfer_buffer_capacity];

static constexpr u8 first_drive_index = 0x80;
static constexpr u8 last_drive_index = 0xFF;
static constexpr u32 dma64_support_bit = 1 << 8;

struct __attribute__((packed)) DriverParameters {
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
static_assert(sizeof(DriverParameters) == 0x42);

struct __attribute__((packed)) DiskAddressPacket {
    u8 packet_size;
    u8 reserved;
    u16 blocks_to_transfer;
    u16 buffer_offset;
    u16 buffer_segment;
    u64 first_block;
    u64 flat_buffer_address;
};
static_assert(sizeof(DiskAddressPacket) == 0x18);

bool operator<(const Disk& l, const Disk& r) { return static_cast<u8>(l.id) < static_cast<u8>(r.id); }
bool operator<(u32 id, const Disk& r) { return static_cast<u8>(id) < static_cast<u8>(r.id); }
bool operator<(const Disk& l, u32 id) { return static_cast<u8>(l.id) < static_cast<u8>(id); }

BIOSDiskServices BIOSDiskServices::create()
{
    return { g_disks_buffer, disk_buffer_capacity };
}

BIOSDiskServices::BIOSDiskServices(Disk* buffer, size_t capacity)
    : m_buffer(buffer)
{
    if (capacity < (last_drive_index - first_drive_index))
        panic("buffer is too small to hold all disks");

    fetch_all_disks();
}

void BIOSDiskServices::fetch_all_disks()
{
    static constexpr ptr_t bda_number_of_disks_ptr = 0x0475;
    u8 number_of_disks = *Address(bda_number_of_disks_ptr).as_pointer<volatile u8>();
    logger::info("BIOS-detected disks: ", number_of_disks);

    // https://oldlinux.superglobalmegacorp.com/Linux.old/docs/interrupts/int-html/rb-0715.htm
    RealModeRegisterState registers;
    DriverParameters drive_params;
    u8 detected_disks = 0;

    for (u8 drive_index = 0x80; drive_index < 0xFF; ++drive_index) {
        zero_memory(&registers, sizeof(registers));
        zero_memory(&drive_params, sizeof(drive_params));

        registers.eax = 0x4800;
        registers.edx = drive_index;
        registers.esi = reinterpret_cast<u32>(&drive_params);
        drive_params.buffer_size = sizeof(drive_params);

        bios_call(0x13, &registers, &registers);

        if (registers.is_carry_set())
            continue;

        if ((registers.eax & 0xFF00) != 0x0000)
            continue;

        if (drive_params.total_sector_count == 0 || drive_params.bytes_per_sector == 0)
            continue;

        if (drive_params.bytes_per_sector != 512 && drive_params.bytes_per_sector != 2048) {
            logger::warning("unsupported bytes per second ", drive_params.bytes_per_sector,
                            " for drive ", drive_index);
            continue;
        }

        if (detected_disks++ == number_of_disks)
            return;

        logger::info("detected drive: ", logger::hex, drive_index,
                     logger::dec, " -> sectors: ", drive_params.total_sector_count,
                     ", bytes per sector: ", drive_params.bytes_per_sector);

        auto& disk = m_buffer[m_size++];
        disk.bytes_per_sector = drive_params.bytes_per_sector;
        disk.sectors = drive_params.total_sector_count;
        disk.id = drive_index;

        static constexpr size_t edd_v3 = 0x42;
        if (drive_params.buffer_size == edd_v3)
            disk.id |= dma64_support_bit;
    }

    if (detected_disks < number_of_disks)
        logger::warning("BIOS reported more disks than was detected?");
}

Span<Disk> BIOSDiskServices::list_disks()
{
    return { m_buffer, m_size };
}

bool BIOSDiskServices::read(u32 id, void* buffer, u64 sector, size_t count)
{
    auto drive_id = id & 0xFF;

    if (drive_id < first_drive_index || drive_id >= last_drive_index) {
        logger::error("read() called on invalid drive ", drive_id);
        hang();
    }

    // https://oldlinux.superglobalmegacorp.com/Linux.old/docs/interrupts/int-html/rb-0708.htm
    DiskAddressPacket packet {};
    packet.packet_size = sizeof(packet);

    RealModeRegisterState registers {};
    registers.edx = drive_id;
    registers.esi = reinterpret_cast<u32>(&packet);

    auto check_read = [] (const RealModeRegisterState& registers) -> bool
    {
        if (registers.is_carry_set() || ((registers.eax & 0xFF00) != 0x0000)) {
            logger::warning("disk read failed, (ret=", registers.eax, ")");
            return false;
        }

        return true;
    };

    auto* end = m_buffer + m_size;
    auto disk = lower_bound(m_buffer, end, drive_id);

    if (disk == end || disk->id != drive_id) {
        logger::error("read() called on invalid id ", drive_id);
        hang();
    }

    auto last_read_sector = sector + count;
    if (disk->sectors < last_read_sector || last_read_sector < sector) {
        logger::error("invalid read() at ", sector, " count ", count);
        hang();
    }

    bool dma64 = disk->id & dma64_support_bit;

    // Cap dma64 at 64 sectors, 0x7F seems to be the actual limit, but just to be on the safe side
    // and read at powers of 2.
    auto sectors_per_read = dma64 ? 64 : transfer_buffer_capacity / disk->bytes_per_sector;

    auto* byte_buffer = reinterpret_cast<u8*>(buffer);

    auto transfer_buffer_address = as_real_mode_address(g_transfer_buffer);

    for (size_t i = 0; i < count; i += sectors_per_read) {
        if ((count - i) < sectors_per_read)
            sectors_per_read = count - i;

        registers.eax = 0x4200;
        packet.first_block = sector + i;
        packet.blocks_to_transfer = sectors_per_read;

        // Fast path for EDD3.0
        if (dma64) {
            packet.buffer_offset = 0xFFFF;
            packet.buffer_segment = 0xFFFF;
            packet.flat_buffer_address = reinterpret_cast<u64>(byte_buffer);

            bios_call(0x13, &registers, &registers);
            dma64 = check_read(registers);

            if (!dma64) {
                logger::warning("disabling DMA64 for disk ", drive_id);
                sectors_per_read = min(transfer_buffer_capacity / disk->bytes_per_sector, sectors_per_read);
                disk->id &= ~dma64_support_bit;
            }
        }

        auto bytes_for_this_read = sectors_per_read * disk->bytes_per_sector;

        if (!dma64) {
            packet.buffer_segment = transfer_buffer_address.segment;
            packet.buffer_offset = transfer_buffer_address.offset;

            bios_call(0x13, &registers, &registers);
            if (!check_read(registers))
                return false;

            copy_memory(g_transfer_buffer, byte_buffer, bytes_for_this_read);
        }

        byte_buffer += bytes_for_this_read;
    }

    return true;
}
