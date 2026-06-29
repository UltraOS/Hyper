#pragma once

#include "common/types.h"
#include "arch/constants.h"

/*
 * A single chunk of memory shared between subsystems that need a transient,
 * real-mode addressable bounce buffer. Sized to hold a full disk page.
 */
#define SCRATCH_BUFFER_SIZE PAGE_SIZE

typedef void (*scratch_buffer_borrowed_cb_t)(void *user);

/*
 * Borrow the shared scratch buffer for 'size' bytes. When another caller later
 * borrows it, 'on_evicted' (if non-NULL) is invoked with 'user' first, so a
 * borrower that caches data in the buffer can drop that cache before it's
 * overwritten. Re-borrowing as the current holder does not fire the callback.
 * 'size' must not exceed SCRATCH_BUFFER_SIZE.
 */
void *scratch_buffer_borrow(
    size_t size, scratch_buffer_borrowed_cb_t on_evicted, void *user
);
