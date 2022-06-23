#include "log.h"
#include "io.h"
#include "format.h"
#include "test_ctl.h"

void vprint(const char *msg, va_list vlist)
{
    static char log_buf[256];

    int chars;
    chars = vscnprintf(log_buf, sizeof(log_buf), msg, vlist);
    test_write_string(log_buf, chars);
}

void print(const char *msg, ...)
{
    va_list vlist;
    va_start(vlist, msg);
    vprint(msg, vlist);
    va_end(vlist);
}
