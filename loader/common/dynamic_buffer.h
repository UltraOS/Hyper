#pragma once

#include "common/types.h"
#include "common/constants.h"
#include "allocator.h"

struct dynamic_buffer {
    size_t size;
    size_t capacity;
    size_t elem_size;
    void *buf;
};

#define DYNAMIC_BUFFER_GROWTH_INCREMENT PAGE_SIZE

bool dynamic_buffer_grow(struct dynamic_buffer *db);

static inline bool dynamic_buffer_init(struct dynamic_buffer *db, size_t elem_size, bool lazy)
{
    BUG_ON(elem_size == 0);
    BUG_ON(elem_size > DYNAMIC_BUFFER_GROWTH_INCREMENT);

    db->elem_size = elem_size;
    db->size = 0;
    db->capacity = 0;
    db->buf = NULL;

    return lazy ? true : dynamic_buffer_grow(db);
}

static void *dynamic_buffer_get_slot(struct dynamic_buffer *db, size_t i)
{
    BUG_ON(i >= db->size);
    return db->buf + (i * db->elem_size);
}

static inline void dynamic_buffer_release(struct dynamic_buffer *db)
{
    if (!db->capacity)
        return;

    free_bytes(db->buf, db->elem_size * db->capacity);
}

static inline void *dynamic_buffer_slot_alloc(struct dynamic_buffer *db)
{
    if (db->size == db->capacity && !dynamic_buffer_grow(db))
        return false;

    return dynamic_buffer_get_slot(db, db->size++);
}
