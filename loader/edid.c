#include "common/log.h"
#include "edid.h"

void edid_get_native_resolution(struct edid *e, size_t *native_width, size_t *native_height)
{
    struct timing_descriptor *td = &e->detailed_timing_descriptors[0];

    *native_height = td->vertical_active_lines_lo;
    *native_height |= td->vertical_active_lines_hi << 8;

    *native_width = td->horizontal_active_pixels_lo;
    *native_width |= td->horizontal_active_pixels_hi << 8;
}

u8 edid_calculate_checksum(struct edid *e)
{
    u8 edid_checksum = 0;
    u8 *e_bytes = (u8*)e;
    size_t i;

    for (i = 0; i < sizeof(struct edid); ++i)
        edid_checksum += e_bytes[i];

    return edid_checksum;
}
