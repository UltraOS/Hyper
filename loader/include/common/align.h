#pragma once

#include "arch/constants.h"

#define ALIGN_UP_MASK(x, mask)   (((x) + (mask)) & ~(mask))
#define ALIGN_UP(x, val)         ALIGN_UP_MASK(x, (typeof(x))(val) - 1)

#define ALIGN_DOWN_MASK(x, mask) ((x) & ~(mask))
#define ALIGN_DOWN(x, val)       ALIGN_DOWN_MASK(x, (typeof(x))(val) - 1)

#define IS_ALIGNED_MASK(x, mask) (((x) & (mask)) == 0)
#define IS_ALIGNED(x, val)       IS_ALIGNED_MASK(x, (typeof(x))(val) - 1)

#define PAGE_ROUND_UP(size)   ALIGN_UP(size, PAGE_SIZE)
#define PAGE_ROUND_DOWN(size) ALIGN_DOWN(size, PAGE_SIZE)
