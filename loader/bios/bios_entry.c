#include "common/log.h"
#include "common/panic.h"
#include "common/string.h"
#include "services.h"

#include "bios_memory_services.h"
#include "bios_video_services.h"
#include "bios_disk_services.h"
#include "bios_find_rsdp.h"
#include "bios_call.h"

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

void loader_abort()
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

    for (;;);
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
