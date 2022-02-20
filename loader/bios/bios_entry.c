#include "common/log.h"
#include "common/panic.h"
#include "common/string.h"
#include "services.h"

#include "bios_memory_services.h"
#include "bios_video_services.h"
#include "bios_disk_services.h"
#include "bios_find_rsdp.h"

extern u8 a20_enabled;
extern u8 section_bss_begin;
extern u8 section_bss_end;

static bool bios_exit_all_services(struct services *sv, size_t key)
{
    if (!memory_services_release(key))
        return false;

    *sv = (struct services) {};
    return true;
}

void bios_entry(void)
{
    struct services s = {
        .provider = SERVICE_PROVIDER_BIOS,
        .get_rsdp = bios_find_rsdp,
        .exit_all_services = bios_exit_all_services
    };

    memzero(&section_bss_begin, &section_bss_end - &section_bss_begin);

    s.vs = video_services_init();

    BUG_ON(!a20_enabled);

    s.ms = memory_services_init();
    s.ds = disk_services_init();

    loader_entry(&s);
}
