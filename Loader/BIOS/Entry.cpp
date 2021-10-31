#include "Common/Logger.h"
#include "Common/Runtime.h"
#include "Common/Utilities.h"

#include "BIOSMemoryServices.h"
#include "BIOSVideoServices.h"
#include "BIOSDiskServices.h"

extern "C" bool a20_enabled;
extern "C" u8 section_bss_begin;
extern "C" u8 section_bss_end;

extern "C" void bios_entry()
{
    zero_memory(&section_bss_begin, &section_bss_end - &section_bss_begin);

    auto video_services = BIOSVideoServices::create();
    logger::set_backend(&video_services);

    if (!a20_enabled)
        panic("Failed to enable A20! Please report this issue.");

    video_services.fetch_all_video_modes();
    video_services.fetch_native_resolution();

    auto memory_services = BIOSMemoryServices::create();
    auto disk_services = BIOSDiskServices::create();

    Services services(disk_services, video_services, memory_services);
    loader_entry(services);
}
