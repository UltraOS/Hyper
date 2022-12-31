#include <stdarg.h>
#include "common/panic.h"
#include "common/log.h"
#include "services.h"

void panic(const char *reason, ...)
{
    va_list vlist;
    va_start(vlist, reason);
    vprintlvl(LOG_LEVEL_ERR, reason, vlist);
    va_end(vlist);

    loader_abort();
}

void oops(const char *reason, ...)
{
    print_err("Oops!\n");

    va_list vlist;
    va_start(vlist, reason);
    vprintlvl(LOG_LEVEL_ERR, reason, vlist);
    va_end(vlist);

    loader_abort();
}
