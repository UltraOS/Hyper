#pragma once

#include "common/types.h"

struct disk {
    u64 sectors;
    void *handle;
    u8 block_shift;
};

struct disk_services {
    /*
     * Retrieves information about a disk at idx.
     * idx -> disk to retrieve.
     * out_disk -> pointer to data that receives disk information.
     */
    void (*query_disk)(size_t idx, struct disk *out_disk);

    /*
     * Number of disks that can be queried.
     */
    u32 disk_count;

    /*
     * Reads byte aligned data from disk.
     * handle -> one of disk handles returned by list_disks.
     * buffer -> first byte of the buffer that receives data.
     * offset -> byte offset of where to start reading.
     * bytes -> number of bytes to read.
     * Returns true if data was read successfully, false otherwise.
     */
    bool (*read)(void *handle, void *buffer, u64 offset, size_t bytes);

    /*
     * Reads sectors from a disk.
     * handle -> one of disk handles returned by list_disks.
     * buffer -> first byte of the buffer that receives data.
     * sector -> first sector from which data is read.
     * count -> number of sectors to read.
     * Returns true if data was read successfully, false otherwise.
     */
    bool (*read_blocks)(void *handle, void *buffer, u64 sector, size_t blocks);
};

#define FB_FORMAT_INVALID 0
#define FB_FORMAT_RGB888   1
#define FB_FORMAT_BGR888   2
#define FB_FORMAT_RGBX8888 3
#define FB_FORMAT_XRGB8888 4

static inline const char *fb_format_as_str(u16 fmt)
{
    switch (fmt) {
    case FB_FORMAT_RGB888:
        return "rgb888";
    case FB_FORMAT_BGR888:
        return "bgr888";
    case FB_FORMAT_RGBX8888:
        return "rgbx8888";
    case FB_FORMAT_XRGB8888:
        return "xrgb8888";
    case FB_FORMAT_INVALID:
    default:
        return "<invalid>";
    }
}

static inline u16 fb_format_from_mask_shifts_8888(u8 r_shift, u8 g_shift, u8 b_shift, u8 x_shift, u8 bpp)
{
    if (bpp == 24) {
        if (b_shift == 0 && g_shift == 8 && r_shift == 16)
            return FB_FORMAT_RGB888;
        if (r_shift == 0 && g_shift == 8 && b_shift == 16)
            return FB_FORMAT_BGR888;
    } else if (bpp == 32) {
        if (x_shift == 0 && b_shift == 8 && g_shift == 16 && r_shift == 24)
            return FB_FORMAT_RGBX8888;
        if (b_shift == 0 && g_shift == 8 && r_shift == 16 && x_shift == 24)
            return FB_FORMAT_XRGB8888;
    }

    return FB_FORMAT_INVALID;
}

struct video_mode {
    u32 width;
    u32 height;
    u16 bpp;
    u16 format;
    u32 id;
};

struct resolution {
    u32 width;
    u32 height;
};

enum color {
    COLOR_WHITE,
    COLOR_GRAY,
    COLOR_YELLOW,
    COLOR_RED,
    COLOR_BLUE,
    COLOR_GREEN,
};

struct framebuffer {
    u32 width;
    u32 height;
    u32 pitch;
    u16 bpp;
    u16 format;
    u64 physical_address;
};

struct video_services {
    /*
     * Lists all available video modes.
     * count -> number of video modes listed. can be zero.
     * Returns the pointer to the array of video modes, can be NULL if count is zero.
     */
    struct video_mode *(*list_modes)(size_t *count);

    /*
     * Attempts to query native screen resolution.
     * out_resolution -> main display resolution in pixels.
     * Returns true if query succeeded, false otherwise.
     */
    bool (*query_resolution)(struct resolution *out_resolution);

    /*
     * Sets one of the modes returned from an earlier call to list_modes().
     * id -> id of the mode to be set.
     * out_framebuffer -> memory region and various data about the set mode.
     * Returns true if mode was set successfully, false otherwise.
     */
    bool (*set_mode)(u32 id, struct framebuffer *out_framebuffer);

    /*
     * Writes string to the output device with the given color.
     * text -> ascii string to output to the device.
     * count -> number of characters to write.
     * color -> color of the output message.
     * Returns true if string was successfully written, false otherwise.
     */
    bool (*tty_write)(const char *text, size_t count, enum color c);
};

// These are consistent with the ACPI specification
#define MEMORY_TYPE_INVALID            0x00000000
#define MEMORY_TYPE_FREE               0x00000001
#define MEMORY_TYPE_RESERVED           0x00000002
#define MEMORY_TYPE_ACPI_RECLAIMABLE   0x00000003
#define MEMORY_TYPE_NVS                0x00000004
#define MEMORY_TYPE_UNUSABLE           0x00000005
#define MEMORY_TYPE_DISABLED           0x00000006
#define MEMORY_TYPE_PERSISTENT         0x00000007

/*
 * All memory allocated by the loader is marked with this by default,
 * the real underlying type is of course MEMORY_TYPE_FREE.
 */
#define MEMORY_TYPE_LOADER_RECLAIMABLE 0xFFFF0001

struct memory_map_entry {
    u64 physical_address;
    u64 size_in_bytes;
    u64 type;
};

/*
 * Converts memory_map_entry to the native protocol memory map entry format.
 * entry -> current entry to be converted.
 * buf -> pointer to the caller buffer where the entry should be written.
 *        buf is guaranteed to have enough capacity for the entry.
 */
typedef void (*entry_convert_func) (struct memory_map_entry *entry, void *buf);

struct memory_services {
    /*
     * Allocates count pages starting at address with type.
     * address -> page aligned address of the first byte of the range to allocate.
     * count -> number of 4096-byte pages to allocate.
     * type -> the type of range to allocate, must be one of the valid protocol values.
     * Returns the same value as 'address' if allocation succeeded, nullptr otherwise.
     */
    u64 (*allocate_pages_at)(u64 address, size_t count, u32 type);

    /*
     * Allocates count pages with type anywhere in available memory.
     * count -> number of 4096-byte pages to allocate.
     * upper_limit -> 1 + maximum allowed address within the allocated range.
     * type -> the type of range to allocate, must be one of the valid protocol values.
     * Returns the address of the first byte of the allocated range if allocation succeeded, nullptr otherwise.
     */
    u64 (*allocate_pages)(size_t count, u64 upper_limit, u32 type);

    /*
     * Frees count pages starting at address.
     */
    void (*free_pages)(u64 address, size_t count);

    /*
     * Copies protocol-formatted memory map entries into buffer.
     * buf -> pointer to the first byte of the buffer that receives memory map entries
     *        (allowed to be nullptr if capacity is passed as 0).
     * capacity -> number of elem_size elements that fit in the buffer.
     * elem_size -> size in bytes of the native memory map entry that will be written to 'buf'
     * out_key -> set by the service provider and is a unique id of the current state of the map
     *            that changes with every allocate/free call. Only set if capacity
     *            was enough to receive the entire map.
     * entry_convert -> a callback to use to convert each memory map entry to the native protocol format.
     *                  Can be NULL, in which case the memory_map_entry struct is copied verbatim
     *                  elem_size must be equal to sizeof(struct memory_map_entry) for this case.
     * Returns the number of entries that would've been copied if buffer had enough capacity.
     */
    size_t (*copy_map)(void *buf, size_t capacity, size_t elem_size,
                       size_t *out_key, entry_convert_func entry_convert);

    /*
     * Returns the address of the last byte of the last entry in the memory map + 1
     */
    u64 (*get_highest_memory_map_address)();
};

enum service_provider {
    SERVICE_PROVIDER_INVALID,
    SERVICE_PROVIDER_BIOS,
    SERVICE_PROVIDER_UEFI
};

struct services {
    struct disk_services *ds;
    struct video_services *vs;
    struct memory_services *ms;

    /*
     * Attempts to retrieve the RSDP structure location.
     * Returns a 16-byte aligned address of the structure if successful, NULL otherwise.
     */
    ptr_t (*get_rsdp)();

    /*
     * Disables all services and makes the caller the owner of all system resources.
     * sv -> services handle.
     * map_key -> expected id of the current state of the memory map.
     * Returns true if key handover was successful and key matched the internal state,
     * otherwise key didn't match the internal state and memory map must be re-fetched.
     */
    bool (*exit_all_services)(struct services *sv, size_t map_key);

    enum service_provider provider;
};

void loader_entry(struct services *services);
