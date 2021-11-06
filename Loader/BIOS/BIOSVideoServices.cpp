#include "BIOSVideoServices.h"

#include "Common/Logger.h"
#include "Common/Utilities.h"
#include "BIOSCall.h"

static constexpr size_t mode_count_capacity = 256;
static VideoMode g_video_modes[mode_count_capacity] {};

struct PACKED SuperVGAInformation {
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
static_assert(sizeof(SuperVGAInformation) == 512);

struct PACKED ModeInformation {
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
static_assert(sizeof(ModeInformation) == 256);

struct PACKED EDID {
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

    struct PACKED {
        u8 x_resolution;
        u8 vertical_frequency : 6;
        u8 aspect_ratio : 2;
    } standard_timing_information[8];

    struct PACKED {
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
    } detailed_timing_descriptors[4];

    u8 number_of_extensions;
    u8 checksum;
};

static_assert(sizeof(EDID) == 128);

BIOSVideoServices BIOSVideoServices::create()
{
    return { g_video_modes, mode_count_capacity };
}

BIOSVideoServices::BIOSVideoServices(VideoMode* buffer, size_t capacity)
    : m_buffer(buffer)
    , m_capacity(capacity)
{
    initialize_legacy_tty();
}

void BIOSVideoServices::initialize_legacy_tty()
{
    RealModeRegisterState registers {};

    // 80x25 color text, https://stanislavs.org/helppc/int_10-0.html
    registers.eax = 0x03;

    // There's no way to check if this worked, so we're done.
    bios_call(0x10, &registers, &registers);

    // Disable cursor, https://stanislavs.org/helppc/int_10-1.html
    registers = {};
    registers.eax = 0x0100;
    registers.ecx = 0x2000;
    bios_call(0x10, &registers, &registers);
}

u16 BIOSVideoServices::as_attribute(Color color)
{
    switch (color)
    {
    default:
    case Color::WHITE:
        return 0x0F00;
    case Color::GRAY:
        return 0x0700;
    case Color::YELLOW:
        return 0x0E00;
    case Color::RED:
        return 0x0C00;
    case Color::BLUE:
        return 0x0900;
    case Color::GREEN:
        return 0x0A00;
    }
}

void BIOSVideoServices::tty_scroll()
{
    auto* vga_memory = reinterpret_cast<volatile u16*>(vga_address);

    for (size_t y = 0; y < (rows - 1); ++y) {
        for (size_t x = 0; x < columns; ++x) {
            vga_memory[y * columns + x] = vga_memory[(y + 1) * columns + x];
        }
    }

    for (size_t x = 0; x < columns; ++x)
        vga_memory[(rows - 1) * columns + x] = ' ';
}

bool BIOSVideoServices::tty_write(StringView text, Color color)
{
    if (!m_legacy_tty_available)
        return false;

    auto* vga_memory = reinterpret_cast<volatile u16*>(vga_address);

    for (char c : text) {
        if (c == '\n') {
            m_y++;
            m_x = 0;
            continue;
        }

        if (c == '\t') {
            m_x += 4;
            continue;
        }

        if (m_x >= columns) {
            m_x = 0;
            m_y++;
        }

        if (m_y >= rows) {
            m_y--;
            tty_scroll();
        }

        vga_memory[m_y * columns + m_x++] = as_attribute(color) | c;
    }

    return true;
}

bool BIOSVideoServices::check_vbe_call(const RealModeRegisterState& registers)
{
    auto al = registers.eax & 0xFF;
    auto ah = (registers.eax >> 8) & 0xFF;

    if (al != 0x4F || ah) {
        logger::warning("VBE call failed (ret=", registers.eax, ")");
        return false;
    }

    return true;
}

bool BIOSVideoServices::fetch_mode_info(u16 id, ModeInformation& mode_info)
{
    // https://oldlinux.superglobalmegacorp.com/Linux.old/docs/interrupts/int-html/rb-0274.htm

    zero_memory(&mode_info, sizeof(mode_info));
    RealModeRegisterState registers {};

    auto real_address = as_real_mode_address(&mode_info);

    registers.es = real_address.segment;
    registers.edi = real_address.offset;
    registers.ecx = id;
    registers.eax = 0x4F01;

    bios_call(0x10, &registers, &registers);

    return check_vbe_call(registers);
}

bool BIOSVideoServices::fetch_vga_info(SuperVGAInformation& vga_info)
{
    // https://oldlinux.superglobalmegacorp.com/Linux.old/docs/interrupts/int-html/rb-0273.htm

    // Apparently these are big endian strings
    static constexpr u32 ascii_vbe2 = 0x32454256;
    static constexpr u32 ascii_vesa = 0x41534556;

    vga_info.signature = ascii_vbe2;

    RealModeRegisterState registers {};
    registers.eax = 0x4F00;

    auto real_address = as_real_mode_address(&vga_info);
    registers.es = real_address.segment;
    registers.edi = real_address.offset;

    bios_call(0x10, &registers, &registers);

    if (!check_vbe_call(registers))
        return false;

    if (vga_info.signature != ascii_vesa) {
        logger::warning("VESA signature mismatch (got ", logger::hex, vga_info.signature, "), vs ", ascii_vesa);
        return false;
    }

    return true;
}

void BIOSVideoServices::fetch_all_video_modes()
{
    SuperVGAInformation vga_info {};
    if (!fetch_vga_info(vga_info))
        return;

    u8 vesa_major = vga_info.vesa_version >> 8;
    u8 vesa_minor = vga_info.vesa_version & 0xFF;
    logger::info("VESA version ", vesa_major, ".", vesa_minor);

    auto* oem_string = real_mode_address(vga_info.oem_name_segment, vga_info.oem_name_offset).as_pointer<const char>();
    logger::info("OEM name \"", oem_string, "\"");

    auto video_modes_ptr = real_mode_address(vga_info.supported_modes_list_segment, vga_info.supported_modes_list_offset);
    volatile auto* video_modes_list = video_modes_ptr.as_pointer<u16>();

    ModeInformation info {};

    while (*video_modes_list != 0xFFFF) {
        auto mode_id = *video_modes_list++;

        if (!fetch_mode_info(mode_id, info))
            return;

        auto mode_idx = m_size++;
        if (mode_idx >= mode_count_capacity) {
            logger::warning("Exceeded video mode storage capacity, skipping the rest");
            return;
        }

        // a bunch of sanity checks for the mode
        static constexpr u8 memory_model_direct_color = 0x06;
        if (info.memory_model_type != memory_model_direct_color)
            continue;

        bool is_vbe3 = vesa_major >= 3;
        auto& red_mask_size = is_vbe3 ? info.red_mask_size_linear : info.red_mask_size;
        auto& red_mask_shift = is_vbe3 ? info.red_mask_shift_linear : info.red_mask_shift;
        auto& green_mask_size = is_vbe3 ? info.green_mask_size_linear : info.green_mask_size;
        auto& green_mask_shift = is_vbe3 ? info.green_mask_shift_linear : info.green_mask_shift;
        auto& blue_mask_size = is_vbe3 ? info.blue_mask_size_linear : info.blue_mask_size;
        auto& blue_mask_shift = is_vbe3 ? info.blue_mask_shift_linear : info.blue_mask_shift;
        auto& reserved_mask_size = is_vbe3 ? info.reserved_mask_size_linear : info.reserved_mask_size;
        auto& reserved_mask_shift = is_vbe3 ? info.reserved_mask_shift_linear : info.reserved_mask_shift;

        if (red_mask_size != 8)
            continue;
        if (red_mask_shift != 16)
            continue;
        if (green_mask_size != 8)
            continue;
        if (green_mask_shift != 8)
            continue;
        if (blue_mask_size != 8)
            continue;
        if (blue_mask_shift != 0)
            continue;

        if (info.bits_per_pixel == 32) {
            if (reserved_mask_size != 8)
                continue;
            if (reserved_mask_shift != 24)
                continue;
        }

        auto& out_mode = m_buffer[mode_idx];
        out_mode.id = mode_id;
        out_mode.width = info.width;
        out_mode.height = info.height;
        out_mode.bpp = info.bits_per_pixel;
    }
}

void BIOSVideoServices::fetch_native_resolution()
{
    // https://oldlinux.superglobalmegacorp.com/Linux.old/docs/interrupts/int-html/rb-0308.htm

    EDID edid {};

    RealModeRegisterState registers {};
    registers.eax = 0x4F15;
    registers.ebx = 0x01;
    registers.edi = reinterpret_cast<u32>(&edid);
    bios_call(0x10, &registers, &registers);

    if (!check_vbe_call(registers)) {
        logger::warning("READ EDID call unsupported");
        return;
    }

    u8 checksum = 0;
    auto* bytes = reinterpret_cast<u8*>(&edid);
    for (size_t i = 0; i < sizeof(EDID); ++i)
        checksum += bytes[i];

    if (checksum != 0) {
        logger::warning("EDID checksum invalid (rem=", checksum, ")");
        return;
    }

    auto& td = edid.detailed_timing_descriptors[0];

    m_native_height = td.vertical_active_lines_lo;
    m_native_height |= td.vertical_active_lines_hi << 8;

    m_native_width = td.horizontal_active_pixels_lo;
    m_native_width |= td.horizontal_active_pixels_hi << 8;

    logger::info("detected native resolution ", m_native_width, "x", m_native_height);
}

Span<VideoMode> BIOSVideoServices::list_modes()
{
    return { m_buffer, m_size };
}

bool BIOSVideoServices::query_resolution(Resolution& out_resolution)
{
    if (m_native_width == 0 || m_native_height == 0)
        return false;

    out_resolution.width = m_native_width;
    out_resolution.height = m_native_height;
    return true;
}

bool BIOSVideoServices::do_set_mode(u16 id)
{
    // https://oldlinux.superglobalmegacorp.com/Linux.old/docs/interrupts/int-html/rb-0275.htm

    static constexpr u32 linear_framebuffer_bit = 1 << 14;

    RealModeRegisterState registers {};

    logger::info("setting video mode ", id);
    registers.ebx = id | linear_framebuffer_bit;
    registers.eax = 0x4F02;

    return check_vbe_call(registers);
}

bool BIOSVideoServices::set_mode(u32 id, Framebuffer& out_framebuffer)
{
    ModeInformation info {};
    if (!fetch_mode_info(id, info))
        return false;

    if (!do_set_mode(id))
        return false;

    out_framebuffer.bpp = info.bits_per_pixel;
    out_framebuffer.height = info.height;
    out_framebuffer.width = info.width;
    out_framebuffer.physical_address = info.framebuffer_address;

    if (info.bits_per_pixel == 24) {
        out_framebuffer.format = FORMAT_RBG;
    } else if (info.bits_per_pixel == 32) {
        out_framebuffer.format = FORMAT_RGBA;
    } else {
        out_framebuffer.format = FORMAT_INVALID;
        logger::warning("Set video mode with unsupported format (", info.bits_per_pixel, " bpp)");
    }

    m_legacy_tty_available = false;
    return true;
}
