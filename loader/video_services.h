#pragma once

#include "common/types.h"

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

/*
 * Number of video modes that can be queried.
 */
u32 vs_get_mode_count();

/*
 * Retrieves information about a video mode at idx.
 * idx -> video mode to retrieve.
 * out_mode -> pointer to data that receives video mode information.
 */
void vs_query_mode(size_t idx, struct video_mode *out_mode);

/*
 * Attempts to query native screen resolution.
 * out_resolution -> main display resolution in pixels.
 * Returns true if query succeeded, false otherwise.
 */
bool vs_query_native_resolution(struct resolution *out_resolution);

/*
 * Sets one of the modes returned from an earlier call to list_modes().
 * id -> id of the mode to be set.
 * out_framebuffer -> memory region and various data about the set mode.
 * Returns true if mode was set successfully, false otherwise.
 */
bool vs_set_mode(u32 id, struct framebuffer *out_framebuffer);

/*
* Writes string to the output device with the given color.
* text -> ascii string to output to the device.
* count -> number of characters to write.
* color -> color of the output message.
* Returns true if string was successfully written, false otherwise.
*/
bool vs_write_tty(const char *text, size_t count, enum color c);
