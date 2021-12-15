#pragma once

#include <stdint.h>

#define ATTRIBUTE_INVALID          0
#define ATTRIBUTE_PLATFORM_INFO    1
#define ATTRIBUTE_MEMORY_MAP       2
#define ATTRIBUTE_MODULE_INFO      3
#define ATTRIBUTE_COMMAND_LINE     4
#define ATTRIBUTE_FRAMEBUFFER_INFO 5
#define ATTRIBUTE_END              6

struct attribute_header {
    uint32_t type;
    uint32_t size_in_bytes;
};

#define PLATFORM_INVALID 0
#define PLATFORM_BIOS    1
#define PLATFORM_UEFI    2

struct platform_info_attribute {
    struct attribute_header header;
    uint32_t platform_type;

    uint16_t loader_major;
    uint16_t loader_minor;
    char loader_name[32];

    u64 acpi_rsdp_address;
};

#define PARTITION_TYPE_RAW 1
#define PARTITION_TYPE_MBR 2
#define PARTITION_TYPE_GPT 3

struct guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t  data4[8];
};

struct kernel_info_attribute {
    struct attribute_header header;

    u64 physical_base;
    u64 virtual_base;
    u64 range_length;

    u64 partition_type;

    // only valid if partition_type == PARTITION_TYPE_GPT
    struct guid disk_guid;
    struct guid partition_guid;

    // always valid
    u32 disk_index;
    u32 partition_index;

    char path_on_disk[256];
};

#define MEMORY_TYPE_INVALID            0
#define MEMORY_TYPE_FREE               1
#define MEMORY_TYPE_RESERVED           2
#define MEMORY_TYPE_RECLAIMABLE        3
#define MEMORY_TYPE_NVS                4
#define MEMORY_TYPE_LOADER_RECLAIMABLE 5
#define MEMORY_TYPE_MODULE             6
#define MEMORY_TYPE_KERNEL_STACK       7
#define MEMORY_TYPE_KERNEL_BINARY      8

struct memory_map_entry {
    uint64_t physical_address;
    uint64_t size_in_bytes;
    uint64_t type;
};
#define MEMORY_MAP_ENTRY_COUNT(header) ((((header).size_in_bytes) - sizeof(AttributeHeader)) / sizeof(MemoryMapEntry))

struct memory_map_attribute {
    struct attribute_header header;
    struct memory_map_entry entries[];
};

struct module_info_attribute {
    struct attribute_header header;
    char name[64];
    uint64_t physical_address;
    uint64_t length;
};

struct command_line_attribute {
    struct attribute_header header;
    uint32_t text_length;
    char text[];
};
#define COMMAND_LINE_LENGTH(header) (((header).size_in_bytes) - sizeof(AttributeHeader))

#define FORMAT_INVALID 0
#define FORMAT_RBG     1
#define FORMAT_RGBA    2

struct framebuffer {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint16_t bpp;
    uint16_t format;
    uint64_t physical_address;
};

struct framebuffer_attribute {
    struct attribute_header header;
    struct framebuffer framebuffer;
};

struct boot_context {
    u64 attribute_count;
    struct attribute_header attributes[];
};
#define NEXT_ATTRIBUTE(current) ((AttributeHeader*)(((uint8_t*)(current)) + (current)->size_in_bytes))

#define ULTRA_MAGIC 0x554c5442
