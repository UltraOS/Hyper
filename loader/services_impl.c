#include "common/panic.h"

#include "services.h"
#include "services_impl.h"

bool services_offline = false;

void on_service_use_after_exit(const char *func)
{
    panic("Attempted to use %s() after exit!\n", func);
}

CTOR_SECTION_DEFINE_ITERATOR(cleanup_handler, cleanup_handlers);

void services_cleanup(void)
{
    cleanup_handler *handler;

    for (handler = cleanup_handlers_begin;
         handler < cleanup_handlers_end;
         ++handler)
    {
        (*handler)();
    }
}
