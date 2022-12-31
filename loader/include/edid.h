#pragma once

#include "common/types.h"
#include "common/attributes.h"
#include "common/bug.h"

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

void edid_get_native_resolution(struct edid *e, size_t *native_width, size_t *native_height);
u8 edid_calculate_checksum(struct edid *e);
