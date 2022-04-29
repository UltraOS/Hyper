#include "common/log.h"
#include "common/panic.h"
#include "common/string.h"
#include "services.h"
#include "services_impl.h"

#include "bios_memory_services.h"
#include "bios_video_services.h"
#include "bios_disk_services.h"
#include "bios_call.h"

extern u8 a20_enabled;
extern u8 section_bss_begin;
extern u8 section_bss_end;

bool services_exit_all(size_t key)
{
    SERVICE_FUNCTION();

    services_offline = bios_memory_services_check_key(key);
    return services_offline;
}

enum service_provider services_get_provider(void)
{
    return SERVICE_PROVIDER_BIOS;
}

void loader_abort(void)
{
    /*
     * INT 0x16, AH = 0x01
     * https://oldlinux.superglobalmegacorp.com/Linux.old/docs/interrupts/int-html/rb-1755.htm
     *
     * INT 0x16, AH = 0x00
     * https://oldlinux.superglobalmegacorp.com/Linux.old/docs/interrupts/int-html/rb-1754.htm
     */

    struct real_mode_regs regs = { 0 };

    for (;;) {
        // Any keystrokes pending?
        regs.eax = 0x0100;
        regs.flags = 0;
        bios_call(0x16, &regs, &regs);

        if (is_zero_set(&regs))
            break;

        // Pop one keystroke
        regs.eax = 0;
        bios_call(0x16, &regs, &regs);
    }

    print_err("Loading aborted! Press any key to reboot...\n");

    regs.eax = 0;
    bios_call(0x16, &regs, &regs);

    bios_jmp_to_reset_vector();
}

void bios_entry(void)
{
    memzero(&section_bss_begin, &section_bss_end - &section_bss_begin);

    bios_video_services_init();
    BUG_ON(!a20_enabled);

    bios_memory_services_init();
    bios_disk_services_init();

    loader_entry();
}
