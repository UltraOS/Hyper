#include "common/log.h"
#include "common/panic.h"
#include "common/string.h"
#include "services.h"

#include "bios_memory_services.h"
#include "bios_video_services.h"
#include "bios_disk_services.h"

extern u8 a20_enabled;
extern u8 section_bss_begin;
extern u8 section_bss_end;

void bios_entry()
{
    struct services s;

    memzero(&section_bss_begin, &section_bss_end - &section_bss_begin);

    s.vs = video_services_init();

    if (!a20_enabled)
        panic("Failed to enable A20! Please report this issue.");

    s.ms = memory_services_init();
    s.ds = disk_services_init();

    loader_entry(&s);
}
