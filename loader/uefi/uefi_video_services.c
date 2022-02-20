#include "structures.h"
#include "uefi_globals.h"
#include "uefi_helpers.h"
#include "services.h"
#include "edid.h"
#include "common/log.h"

#undef MSG_FMT
#define MSG_FMT(msg) "UEFI-GOP: " msg

static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *conout = NULL;
static EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx = NULL;
static size_t native_width = 0;
static size_t native_height = 0;
static struct video_mode *video_modes = NULL;
static size_t mode_count = 0;

static struct video_mode *uefi_list_modes(size_t *count)
{
    *count = mode_count;
    return video_modes;
}

static bool uefi_query_resolution(struct resolution *out_resolution)
{
    if (!native_height || !native_width)
        return false;

    out_resolution->width = native_width;
    out_resolution->height = native_height;
    return true;
}

static bool uefi_set_mode(u32 id, struct framebuffer *out_framebuffer)
{
    EFI_STATUS ret;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mode_info;
    BUG_ON(!gfx);

    print_info("setting video mode %u...\n", id);

    ret = gfx->SetMode(gfx, id);
    if (unlikely_efi_error(ret)) {
        struct string_view err_msg = uefi_status_to_string(ret);
        print_warn("failed to set video mode %u: %pSV\n", id, &err_msg);
        return false;
    }

    if (unlikely(!gfx->Mode)) {
        print_warn("mode set successfully but EFI_GRAPHICS_OUTPUT_PROTOCOL::Mode is null?\n");
        return false;
    }

    mode_info = gfx->Mode->Info;
    if (unlikely(!mode_info)) {
        print_warn("mode set successfully but no mode information available?\n");
        return false;
    }
    if (unlikely(gfx->Mode->SizeOfInfo != sizeof(*mode_info))) {
        print_warn("unexpected mode info: expected %zu got %zu\n",
                   sizeof(*mode_info), gfx->Mode->SizeOfInfo);
        return false;
    }

    *out_framebuffer = (struct framebuffer) {
        .width = mode_info->HorizontalResolution,
        .height = mode_info->VerticalResolution,
        .physical_address = gfx->Mode->FrameBufferBase,

        // This is sort of hardcoded for now because we only accept BGRA modes
        // TODO: unhardcode
        .pitch = mode_info->PixelsPerScanLine * 4,
        .bpp = 32,
        .format = FB_FORMAT_XRGB8888,
    };
    return true;
}

static UINTN as_efi_color(enum color c)
{
    switch (c) {
    default:
    case COLOR_WHITE:
        return EFI_WHITE;
    case COLOR_GRAY:
        return EFI_LIGHTGRAY;
    case COLOR_YELLOW:
        return EFI_YELLOW;
    case COLOR_RED:
        return EFI_RED;
    case COLOR_BLUE:
        return EFI_BLUE;
    case COLOR_GREEN:
        return EFI_GREEN;
    }
}

#define MAX_CHARS_PER_WRITE 255
#define TTY_FLUSH_BUF()                                                      \
    do {                                                                     \
        wide_buf[w_off] = 0;                                                 \
        if (unlikely(conout->OutputString(conout, wide_buf) != EFI_SUCCESS)) \
            return false;                                                    \
        w_off = 0;                                                           \
    } while (0)

static bool uefi_tty_write(const char *text, size_t count, enum color col)
{
    static CHAR16 wide_buf[MAX_CHARS_PER_WRITE + 1];
    UINTN color = as_efi_color(col);
    size_t c_off, w_off = 0;

    if (unlikely(!count))
        return true;
    if (unlikely(conout->SetAttribute(conout, color) != EFI_SUCCESS))
        return false;

    for (c_off = 0; c_off < count; ++c_off) {
        char c = text[c_off];
        size_t chars_to_write = 1 + (c == '\n');

        if ((MAX_CHARS_PER_WRITE - w_off) < chars_to_write)
            TTY_FLUSH_BUF();

        // Write both \r and \n for newline
        if (c == '\n')
            wide_buf[w_off++] = L'\r';
        wide_buf[w_off++] = (CHAR16)c;
    }

    if (w_off)
        TTY_FLUSH_BUF();

    return conout->SetAttribute(conout, EFI_LIGHTGRAY) == EFI_SUCCESS;
}

static struct video_services uefi_video_services = {
    .list_modes = uefi_list_modes,
    .query_resolution = uefi_query_resolution,
    .set_mode = uefi_set_mode,
    .tty_write = uefi_tty_write
};

static void tty_init()
{
    EFI_STATUS res;
    INT32 mode;
    UINTN best_mode = 0, max_rows = 0, max_cols = 0;
    conout = g_st->ConOut;

    res = conout->Reset(conout, TRUE);
    // TODO: handle better
    BUG_ON(res != EFI_SUCCESS);

    // TODO: handle not being able to find a mode
    for (mode = 0; mode < conout->Mode->MaxMode; ++mode) {
        UINTN cols = 0, rows = 0;

        if (conout->QueryMode(conout, mode, &cols, &rows) != EFI_SUCCESS)
            continue;

        if (cols >= max_cols && rows >= max_rows) {
            max_cols = cols;
            max_rows = rows;
            best_mode = mode;
        }
    }

    res = conout->SetMode(conout, best_mode);
    // TODO: handle better
    BUG_ON(res != EFI_SUCCESS);

    conout->EnableCursor(conout, FALSE);
    print_info("set tty mode %zu cols x %zu rows\n", max_cols, max_rows);
}

static void edid_init(EFI_EDID_ACTIVE_PROTOCOL *edid)
{
    u8 cksm = edid_calculate_checksum((struct edid*)edid->Edid);
    if (cksm != 0) {
        print_warn("invalid EDID checksum (rem=%u)\n", cksm);
        return;
    }

    edid_get_native_resolution((struct edid*)edid->Edid, &native_width, &native_height);
    print_info("detected native resolution %zux%zu\n", native_width, native_height);
}

static EFI_HANDLE choose_gop_handle(EFI_HANDLE *handles, UINTN handle_count)
{
    EFI_STATUS ret;
    EFI_GUID dev_path_guid = EFI_DEVICE_PATH_PROTOCOL_GUID;
    size_t i;

    // If there's only one handle assume it's real, we don't have better alternatives anyway
    if (handle_count == 1)
        return handles[0];

    /*
     * Filter out fake GOP handles (those that don't have a device path),
     * for now pick the first one that doesn't fail. Fake handles are likely
     * to not have a valid EDID blob and are overall useless.
     */
    for (i = 0; i < handle_count; ++i) {
        EFI_DEVICE_PATH_PROTOCOL *proto;

        ret = g_st->BootServices->HandleProtocol(handles[i], &dev_path_guid, (void**)&proto);
        if (!EFI_ERROR(ret))
            return handles[i];

        if (unlikely(ret != EFI_UNSUPPORTED)) {
            struct string_view err_msg = uefi_status_to_string(ret);
            print_warn("unexpected error for HandleProtocol(): %pSV\n", &err_msg);
        }
    }

    /*
     * Probably some firmware bug, but none of the handles have a valid device path.
     * Just return the first one and hope for the best.
     */
    return handles[0];
}

static void gfx_modes_init()
{
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mode_info;
    size_t i, mode_size;

    if (!uefi_pool_alloc(EfiLoaderData, sizeof(struct video_mode), gfx->Mode->MaxMode, (void**)&video_modes))
        return;

    for (i = 0; i < gfx->Mode->MaxMode; ++i) {
        EFI_STATUS ret = gfx->QueryMode(gfx, i, &mode_size, &mode_info);
        if (EFI_ERROR(ret)) {
            struct string_view err_msg = uefi_status_to_string(ret);
            print_warn("QueryMode(%zu) failed: %pSV\n", i, &err_msg);
            continue;
        }

        if (mode_size != sizeof(*mode_info)) {
            print_warn("unexpected GOP mode buffer size, expected %zu got %zu\n",
                       sizeof(*mode_info), mode_size);
            continue;
        }

        print_info("video-mode[%zu] %ux%u fmt: %u\n", i, mode_info->HorizontalResolution,
                   mode_info->VerticalResolution, mode_info->PixelFormat);

        // We don't support other modes for now, so skip it
        if (mode_info->PixelFormat != PixelBlueGreenRedReserved8BitPerColor)
            continue;

        video_modes[mode_count++] = (struct video_mode) {
            .width = mode_info->HorizontalResolution,
            .height = mode_info->VerticalResolution,
            .bpp = 32,
            .id = i
        };
    }
}

static void gop_init()
{
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GUID active_edid_guid = EFI_EDID_ACTIVE_PROTOCOL_GUID;
    EFI_GUID discovered_edid_guid = EFI_EDID_DISCOVERED_PROTOCOL_GUID;
    EFI_EDID_ACTIVE_PROTOCOL *edid_blob;
    EFI_HANDLE *gop_handles, picked_handle = NULL;
    EFI_STATUS ret;
    UINTN handle_count;

    if (!uefi_get_protocol_handles(&gop_guid, &gop_handles, &handle_count)) {
        print_warn("no GOP handles found, graphics won't be available\n");
        return;
    }

    picked_handle = choose_gop_handle(gop_handles, handle_count);
    g_st->BootServices->FreePool(gop_handles);

    ret = g_st->BootServices->HandleProtocol(picked_handle, &gop_guid, (void**)&gfx);
    if (unlikely_efi_error(ret)) {
        struct string_view err_msg = uefi_status_to_string(ret);
        print_warn("unexpected error for GOP handle: %pSV, graphics won't be available\n", &err_msg);
        return;
    }

    gfx_modes_init();

    ret = g_st->BootServices->HandleProtocol(picked_handle, &active_edid_guid, (void**)&edid_blob);
    if (EFI_ERROR(ret))
        ret = g_st->BootServices->HandleProtocol(picked_handle, &discovered_edid_guid, (void**)&edid_blob);

    if (EFI_ERROR(ret)) {
        struct string_view err_msg = uefi_status_to_string(ret);
        print_warn("failed to retrieve EDID blob: %pSV\n", &err_msg);
        return;
    }

    if (edid_blob->SizeOfEdid == 0) {
        print_warn("got an empty EDID blob\n");
        return;
    }

    if (edid_blob->SizeOfEdid != sizeof(struct edid)) {
        print_warn("unexpected EDID blob size, expected %zu got %u\n",
                   sizeof(struct edid), edid_blob->SizeOfEdid);
        return;
    }

    edid_init(edid_blob);
}

struct video_services *video_services_init()
{
    logger_set_backend(&uefi_video_services);
    tty_init();
    gop_init();

    return &uefi_video_services;
}
