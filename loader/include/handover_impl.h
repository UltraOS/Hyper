#pragma once

#include <stdbool.h>
#include "common/string_view.h"

extern bool handover_flags_map[32];
extern struct string_view handover_flags_to_string[32];

void initialize_flags_map(void);
