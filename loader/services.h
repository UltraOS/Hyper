#pragma once

#include "common/types.h"
#include "ultra_protocol.h"

struct disk {
    u64 sectors;
    u16 bytes_per_sector;
    u16 opaque_flags;
    void *handle;
};

struct disk_services {
    /*
     * Lists all available disks.
     * count -> number of disks listed. can be zero.
     * Returns the pointer to the array of disks, can be NULL if count is zero.
     */
    struct disk *(*list_disks)(size_t *count);

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

struct video_mode {
    u32 width;
    u32 height;
    u32 bpp;
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
     * top_down -> indicates whether range should be allocated at the highest available region of memory
     *             but under upper_limit. If set to NO the lowest available range is picked.
     * Returns the address of the first byte of the allocated range if allocation succeeded, nullptr otherwise.
     */
    u64 (*allocate_pages)(size_t count, u64 upper_limit, u32 type, bool top_down);

    /*
     * Frees count pages starting at address.
     */
    void (*free_pages)(u64 address, size_t count);

    /*
     * Copies protocol-formatted memory map entries into buffer.
     * into_buffer -> pointer to the first byte of the buffer that receives memory map entries
     * (allowed to be nullptr if capacity is passed as 0).
     * capacity_in_bytes -> capacity of the buffer.
     * key -> set by the service provider and is a unique id of the current state of the map
     * that changes with every allocate/free call. Only set if capacity_in_bytes
     * was enough to receive the entire map.
     * Returns the number of bytes that would've been copied if buffer had enough capacity.
     */
    size_t (*copy_map)(struct memory_map_entry* into_buffer, size_t capacity_in_bytes, size_t* out_key);

    /*
     * Disables the service and makes caller the owner of the entire map.
     * key -> expected id of the current state of the memory map.
     * Returns true if key handover was successful and key matched the internal state,
     * otherwise key didn't match the internal state and memory map must be re-fetched.
     */
    bool (*handover)(size_t key);
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
};

void loader_entry(struct services *services);
