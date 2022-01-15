#include "cpuid.h"

void cpuid(u32 function, struct cpuid_res *id)
{
    asm volatile("cpuid"
        : "=a"(id->a), "=b"(id->b), "=c"(id->c), "=d"(id->d)
        : "a"(function), "c"(0));
}

#define HIGHEST_IMPLEMENTED_FUNCTION_NUMBER 0x80000000
#define EXTENDED_PROCESSOR_INFO_FUNCTION_NUMBER 0x80000001
#define LONG_MODE_BIT (1 << 29)

bool cpu_supports_long_mode()
{
    struct cpuid_res id = { 0 };

    cpuid(HIGHEST_IMPLEMENTED_FUNCTION_NUMBER, &id);
    if (id.a < EXTENDED_PROCESSOR_INFO_FUNCTION_NUMBER)
        return false;

    cpuid(EXTENDED_PROCESSOR_INFO_FUNCTION_NUMBER, &id);
    return id.d & LONG_MODE_BIT;
}
