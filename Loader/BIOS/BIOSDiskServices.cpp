#include "BIOSDiskServices.h"

#include "Common/Logger.h"
#include "Common/Utilities.h"
#include "Common/Panic.h"
#include "BIOSCall.h"

static constexpr size_t disk_buffer_capacity = 128;
static Disk g_disks_buffer[disk_buffer_capacity];

static constexpr size_t transfer_buffer_capacity = 4096;
static u8 g_transfer_buffer[transfer_buffer_capacity];

static constexpr u8 first_drive_index = 0x80;
static constexpr u8 last_drive_index = 0xFF;
static constexpr u32 dma64_support_bit = 1 << 8;

struct PACKED DriverParameters {
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

struct PACKED DiskAddressPacket {
    u8 packet_size;
    u8 reserved;
    u16 blocks_to_transfer;
    u16 buffer_offset;
    u16 buffer_segment;
    u64 first_block;
    u64 flat_address;
};
static_assert(sizeof(DiskAddressPacket) == 0x18);

bool operator<(const Disk& l, const Disk& r) { return l.handle < r.handle; }
bool operator<(void* l, const Disk& r) { return l < r.handle; }
bool operator<(const Disk& l, void* r) { return l.handle < r; }

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
    static constexpr Address bda_number_of_disks_address = 0x0475;
    u8 number_of_disks = *bda_number_of_disks_address.as_pointer<volatile u8>();
    logln("BIOS-detected disks: {}", number_of_disks);

    if (number_of_disks == 0)
        panic("BIOS reported 0 detected disks");

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
            warnln("unsupported bytes per sector {} for drive {}",
                   drive_params.bytes_per_sector, drive_index);
            continue;
        }

        logln("detected drive: {x} -> sectors: {}, bytes per sector: {}",
              drive_index, drive_params.total_sector_count, drive_params.bytes_per_sector);

        auto& disk = m_buffer[m_size++];
        disk.bytes_per_sector = drive_params.bytes_per_sector;
        disk.sectors = drive_params.total_sector_count;
        disk.handle = reinterpret_cast<void*>(drive_index);

        static constexpr size_t edd_v3 = 0x42;
        if (drive_params.buffer_size == edd_v3)
            disk.opaque_flags |= dma64_support_bit;

        if (++detected_disks == number_of_disks)
            return;
    }

    warnln("BIOS reported more disks than was detected? ({} vs {})",
           detected_disks, number_of_disks);
}

Span<Disk> BIOSDiskServices::list_disks()
{
    return { m_buffer, m_size };
}

Disk* BIOSDiskServices::disk_from_handle(void* handle)
{
    auto drive_id = reinterpret_cast<u32>(handle) & 0xFF;

    if (drive_id < first_drive_index || drive_id >= last_drive_index)
        return nullptr;

    auto* end = m_buffer + m_size;
    auto disk = lower_bound(m_buffer, end, handle);

    if (disk == end || disk->handle != handle)
        return nullptr;

    return disk;
}

bool BIOSDiskServices::read_blocks(void* handle, void* buffer, u64 sector, size_t blocks)
{
    auto* disk = disk_from_handle(handle);
    if (!disk)
        panic("read_blocks() called on invalid handle {}", handle);

    return do_read(*disk, buffer, sector * disk->bytes_per_sector, blocks * disk->bytes_per_sector);
}

bool BIOSDiskServices::read(void* handle, void* buffer, u64 offset, size_t bytes)
{
    auto* disk = disk_from_handle(handle);
    if (!disk)
        panic("read() called on invalid handle {}", handle);

    return do_read(*disk, buffer, offset, bytes);
}

bool BIOSDiskServices::do_read(const Disk& disk, void* buffer, u64 offset, size_t bytes)
{
    ASSERT(bytes != 0);

    // https://oldlinux.superglobalmegacorp.com/Linux.old/docs/interrupts/int-html/rb-0708.htm
    DiskAddressPacket packet {};
    packet.packet_size = sizeof(packet);

    RealModeRegisterState registers {};
    registers.edx = reinterpret_cast<u32>(disk.handle) & 0xFF;
    registers.esi = reinterpret_cast<u32>(&packet);

    auto check_read = [] (const RealModeRegisterState& registers) -> bool
    {
        if (registers.is_carry_set() || ((registers.eax & 0xFF00) != 0x0000)) {
            warnln("disk read failed, (ret={})", registers.eax);
            return false;
        }

        return true;
    };

    auto last_read_sector = (offset + bytes) / disk.bytes_per_sector;
    if (disk.sectors <= last_read_sector)
        panic("invalid read(..., at {} with {} bytes", offset, bytes);

    auto* byte_buffer = reinterpret_cast<u8*>(buffer);
    auto transfer_buffer_address = as_real_mode_address(g_transfer_buffer);

    auto current_sector = offset / disk.bytes_per_sector;
    auto sectors_to_read = bytes / disk.bytes_per_sector;

    offset -= current_sector * disk.bytes_per_sector;

    // round up sectors to read in case read is not aligned to sector size
    if (offset != 0 || (bytes % disk.bytes_per_sector))
        sectors_to_read++;

    for (;;) {
        u32 sectors_for_this_read = min(sectors_to_read, transfer_buffer_capacity / disk.bytes_per_sector);

        auto bytes_for_this_read = sectors_for_this_read * disk.bytes_per_sector;
        sectors_to_read -= sectors_for_this_read;

        registers.eax = 0x4200;
        packet.first_block = current_sector;
        packet.blocks_to_transfer = sectors_for_this_read;
        packet.buffer_segment = transfer_buffer_address.segment;
        packet.buffer_offset = transfer_buffer_address.offset;

        bios_call(0x13, &registers, &registers);
        if (!check_read(registers))
            return false;

        auto bytes_to_copy = min(bytes_for_this_read, bytes);
        copy_memory(&g_transfer_buffer[offset], byte_buffer, bytes_to_copy);

        bytes -= bytes_to_copy;
        if (bytes == 0)
            break;
        ASSERT(sectors_to_read != 0);
    }

    return true;
}
