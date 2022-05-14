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

size_t mm_fixup(struct memory_map_entry *buf, size_t count,
                bool is_sorted, size_t fixup_cap);
size_t mm_compress(struct memory_map_entry *buf, size_t count);
size_t mm_force_compress(struct memory_map_entry *buf, size_t count);

ssize_t mm_find_first_that_contains(struct memory_map_entry *buf, u64 count,
                                    u64 value, bool allow_one_above);
