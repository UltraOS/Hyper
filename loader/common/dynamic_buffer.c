#include "common/dynamic_buffer.h"
#include "common/string.h"

bool dynamic_buffer_grow(struct dynamic_buffer *db)
{
    size_t entries_per_inc = DYNAMIC_BUFFER_GROWTH_INCREMENT / db->elem_size;
    size_t new_capacity = db->capacity + entries_per_inc;
    size_t old_cap_bytes = db->capacity * db->elem_size;

    void *new_buf = allocate_bytes(new_capacity * db->elem_size);
    if (unlikely(!new_buf))
        return false;

    if (old_cap_bytes) {
        memcpy(new_buf, db->buf, old_cap_bytes);
        free_bytes(db->buf, old_cap_bytes);
    }

    db->buf = new_buf;
    db->capacity = new_capacity;
    return true;
}
