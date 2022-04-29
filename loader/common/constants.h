#pragma once

#define KB ((unsigned long)1024)
#define MB ((unsigned long)1024 * KB)
#define GB ((unsigned long)1024 * MB)

#define ALIGN_UP_MASK(x, mask)   (((x) + (mask)) & ~(mask))
#define ALIGN_UP(x, val)         ALIGN_UP_MASK(x, (typeof(x))(val) - 1)

#define ALIGN_DOWN_MASK(x, mask) ((x) & ~(mask))
#define ALIGN_DOWN(x, val)       ALIGN_DOWN_MASK(x, (typeof(x))(val) - 1)

#define IS_ALIGNED_MASK(x, mask) (((x) & (mask)) == 0)
#define IS_ALIGNED(x, val)       IS_ALIGNED_MASK(x, (typeof(x))(val) - 1)

#define PAGE_SIZE 4096
#define PAGE_SHIFT 12
#define PAGE_ROUND_UP(size)   ALIGN_UP(size, PAGE_SIZE)
#define PAGE_ROUND_DOWN(size) ALIGN_DOWN(size, PAGE_SIZE)

#define HUGE_PAGE_SIZE (2 * MB)
#define HUGE_PAGE_ROUND_UP(size)   ALIGN_UP(size, HUGE_PAGE_SIZE)
#define HUGE_PAGE_ROUND_DOWN(size) ALIGN_DOWN(size, HUGE_PAGE_SIZE)

#define DIRECT_MAP_BASE  0xFFFF800000000000
#define HIGHER_HALF_BASE 0xFFFFFFFF80000000
