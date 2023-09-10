#include "common/panic.h"

#include "services.h"
#include "services_impl.h"
#include "handover.h"
#include "handover_impl.h"

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

static void do_detect_flags(void)
{
    static bool flags_detected = false;

    if (flags_detected)
        return;

    initialize_flags_map();
    handover_flags_map[HO_HIGHER_HALF_ONLY_BIT] = true;

    flags_detected = true;
}

bool handover_is_flag_supported(u32 flag)
{
    do_detect_flags();
    return handover_flags_map[__builtin_ctz(flag)];
}

void handover_ensure_supported_flags(u32 flags)
{
    size_t i;

    do_detect_flags();

    for (i = 0; i < sizeof(handover_flags_map); ++i) {
        u32 value = 1u << i;

        if ((flags & value) != value)
            continue;

        if (!handover_flags_map[i])
            oops("unsupported feature: '%pSV'\n", &handover_flags_to_string[i]);
    }
}
