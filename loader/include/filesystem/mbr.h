#pragma once

#include "disk_services.h"
#include "block_cache.h"

bool mbr_initialize(const struct disk *d, struct block_cache *bc);
