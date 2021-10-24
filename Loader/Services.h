#pragma once

#include "Common/Types.h"
#include "Common/StringView.h"
#include "Common/Span.h"

#include "Protocol.h"

struct Disk {
    u64 sectors;
    u32 bytes_per_sector;
    u32 id;
};

class DiskServices {
public:
    // Lists all available disks.
    virtual Span<Disk> list_disks() = 0;

    // Reads sectors from a disk.
    // id -> one of disk ids returned by list_disks.
    // buffer -> first byte of the buffer that receives data.
    // sector -> first sector from which data is read.
    // count -> number of sectors to read.
    // Returns true if data was read successfully, false otherwise.
    virtual bool read(u32 id, void* buffer, u64 sector, size_t count) = 0;
};

struct VideoMode {
    u32 width;
    u32 height;
    u32 bpp;
    u32 id;
};

struct Resolution {
    u32 width;
    u32 height;
};

class VideoServices {
public:
    // Lists all available video modes.
    virtual Span<VideoMode> list_modes() = 0;

    // Attempts to query native screen resolution.
    // out_resolution -> main display resolution in pixels.
    // Returns true if query succeeded, false otherwise.
    virtual bool query_resolution(Resolution& out_resolution) = 0;

    // Sets one of the modes returned from an earlier call to list_modes().
    // id -> id of the mode to be set.
    // out_framebuffer -> memory region and various data about the set mode.
    // Returns true if mode was set successfully, false otherwise.
    virtual bool set_mode(u32 id, Framebuffer& out_framebuffer) = 0;
};

enum class TopDown {
    YES,
    NO
};

class MemoryServices {
public:
    // Allocates count pages starting at address with type.
    // address -> page aligned address of the first byte of the range to allocate.
    // count -> number of 4096-byte pages to allocate.
    // type -> the type of range to allocate, must be one of the valid protocol values.
    // Returns the same value as 'address' if allocation succeeded, nullptr otherwise.
    virtual Address64 allocate_pages_at(Address64 address, size_t count, u32 type) = 0;

    // Allocates count pages with type anywhere in available memory.
    // count -> number of 4096-byte pages to allocate.
    // upper_limit -> 1 + maximum allowed address within the allocated range.
    // type -> the type of range to allocate, must be one of the valid protocol values.
    // TopDown -> indicates whether range should be allocated at the highest available region of memory
    //            but under upper_limit. If set to NO the lowest available range is picked.
    // Returns the address of the first byte of the allocated range if allocation succeeded, nullptr otherwise.
    virtual Address64 allocate_pages(size_t count, Address64 upper_limit, u32 type, TopDown) = 0;

    // Frees count pages starting at address.
    virtual void free_pages(Address64 address, size_t count) = 0;

    // Copies protocol-formatted memory map entries into buffer.
    // into_buffer -> pointer to the first byte of the buffer that receives memory map entries
    //                (allowed to be nullptr if capacity is passed as 0).
    // capacity_in_bytes -> capacity of the buffer.
    // key -> set by the service provider and is a unique id of the current state of the map
    //        that changes with every allocate/free call. Only set if capacity_in_bytes
    //        was enough to receive the entire map.
    // Returns the number of bytes that would've been copied if buffer had enough capacity.
    virtual size_t copy_map(MemoryMapEntry* into_buffer, size_t capacity_in_bytes, size_t& out_key) = 0;

    // Disables the service and makes caller the owner of the entire map.
    // key -> expected id of the current state of the memory map.
    // Returns true if key handover was successful and key matched the internal state,
    // otherwise key didn't match the internal state and memory map must be refetched.
    virtual bool handover(size_t key) = 0;
};

enum class Color {
    WHITE,
    GRAY,
    YELLOW,
    RED,
    BLUE,
    GREEN,
};

class TTYServices {
public:
    // Writes string to the output device with the given color.
    // test -> ascii string to output to the device.
    // color -> color of the output message.
    // Returns true if string was successfully written, false otherwise.
    virtual bool write(StringView text, Color color) = 0;

    // Returns the output device resolution in characters.
    virtual Resolution resolution() const = 0;

    // Returns true if the service is currently available.
    // This might change after setting a video mode.
    virtual bool is_available() const = 0;
};

class Services {
public:
    Services(DiskServices& disk_services, VideoServices& video_services, MemoryServices& memory_services, TTYServices& tty_services)
        : m_disk_services(disk_services)
        , m_video_services(video_services)
        , m_memory_services(memory_services)
        , m_tty_services(tty_services)
    {
    }

    DiskServices& disk_services() { return m_disk_services; }
    VideoServices& video_services() { return m_video_services; }
    MemoryServices& memory_services() { return m_memory_services; }
    TTYServices& tty_services() { return m_tty_services; }

private:
    DiskServices& m_disk_services;
    VideoServices& m_video_services;
    MemoryServices& m_memory_services;
    TTYServices& m_tty_services;
};

// Entrypoint implemented by the loader
void loader_entry(Services& services);
