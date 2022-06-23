#include "fb_tty.h"
#include "fb_font.h"
#include "ultra_protocol.h"
#include "ultra_helpers.h"
#include "test_ctl.h"

static void *fb_ptr = NULL;
static size_t fb_pitch;
static size_t fb_width;
static size_t fb_height;
static size_t tty_x;
static size_t tty_y;
static size_t rows;
static size_t columns;

struct ultra_framebuffer *get_fb(struct ultra_boot_context *bctx)
{
    typedef struct ultra_framebuffer_attribute ufbattr;
    ufbattr *fbattr = (ufbattr*)find_attr(bctx, ULTRA_ATTRIBUTE_FRAMEBUFFER_INFO);

    return fbattr ? &fbattr->fb : NULL;
}

void fb_tty_init(struct ultra_boot_context *bctx)
{
    struct ultra_framebuffer *fb = get_fb(bctx);
    u32 expected_bpp;
    u32 expected_pitch_min;

    if (!fb) {
        print("Couldn't find FB info, framebuffer logging won't be available\n");
        return;
    }

    if (fb->width < 800 || fb->height < 600)
        test_fail("invalid framebuffer resolution %ux%u\n",
                  fb->width, fb->height);

    switch (fb->format) {
    default:
    case ULTRA_FB_FORMAT_INVALID:
        test_fail("bogus framebuffer format %u\n", fb->format);

    case ULTRA_FB_FORMAT_XRGB8888:
    case ULTRA_FB_FORMAT_RGBX8888:
        expected_bpp = 32;
        break;

    case ULTRA_FB_FORMAT_RGB888:
    case ULTRA_FB_FORMAT_BGR888:
        expected_bpp = 24;
        break;
    }

    if (fb->bpp != expected_bpp)
        test_fail("invalid bpp %u for format %u\n", fb->bpp, fb->format);

    expected_pitch_min = (fb->bpp / 8) * fb->width;
    if (fb->pitch < expected_pitch_min)
        test_fail("bogus framebuffer pitch %u\n", fb->pitch);

    if (fb->format != ULTRA_FB_FORMAT_XRGB8888)
        return;
    if (sizeof(void*) == 4 && fb->address > 0xFFFFFFFF)
        return;

    fb_pitch = fb->pitch;
    fb_width = fb->width;
    fb_height = fb->height;
    fb_ptr = (void*)fb->address;

    rows = fb_height / FONT_HEIGHT;
    columns = fb_width / FONT_WIDTH;
}

void fb_write_one(char c)
{
    size_t x_initial = FONT_WIDTH * tty_x;
    size_t y_initial = FONT_HEIGHT * tty_y;
    size_t y, x;

    for (y = 0; y < FONT_HEIGHT; ++y) {
        for (x = 0; x < FONT_WIDTH; ++x) {
            u32 *fb_at_y = fb_ptr + (y_initial + y) * fb_pitch;

            bool present = fb_font[(size_t)c][y] & (1 << x);
            fb_at_y[x_initial + x] = 0xFFFFFFFF * present;
        }
    }
}

static void fb_tty_newline()
{
   if (++tty_y >= rows)
       tty_y = 0;

   tty_x = 0;
}

void fb_tty_write(const char *str, size_t count)
{
    size_t i;

    if (!fb_ptr)
        return;

    for (i = 0; i < count; ++i)
    {
        char c = str[i];

        if (c == '\n') {
            fb_tty_newline();
            continue;
        }

        fb_write_one(c);

        if (++tty_x >= columns)
            fb_tty_newline();
    }
}
