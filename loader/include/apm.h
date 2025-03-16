#pragma once

#include "common/types.h"

struct apm_info {
    u16 version;
    u16 flags;

    u16 pm_code_segment;
    u16 pm_code_segment_length;
    u32 pm_offset;

    u16 rm_code_segment;
    u16 rm_code_segment_length;

    u16 data_segment;
    u16 data_segment_length;
};
