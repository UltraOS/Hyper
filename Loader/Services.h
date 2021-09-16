#pragma once

#include <stdint.h>
#include <stddef.h>

class DiskServices {
public:
    // Reads count sectors at sector offset.
    // true -> data was read successfully and buffer contents are valid.
    // false -> failed to read data, buffer contents are undefined.
    virtual bool read(void* buffer, size_t sector, size_t count) = 0;
};

struct VideoMode {
    uint32_t width;
    uint32_t height;
    uint32_t id;
};

struct Resolution {
    uint32_t width;
    uint32_t height;
};

struct Framebuffer {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t physical_address;
};

enum class Platform {
    BIOS,
    UEFI
};

class VideoServices {
public:
    // Lists up to max_count modes into buffer.
    // Retruns the number of modes that could've been listed.
    // A return value of 0 means no modes could be listed and should be considered fatal.
    virtual size_t list_modes(VideoMode* into_buffer, size_t max_count) = 0;

    // Returns the native screen resolution in out_resolution.
    // true -> successful query, returned resolution is verified to be valid.
    // false -> either unsuccessful query, or bogus data, out_resolution contents should be ignored.
    virtual bool query_resolution(Resolution& out_resolution) = 0;

    // Sets one of the modes returned from an earlier call to list_modes().
    // true -> mode was successfuly set, out_framebuffer is valid.
    // false -> modesetting failed, out_framebuffer contents should be ignored.
    virtual bool set_mode(uint32_t id, Framebuffer& out_framebuffer) = 0;
};

class Services {
public:
    Services(Platform platform, DiskServices& disk_services, VideoServices& video_services)
        : m_platform(platform)
        , m_disk_services(disk_services)
        , m_video_services(video_services)
    {
    }

    Platform platform() const { return m_platform; }

    DiskServices& disk_services() { return m_disk_services; }
    VideoServices& video_services() { return m_video_services; }

private:
    Platform m_platform;
    DiskServices& m_disk_services;
    VideoServices& m_video_services;
};
