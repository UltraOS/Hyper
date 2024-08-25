#pragma once

#include "common/attributes.h"
#include "common/types.h"

NORETURN
void on_service_use_after_exit(const char *func);

extern bool services_offline;

#define SERVICE_FUNCTION()                           \
    do {                                             \
        if (unlikely(services_offline))              \
            on_service_use_after_exit(__FUNCTION__); \
    } while (0)

struct memory_map_entry;
void mme_align_if_needed(struct memory_map_entry *me);
bool mme_is_valid(struct memory_map_entry *me);
void mme_insert(struct memory_map_entry *buf, struct memory_map_entry *me,
                size_t idx, size_t count);

void mm_sort(struct memory_map_entry *buf, size_t count);


#define FIXUP_UNSORTED                   (1 << 0)
#define FIXUP_IF_DIRTY                   (1 << 1)
#define FIXUP_OVERLAP_RESOLVE            (1 << 2)
#define FIXUP_OVERLAP_INTENTIONAL        (1 << 3)
#define FIXUP_NO_PRESERVE_LOADER_RECLAIM (1 << 4)

size_t mm_fixup(struct memory_map_entry *buf, size_t count, size_t cap, u8 flags);

ssize_t mm_find_first_that_contains(struct memory_map_entry *buf, u64 count,
                                    u64 value, bool allow_one_above);

typedef void (*cleanup_handler)(void);

#define DECLARE_CLEANUP_HANDLER(handler)         \
    static cleanup_handler CONCAT(handler, hook) \
           CTOR_SECTION(cleanup_handlers) USED = &handler
