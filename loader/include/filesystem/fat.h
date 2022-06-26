#pragma once

#include "filesystem/filesystem.h"
#include "common/range.h"

struct filesystem *try_create_fat(const struct disk *d, struct range lba_range,
                                  struct block_cache *bc);
