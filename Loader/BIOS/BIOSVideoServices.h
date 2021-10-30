#pragma once

#include "Services.h"

struct RealModeRegisterState;
struct ModeInformation;
struct SuperVGAInformation;

class BIOSVideoServices final : public VideoServices {
public:
    static BIOSVideoServices create();

    Span<VideoMode> list_modes() override;

    bool query_resolution(Resolution& out_resolution) override;

    bool set_mode(u32 id, Framebuffer& out_framebuffer) override;

    bool tty_write(StringView text, Color color) override;

    void fetch_all_video_modes();
    void fetch_native_resolution();

private:
    BIOSVideoServices(VideoMode*, size_t capacity);

    static bool check_vbe_call(const RealModeRegisterState&);
    static bool do_set_mode(u16 id);

    bool fetch_mode_info(u16 id, ModeInformation&);
    bool fetch_vga_info(SuperVGAInformation&);

    static void initialize_legacy_tty();
    static u16 as_attribute(Color);
    static void tty_scroll();

private:
    VideoMode* m_buffer { nullptr };
    size_t m_capacity { 0 };
    size_t m_size { 0 };

    size_t m_native_width { 0 };
    size_t m_native_height { 0 };

    // ---- legacy TTY ----
    static constexpr ptr_t vga_address = 0xB8000;
    static constexpr size_t columns = 80;
    static constexpr size_t rows = 25;

    size_t m_x { 0 };
    size_t m_y { 0 };
    bool m_legacy_tty_available { true };
};
