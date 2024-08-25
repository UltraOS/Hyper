#define MSG_FMT(msg) "BIOS-VBE: " msg

#include "common/log.h"
#include "common/string.h"
#include "bios_video_services.h"
#include "video_services.h"
#include "services_impl.h"
#include "bios_call.h"
#include "edid.h"

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

static size_t native_width;
static size_t native_height;

#define MODE_BUFFER_CAPACITY 256
static struct video_mode video_modes[MODE_BUFFER_CAPACITY];
static size_t video_mode_count = 0;
static u8 vesa_detected_major;

// ---- legacy TTY ----
#define VGA_ADDRESS 0xB8000
#define TTY_COLUMNS 80
#define TTY_ROWS 25

static size_t tty_x = 0;
static size_t tty_y = 0;
static bool legacy_tty_available = false;

static void initialize_legacy_tty(void)
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

static void tty_scroll(void)
{
    volatile u16 *vga_memory = (volatile u16*)VGA_ADDRESS;
    size_t x, y;

    for (y = 0; y < (TTY_ROWS - 1); ++y) {
        for (x = 0; x < TTY_COLUMNS; ++x) {
            vga_memory[y * TTY_COLUMNS + x] = vga_memory[(y + 1) * TTY_COLUMNS + x];
        }
    }

    for (x = 0; x < TTY_COLUMNS; ++x)
        vga_memory[(TTY_ROWS - 1) * TTY_COLUMNS + x] = ' ';
}

bool vs_write_tty(const char *text, size_t count, enum color col)
{
    volatile u16 *vga_memory = (volatile u16*)VGA_ADDRESS;
    size_t i;
    bool no_write;
    char c;

    if (!legacy_tty_available)
        return false;

    for (i = 0; i < count; ++i) {
        c = text[i];
        no_write = false;

        if (c == '\r')
            continue;

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

static u16 mode_fb_format(
    struct mode_information *m, u16 mode_id, bool use_linear
)
{
    u8 r_shift, g_shift, b_shift, x_shift;

    if (m->memory_model_type != MEMORY_MODEL_DIRECT_COLOR)
        return FB_FORMAT_INVALID;

    r_shift = use_linear ? m->red_mask_shift_linear : m->red_mask_shift;
    g_shift = use_linear ? m->green_mask_shift_linear : m->green_mask_shift;
    b_shift =  use_linear ? m->blue_mask_shift_linear : m->blue_mask_shift;
    x_shift = use_linear ? m->reserved_mask_shift_linear : m->reserved_mask_shift;

    // We only expose 8 bits per value framebuffer formats, so filter everything else out.
    if ((use_linear ? m->blue_mask_size_linear : m->blue_mask_size) != 8)
        return FB_FORMAT_INVALID;
    if ((use_linear ? m->green_mask_size_linear : m->green_mask_size) != 8)
        return FB_FORMAT_INVALID;
    if ((use_linear ? m->red_mask_size_linear : m->red_mask_size) != 8)
        return FB_FORMAT_INVALID;

    if (m->bits_per_pixel == 32) {
        u8 x_size;

        x_size = use_linear ? m->reserved_mask_size_linear :
                m->reserved_mask_size;

        /*
         * Some BIOSes don't bother filling the reserved component's shift and
         * size values, derive them from other components here.
         */
        if (unlikely(x_size == 0)) {
            x_size = 8;
            print_warn(
                "32-bpp mode %d with zeroed x-component size, assuming 8 bits\n",
                mode_id
            );

            if (x_shift == 0) {
                switch (r_shift + g_shift + b_shift) {
                case 24: // 0 + 8 + 16 [+ 24]
                    x_shift = 24;
                    break;
                case 32: // 0 + 8 + 24 [+ 16]
                    x_shift = 16;
                    break;
                case 40: // 0 + 16 + 24 [+ 8]
                    x_shift = 8;
                default:
                    break;
                }

                if (x_shift != 0) {
                    print_warn(
                        "32-bpp mode %d with zeroed x-component shift, "
                        "guessing %d bits\n", mode_id, x_shift
                    );
                }
            }
        }

        if (unlikely(x_size != 8))
            return FB_FORMAT_INVALID;
    }

    return fb_format_from_mask_shifts_8888(
        r_shift, g_shift, b_shift, x_shift, m->bits_per_pixel
    );
}

static void fetch_all_video_modes(void)
{
    struct super_vga_info vga_info = { 0 };
    struct mode_information info;
    const char *oem_string;
    u8 vesa_minor;
    volatile u16 *video_modes_list;

    if (!fetch_vga_info(&vga_info))
        return;

    vesa_detected_major = vga_info.vesa_version >> 8;
    vesa_minor = vga_info.vesa_version & 0xFF;
    oem_string = from_real_mode_addr(vga_info.oem_name_segment, vga_info.oem_name_offset);

    print_info("VESA version %u.%u\n", vesa_detected_major, vesa_minor);
    print_info("OEM name \"%s\"\n", oem_string);

    video_modes_list = from_real_mode_addr(vga_info.supported_modes_list_segment,
                                           vga_info.supported_modes_list_offset);

    while (*video_modes_list != 0xFFFF) {
        u16 mode_id = *video_modes_list++;
        u16 fb_format;
        u32 buffer_idx;

        memzero(&info, sizeof(info));
        if (!fetch_mode_info(mode_id, &info))
            return;

        fb_format = mode_fb_format(&info, mode_id, vesa_detected_major >= 3);
        if (fb_format == FB_FORMAT_INVALID)
            continue;

        if (video_mode_count == MODE_BUFFER_CAPACITY) {
            print_warn("exceeded video mode storage capacity, skipping the rest\n");
            return;
        }
        buffer_idx = video_mode_count++;

        print_info("video-mode[%u] %ux%u fmt: %s\n", buffer_idx, info.width, info.height,
                   fb_format_as_str(fb_format));

        video_modes[buffer_idx] = (struct video_mode) {
            .width = info.width,
            .height = info.height,
            .bpp = info.bits_per_pixel,
            .format = fb_format,
            .id = (mode_id << 16) | buffer_idx
        };
    }
}

void fetch_native_resolution(void)
{
    struct edid e = { 0 };
    u8 edid_checksum;

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

    edid_checksum = edid_calculate_checksum(&e);
    if (edid_checksum != 0) {
        print_warn("EDID checksum invalid (rem=%u)\n", edid_checksum);
        return;
    }

    edid_get_native_resolution(&e, &native_width, &native_height);
    print_info("detected native resolution %zux%zu\n", native_width, native_height);
}

u32 vs_get_mode_count(void)
{
    SERVICE_FUNCTION();
    return video_mode_count;
}

void vs_query_mode(size_t idx, struct video_mode *out_mode)
{
    SERVICE_FUNCTION();
    BUG_ON(idx >= video_mode_count);

    *out_mode = video_modes[idx];
}

bool vs_query_native_resolution(struct resolution *out_resolution)
{
    SERVICE_FUNCTION();

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

    print_info("setting video mode %hu...\n", id);
    bios_call(0x10, &regs, &regs);

    return check_vbe_call(0x4F02, &regs);
}

bool vs_set_mode(u32 id, struct framebuffer *out_framebuffer)
{
    SERVICE_FUNCTION();

    struct mode_information info = { 0 };
    u16 mode_id = id >> 16;
    u16 mode_idx = id & 0xFFFF;
    struct video_mode *vm;

    BUG_ON(mode_idx >= video_mode_count);
    vm = &video_modes[mode_idx];

    if (!fetch_mode_info(mode_id, &info))
        return false;

    if (!do_set_mode(mode_id))
        return false;

    out_framebuffer->width = vm->width;
    out_framebuffer->height = vm->height;
    out_framebuffer->pitch = (vesa_detected_major >= 3) ? info.bytes_per_scanline_linear : info.bytes_per_scanline;
    out_framebuffer->bpp = vm->bpp;
    out_framebuffer->physical_address = info.framebuffer_address;
    out_framebuffer->format = vm->format;

    legacy_tty_available = false;
    return true;
}

void bios_video_services_init(void)
{
    initialize_legacy_tty();
    fetch_all_video_modes();
    fetch_native_resolution();
}
