#include "scratch_buffer.h"
#include "common/bug.h"

static u8 s_scratch_buffer[SCRATCH_BUFFER_SIZE];

static scratch_buffer_borrowed_cb_t s_on_evicted;
static void *s_user;

static void replace_owner(scratch_buffer_borrowed_cb_t on_evicted, void *user)
{
    // Same user still owns the buffer, we're good
    if (s_on_evicted == on_evicted && s_user == user)
        return;

    if (s_on_evicted)
        s_on_evicted(s_user);

    s_on_evicted = on_evicted;
    s_user = user;
}

void *scratch_buffer_borrow(
    size_t size, scratch_buffer_borrowed_cb_t on_evicted, void *user
)
{
    BUG_ON(size > SCRATCH_BUFFER_SIZE);

    replace_owner(on_evicted, user);

    return s_scratch_buffer;
}
