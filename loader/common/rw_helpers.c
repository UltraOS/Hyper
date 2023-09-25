#include "common/bug.h"
#include "common/rw_helpers.h"

u32 read_u32(void *ptr) { return *(u32*)ptr; }
u64 read_u32_zero_extend(void *ptr) { return read_u32(ptr); }

u64 read_u64(void *ptr) { return *(u64*)ptr; }

void write_u32(void *ptr, u32 val)
{
    u32 *dword = ptr;
    *dword = val;
}

void write_u32_u64(void *ptr, u64 val)
{
    write_u32(ptr, (u32)val);
}

void write_u32_checked_u64(void *ptr, u64 val)
{
    BUG_ON(val > 0xFFFFFFFF);
    write_u32_u64(ptr, val);
}

void write_u64(void *ptr, u64 val)
{
    u64 *qword = ptr;
    *qword = val;
}
