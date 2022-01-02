#pragma once

#include "filesystem/filesystem.h"
#include "structures.h"
#include "common/range.h"

struct filesystem *try_create_fat32(const struct disk *d, struct range lba_range, void *first_page);
