#include "common/cpuid.h"

void cpuid(u32 function, struct cpuid_res *id)
{
    asm volatile("cpuid"
        : "=a"(id->a), "=b"(id->b), "=c"(id->c), "=d"(id->d)
        : "a"(function), "c"(0));
}
