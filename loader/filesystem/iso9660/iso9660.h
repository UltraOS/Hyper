#pragma once

#include "filesystem/filesystem.h"
#include "filesystem/block_cache.h"

struct filesystem *try_create_iso9660(const struct disk *d, struct block_cache *bc);
