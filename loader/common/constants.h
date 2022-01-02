#pragma once

#define KB ((unsigned long)1024)
#define MB ((unsigned long)1024 * KB)
#define GB ((unsigned long)1024 * MB)

#define PAGE_SIZE 4096
#define PAGE_ROUND_UP(size) \
    (size & (PAGE_SIZE - 1)) ? ((size + PAGE_SIZE) & ~PAGE_SIZE) : size

#define HUGE_PAGE_SIZE (2 * MB)
#define HUGE_PAGE_ROUND_UP(size) \
    (size & (HUGE_PAGE_SIZE - 1)) ? ((size + HUGE_PAGE_SIZE) & ~HUGE_PAGE_SIZE) : size
