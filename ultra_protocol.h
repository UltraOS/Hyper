#pragma once

#include <stdint.h>

#define ULTRA_ATTRIBUTE_INVALID          0
#define ULTRA_ATTRIBUTE_PLATFORM_INFO    1
#define ULTRA_ATTRIBUTE_KERNEL_INFO      2
#define ULTRA_ATTRIBUTE_MEMORY_MAP       3
#define ULTRA_ATTRIBUTE_MODULE_INFO      4
#define ULTRA_ATTRIBUTE_COMMAND_LINE     5
#define ULTRA_ATTRIBUTE_FRAMEBUFFER_INFO 6

struct ultra_attribute_header {
    uint32_t type;
    uint32_t size_in_bytes;
};

#define ULTRA_PLATFORM_INVALID 0
#define ULTRA_PLATFORM_BIOS    1
#define ULTRA_PLATFORM_UEFI    2

struct ultra_platform_info_attribute {
    struct ultra_attribute_header header;
    uint32_t platform_type;

    uint16_t loader_major;
    uint16_t loader_minor;
    char loader_name[32];

    uint64_t acpi_rsdp_address;
};

#define ULTRA_PARTITION_TYPE_RAW 1
#define ULTRA_PARTITION_TYPE_MBR 2
#define ULTRA_PARTITION_TYPE_GPT 3

struct ultra_guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t  data4[8];
};

struct ultra_kernel_info_attribute {
    struct ultra_attribute_header header;

    uint64_t physical_base;
    uint64_t virtual_base;
    uint64_t range_length;

    uint64_t partition_type;

    // only valid if partition_type == PARTITION_TYPE_GPT
    struct ultra_guid disk_guid;
    struct ultra_guid partition_guid;

    // always valid
    uint32_t disk_index;
    uint32_t partition_index;

    char path_on_disk[256];
};

#define ULTRA_MEMORY_TYPE_INVALID            0x00000000
#define ULTRA_MEMORY_TYPE_FREE               0x00000001
#define ULTRA_MEMORY_TYPE_RESERVED           0x00000002
#define ULTRA_MEMORY_TYPE_RECLAIMABLE        0x00000003
#define ULTRA_MEMORY_TYPE_NVS                0x00000004
#define ULTRA_MEMORY_TYPE_LOADER_RECLAIMABLE 0xFFFF0001
#define ULTRA_MEMORY_TYPE_MODULE             0xFFFF0002
#define ULTRA_MEMORY_TYPE_KERNEL_STACK       0xFFFF0003
#define ULTRA_MEMORY_TYPE_KERNEL_BINARY      0xFFFF0004

struct ultra_memory_map_entry {
    uint64_t physical_address;
    uint64_t size_in_bytes;
    uint64_t type;
};
#define MEMORY_MAP_ENTRY_COUNT(header) ((((header).size_in_bytes) - sizeof(struct ultra_attribute_header)) / sizeof(struct ultra_memory_map_entry))

struct ultra_memory_map_attribute {
    struct ultra_attribute_header header;
    struct ultra_memory_map_entry entries[];
};

struct ultra_module_info_attribute {
    struct ultra_attribute_header header;
    char name[64];
    uint64_t physical_address;
    uint64_t length;
};

struct ultra_command_line_attribute {
    struct ultra_attribute_header header;
    char text[];
};

#define ULTRA_FB_FORMAT_INVALID 0
#define ULTRA_FB_FORMAT_RBG     1
#define ULTRA_FB_FORMAT_RGBA    2

struct ultra_framebuffer {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint16_t bpp;
    uint16_t format;
    uint64_t physical_address;
};

struct ultra_framebuffer_attribute {
    struct ultra_attribute_header header;
    struct ultra_framebuffer fb;
};

struct ultra_boot_context {
    uint32_t attribute_count;
    struct ultra_attribute_header attributes[];
};
#define NEXT_ATTRIBUTE(current) ((struct ultra_attribute_header*)(((uint8_t*)(current)) + (current)->size_in_bytes))

#define ULTRA_MAGIC 0x554c5442
