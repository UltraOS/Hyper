#include "common/panic.h"
#include "services_impl.h"

bool services_offline = false;

void on_service_use_after_exit(const char *func)
{
    panic("Attempted to use %s() after exit!\n", func);
}
