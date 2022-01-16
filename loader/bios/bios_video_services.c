#include "bios_video_services.h"
#include "bios_call.h"
#include "common/log.h"
#include "common/string.h"

#undef MSG_FMT
#define MSG_FMT(msg) "BIOS-VBE: " msg

struct PACKED super_vga_info {
    u32 signature; // 'VBE2' request -> 'VESA' response
    u16 vesa_version;
    u16 oem_name_offset;
    u16 oem_name_segment;
    u32 capabilities;
    u16 supported_modes_list_offset;
    u16 supported_modes_list_segment;
    u16 vram_64k_block_count;

    // VBE 2.0 vvvv
    u16 oem_software_version;
    u16 vendor_name_offset;
    u16 vendor_name_segment;
    u16 product_name_offset;
    u16 product_name_segment;
    u16 product_revision_offset;
    u16 product_revision_segment;
    u16 vbe_ef_version;
    u16 supported_accelerated_modes_list_offset;
    u16 supported_accelerated_modes_list_segment;
    char reserved1[216];
    char oem_scratchpad[256];
};
BUILD_BUG_ON(sizeof(struct super_vga_info) != 512);

struct PACKED mode_information {
    u16 attributes;
    u8 window_attributes_a;
    u8 window_attributes_b;
    u16 window_granularity_kb;
    u16 window_size_kb;
    u16 window_a_start_segment;
    u16 window_b_start_segment;
    u32 window_positioning_function;
    u16 bytes_per_scanline;
    u16 width;
    u16 height;
    u8 width_pixels_per_character;
    u8 height_pixels_per_character;
    u8 memory_plane_count;
    u8 bits_per_pixel;
    u8 bank_count;
    u8 memory_model_type;
    u8 kb_per_bank;
    u8 vram_video_pages;
    u8 reserved;

    // VBE 1.2+ vvvv
    u8 red_mask_size;
    u8 red_mask_shift;
    u8 green_mask_size;
    u8 green_mask_shift;
    u8 blue_mask_size;
    u8 blue_mask_shift;
    u8 reserved_mask_size;
    u8 reserved_mask_shift;
    u8 direct_color_mode_info;

    // VBE v2.0+ vvvv
    u32 framebuffer_address;
    u32 start_of_offscreen_memory;
    u16 kb_of_offscreen_memory;

    // VBE v3.0 vvvv
    u16 bytes_per_scanline_linear;
    u8 number_of_images_banked;
    u8 number_of_images_linear;
    u8 red_mask_size_linear;
    u8 red_mask_shift_linear;
    u8 green_mask_size_linear;
    u8 green_mask_shift_linear;
    u8 blue_mask_size_linear;
    u8 blue_mask_shift_linear;
    u8 reserved_mask_size_linear;
    u8 reserved_mask_shift_linear;
    u32 max_pixel_clock;

    char reserved1[190];
};
BUILD_BUG_ON(sizeof(struct mode_information) != 256);

struct PACKED timing_information {
    u8 x_resolution;
    u8 vertical_frequency : 6;
    u8 aspect_ratio : 2;
};

struct PACKED timing_descriptor {
    u16 pixel_clock;
    u8 horizontal_active_pixels_lo;
    u8 horizontal_blanking_pixels_lo;
    u8 horizontal_blanking_pixels_hi : 4;
    u8 horizontal_active_pixels_hi : 4;
    u8 vertical_active_lines_lo;
    u8 vertical_blanking_lines_lo;
    u8 vertical_blanking_lines_hi : 4;
    u8 vertical_active_lines_hi : 4;
    u8 horizontal_front_porch;
    u8 horizontal_sync_pulse_width;
    u8 vertical_sync_pulse_width_lo : 4;
    u8 vertical_front_porch_lo : 4;
    u8 vertical_sync_pulse_hi : 2;
    u8 vertical_front_porch_hi : 2;
    u8 horizontal_sync_pulse_width_hi : 2;
    u8 horizontal_front_porch_hi : 2;
    u8 horizontal_image_size_mm_lo;
    u8 vertical_image_size_mm_lo;
    u8 verticaL_image_size_mm_hi : 4;
    u8 horizontal_image_size_mm_hi : 4;
    u8 horizontal_border_pixels_half;
    u8 vertical_border_lines_half;
    u8 features_bitmap;
};

struct PACKED edid {
    u8 header[8];
    u16 manufacturer_id;
    u16 manufacturer_product_code;
    u32 serial_number;
    u8 week_of_manufacture;
    u8 year_of_manufacture;
    u8 edid_version;
    u8 edid_revision;
    u8 video_input_parameters;
    u8 horizontal_screen_size_cm;
    u8 vertical_screen_size_cm;
    u8 display_gamma;
    u8 features_bitmap;
    u8 red_green_least_significant_bits;
    u8 blue_white_least_significant_bits;
    u8 red_x_value_most_significant_bits;
    u8 red_y_value_most_significant_bits;
    u8 green_x_value_most_significant_bits;
    u8 green_y_value_most_significant_bits;
    u8 blue_x_value_most_significant_bits;
    u8 blue_y_value_most_significant_bits;
    u8 default_white_x_point_value_most_significant_bits;
    u8 default_white_y_point_value_most_significant_bits;
    u8 established_timing_bitmap[3];
    struct timing_information standard_timing_information[8];
    struct timing_descriptor detailed_timing_descriptors[4];
    u8 number_of_extensions;
    u8 checksum;
};
BUILD_BUG_ON(sizeof(struct edid) != 128);

static size_t native_width = 1024;
static size_t native_height = 768;

#define MODE_BUFFER_CAPACITY 256
static struct video_mode video_modes[MODE_BUFFER_CAPACITY];
static size_t video_mode_count = 0;

// ---- legacy TTY ----
#define VGA_ADDRESS 0xB8000
#define TTY_COLUMNS 80
#define TTY_ROWS 25

static size_t tty_x = 0;
static size_t tty_y = 0;
static bool legacy_tty_available = false;

static void initialize_legacy_tty()
{
    // 80x25 color text, https://stanislavs.org/helppc/int_10-0.html
    struct real_mode_regs regs = {
        .eax = 0x03
    };
    bios_call(0x10, &regs, &regs);

    // Disable cursor, https://stanislavs.org/helppc/int_10-1.html
    regs = (struct real_mode_regs) {
       .eax = 0x0100,
       .ecx = 0x2000
    };
    bios_call(0x10, &regs, &regs);

    legacy_tty_available = true;
}

static u16 color_as_attribute(enum color c)
{
    switch (c)
    {
        default:
        case COLOR_WHITE:
            return 0x0F00;
        case COLOR_GRAY:
            return 0x0700;
        case COLOR_YELLOW:
            return 0x0E00;
        case COLOR_RED:
            return 0x0C00;
        case COLOR_BLUE:
            return 0x0900;
        case COLOR_GREEN:
            return 0x0A00;
    }
}

static void tty_scroll()
{
    volatile u16 *vga_memory = (volatile u16*)VGA_ADDRESS;

    for (size_t y = 0; y < (TTY_ROWS - 1); ++y) {
        for (size_t x = 0; x < TTY_COLUMNS; ++x) {
            vga_memory[y * TTY_COLUMNS + x] = vga_memory[(y + 1) * TTY_COLUMNS + x];
        }
    }

    for (size_t x = 0; x < TTY_COLUMNS; ++x)
        vga_memory[(TTY_ROWS - 1) * TTY_COLUMNS + x] = ' ';
}

static bool tty_write(const char *text, size_t count, enum color col)
{
    for (size_t i = 0; i < count; ++i)
        asm volatile("outb %0, %1" ::"a"(text[i]), "Nd"(0xE9));

    volatile u16 *vga_memory = (volatile u16*)VGA_ADDRESS;
    bool no_write;
    char c;

    if (!legacy_tty_available)
        return false;

    for (size_t i = 0; i < count; ++i) {
        c = text[i];
        no_write = false;

        if (c == '\n') {
            tty_y++;
            tty_x = 0;
            no_write = true;
        }

        if (c == '\t') {
            tty_x += 4;
            no_write = true;
        }

        if (tty_x >= TTY_COLUMNS) {
            tty_x = 0;
            tty_y++;
        }

        if (tty_y >= TTY_ROWS) {
            tty_y = TTY_ROWS - 1;
            tty_scroll();
        }

        if (no_write)
            continue;

        vga_memory[tty_y * TTY_COLUMNS + tty_x++] = color_as_attribute(col) | c;
    }

    return true;
}

static bool check_vbe_call(u32 call_number, const struct real_mode_regs *regs)
{
    u32 al = regs->eax & 0xFF;
    u32 ah = (regs->eax >> 8) & 0xFF;

    if (al != 0x4F || ah) {
        print_warn("VBE call 0x%X failed (ret=%u)\n", call_number, regs->eax);
        return false;
    }

    return true;
}

static bool fetch_mode_info(u16 id, struct mode_information *mode_info)
{
    // https://oldlinux.superglobalmegacorp.com/Linux.old/docs/interrupts/int-html/rb-0274.htm
    struct real_mode_addr rm_addr;
    struct real_mode_regs regs = {
        .eax = 0x4F01,
        .ecx = id
    };

    as_real_mode_addr((u32)mode_info, &rm_addr);
    regs.edi = rm_addr.offset;
    regs.es = rm_addr.segment;

    bios_call(0x10, &regs, &regs);
    return check_vbe_call(0x4F01, &regs);
}

// Apparently these are big endian strings
#define ASCII_VBE2 0x32454256
#define ASCII_VESA 0x41534556

static bool fetch_vga_info(struct super_vga_info* vga_info)
{
    // https://oldlinux.superglobalmegacorp.com/Linux.old/docs/interrupts/int-html/rb-0273.htm
    struct real_mode_addr rm_addr;
    struct real_mode_regs regs = {
        .eax = 0x4F00
    };
    vga_info->signature = ASCII_VBE2;
    as_real_mode_addr((u32)vga_info, &rm_addr);
    regs.edi = rm_addr.offset;
    regs.es = rm_addr.segment;

    bios_call(0x10, &regs, &regs);

    if (!check_vbe_call(0x4F00, &regs))
        return false;

    if (vga_info->signature != ASCII_VESA) {
        print_warn("VESA signature mismatch: got 0x%08X vs 0x41534556\n", vga_info->signature);
        return false;
    }

    return true;
}

#define MEMORY_MODEL_DIRECT_COLOR 0x06

static bool validate_video_mode(struct mode_information *m, bool use_linear)
{
    if (m->memory_model_type != MEMORY_MODEL_DIRECT_COLOR)
        return false;

    if ((use_linear ? m->blue_mask_size_linear : m->blue_mask_size) != 8)
        return false;
    if ((use_linear ? m->blue_mask_shift_linear : m->blue_mask_shift) != 0)
        return false;
    if ((use_linear ? m->green_mask_size_linear : m->green_mask_size) != 8)
        return false;
    if ((use_linear ? m->green_mask_shift_linear : m->green_mask_shift) != 8)
        return false;
    if ((use_linear ? m->red_mask_size_linear : m->red_mask_size) != 8)
        return false;
    if ((use_linear ? m->red_mask_shift_linear : m->red_mask_shift) != 16)
        return false;

    if (m->bits_per_pixel == 32) {
        if ((use_linear ? m->reserved_mask_size_linear : m->reserved_mask_size) != 8)
            return false;
        if ((use_linear ? m->reserved_mask_shift_linear : m->reserved_mask_shift) != 24)
            return false;
    }

    return true;
}

static void fetch_all_video_modes()
{
    struct super_vga_info vga_info = { 0 };
    struct mode_information info;
    const char *oem_string;
    u8 vesa_major, vesa_minor;
    volatile u16 *video_modes_list;

    if (!fetch_vga_info(&vga_info))
        return;

    vesa_major = vga_info.vesa_version >> 8;
    vesa_minor = vga_info.vesa_version & 0xFF;
    oem_string = from_real_mode_addr(vga_info.oem_name_segment, vga_info.oem_name_offset);

    print_info("VESA version %u.%u\n", vesa_major, vesa_minor);
    print_info("OEM name \"%s\"\n", oem_string);

    video_modes_list = from_real_mode_addr(vga_info.supported_modes_list_segment,
                                           vga_info.supported_modes_list_offset);

    while (*video_modes_list != 0xFFFF) {
        u16 mode_id = *video_modes_list++;
        u32 buffer_idx;

        memzero(&info, sizeof(info));
        if (!fetch_mode_info(mode_id, &info))
            return;

        if (!validate_video_mode(&info, vesa_major >= 3))
            continue;

        buffer_idx = video_mode_count++;
        if (buffer_idx >= MODE_BUFFER_CAPACITY) {
            print_warn("Exceeded video mode storage capacity, skipping the rest\n");
            return;
        }

        video_modes[buffer_idx] = (struct video_mode) {
            .width = info.width,
            .height = info.height,
            .bpp = info.bits_per_pixel,
            .id = mode_id
        };
    }
}

void fetch_native_resolution()
{
    struct edid e = { 0 };
    u8 *e_bytes = (u8*)&e;
    struct timing_descriptor *td;
    u8 edid_checksum = 0;
    size_t i;

    // https://oldlinux.superglobalmegacorp.com/Linux.old/docs/interrupts/int-html/rb-0308.htm
    struct real_mode_regs regs = {
        .eax = 0x4F15,
        .ebx = 0x01,
        .edi = (u32)&e
    };
    bios_call(0x10, &regs, &regs);

    if (!check_vbe_call(0x4F15, &regs)) {
        print_warn("read EDID call unsupported\n");
        return;
    }

    for (i = 0; i < sizeof(struct edid); ++i)
        edid_checksum += e_bytes[i];

    if (edid_checksum != 0) {
        print_warn("EDID checksum invalid (rem=%u)\n", edid_checksum);
        return;
    }

    td = &e.detailed_timing_descriptors[0];

    native_height = td->vertical_active_lines_lo;
    native_height |= td->vertical_active_lines_hi << 8;

    native_width = td->horizontal_active_pixels_lo;
    native_width |= td->horizontal_active_pixels_hi << 8;

    print_info("detected native resoultion %zux%zu\n", native_width, native_height);
}

static struct video_mode *list_modes(size_t *count)
{
    *count = video_mode_count;
    return video_modes;
}

static bool query_resolution(struct resolution *out_resolution)
{
    if (native_width == 0 || native_height == 0)
        return false;

    out_resolution->width = native_width;
    out_resolution->height = native_height;
    return true;
}

#define LINEAR_FRAMEBUFFER_BIT (1 << 14)

static bool do_set_mode(u16 id)
{
    // https://oldlinux.superglobalmegacorp.com/Linux.old/docs/interrupts/int-html/rb-0275.htm
    struct real_mode_regs regs = {
        .eax = 0x4F02,
        .ebx = id | LINEAR_FRAMEBUFFER_BIT
    };

    print_info("setting video mode %hu\n", id);
    bios_call(0x10, &regs, &regs);

    return check_vbe_call(0x4F02, &regs);
}

static bool set_mode(u32 id, struct framebuffer *out_framebuffer)
{
    struct mode_information info = { 0 };

    if (!fetch_mode_info(id, &info))
        return false;

    if (!do_set_mode(id))
        return false;

    out_framebuffer->bpp = info.bits_per_pixel;
    out_framebuffer->height = info.height;
    out_framebuffer->width = info.width;
    out_framebuffer->physical_address = info.framebuffer_address;

    if (info.bits_per_pixel == 24) {
        out_framebuffer->format = FORMAT_RBG;
    } else if (info.bits_per_pixel == 32) {
        out_framebuffer->format = FORMAT_RGBA;
    } else {
        out_framebuffer->format = FORMAT_INVALID;
        print_warn("Set video mode with unsupported format (%d bpp)\n", info.bits_per_pixel);
    }

    legacy_tty_available = false;
    return true;
}

static struct video_services bios_video_services = {
    .list_modes = list_modes,
    .query_resolution = query_resolution,
    .set_mode = set_mode,
    .tty_write = tty_write
};

struct video_services *video_services_init()
{
    if (!legacy_tty_available)
        initialize_legacy_tty();

    logger_set_backend(&bios_video_services);

    if (video_mode_count == 0) {
        fetch_all_video_modes();
        fetch_native_resolution();
    }

    return &bios_video_services;
}





















