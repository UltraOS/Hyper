#include <stdarg.h>
#include "test_ctl.h"
#include "test_ctl_impl.h"

void test_fail(const char *reason, ...)
{
    va_list vlist;
    va_start(vlist, reason);
    test_vfail(reason, vlist);
    va_end(vlist);
}

void panic(const char *reason, ...)
{
    va_list vlist;
    va_start(vlist, reason);
    test_vfail(reason, vlist);
    va_end(vlist);
}

void oops(const char *reason, ...)
{
    va_list vlist;
    va_start(vlist, reason);
    test_vfail(reason, vlist);
    va_end(vlist);
}
