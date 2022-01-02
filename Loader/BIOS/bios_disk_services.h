#pragma once

#include "Services.h"

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

struct disk_services *disk_services_init();
