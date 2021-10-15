#pragma once

#define ATTRIBUTE_INVALID          0
#define ATTRIBUTE_SYSTEM_INFO      1
#define ATTRIBUTE_MEMORY_MAP       2
#define ATTRIBUTE_MODULE_INFO      3
#define ATTRIBUTE_COMMAND_LINE     4
#define ATTRIBUTE_FRAMEBUFFER_INFO 5
#define ATTRIBUTE_END              6

struct AttributeHeader {
    uint32_t type;
    uint32_t size_in_bytes;
};

#define PLATFORM_INVALID 0
#define PLATFORM_BIOS    1
#define PLATFORM_UEFI    2

struct SystemInfoAttribute {
    struct AttributeHeader header;
    uint32_t platform_type;
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

struct MemoryMapEntry {
    uint64_t physical_address;
    uint64_t size_in_bytes;
    uint64_t type;
};
#define MEMORY_MAP_ENTRY_COUNT(header) ((((header).size_in_bytes) - sizeof(AttributeHeader)) / sizeof(MemoryMapEntry))

struct MemoryMapAttribute {
    struct AttributeHeader header;
    struct MemoryMapEntry entries[];
};

struct ModuleInfoAttribute {
    struct AttributeHeader header;
    char name[64];
    uint64_t physical_address;
    uint64_t length;
};

struct CommandLineAttribute {
    struct AttributeHeader header;
    char text[];
};
#define COMMAND_LINE_LENGTH(header) (((header).size_in_bytes) - sizeof(AttributeHeader))

#define FORMAT_INVALID 0
#define FORMAT_RBG     1
#define FORMAT_RGBA    2

struct Framebuffer {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t format;
    uint64_t physical_address;
};

struct FramebufferAttribute {
    struct AttributeHeader header;
    struct Framebuffer framebuffer;
};

struct BootContext {
    struct AttributeHeader* attributes;
};
#define NEXT_ATTRIBUTE(current) ((AttributeHeader*)(((uint8_t*)(current)) + (current)->size_in_bytes))

#define MAGIC32 0x48595045
#define MAGIC64 0x48595045525f3634
