#pragma once

#define KB ((unsigned long)1024)
#define MB ((unsigned long)1024 * KB)
#define GB ((unsigned long)1024 * MB)

#define PAGE_SIZE 4096
#define PAGE_SHIFT 12

#define HUGE_PAGE_SIZE (2 * MB)
#define HUGE_PAGE_SHIFT 21

#define DIRECT_MAP_BASE  0xFFFF800000000000
#define HIGHER_HALF_BASE 0xFFFFFFFF80000000
