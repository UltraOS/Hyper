#define MSG_FMT(msg) "BIOS-APM: " msg

#include "common/format.h"
#include "common/log.h"
#include "bios_call.h"
#include "services_impl.h"
#include "apm.h"

#define APM_SIGNATURE 0x504D
#define APM_POWER_DEVICE_ID_APM_BIOS 0x0000

#define APM_FLAG_32BIT_INTERFACE_SUPPORTED (1 << 1)

#define APM_INT 0x15
#define APM_CMD 0x53
#define MAKE_APM_CMD(cmd) ((APM_CMD << 8) | (cmd))

#define APM_INSTALLATION_CHECK MAKE_APM_CMD(0x00)
#define APM_PM32_INTERFACE_CONNECT MAKE_APM_CMD(0x03)
#define APM_INTERFACE_DISCONNECT MAKE_APM_CMD(0x04)

static bool check_apm_call(
    const struct real_mode_regs *in_regs,
    const struct real_mode_regs *out_regs
)
{
    if (is_carry_set(out_regs)) {
        print_warn(
            "APM call 0x%04X failed: %d\n",
            in_regs->eax, (out_regs->eax & 0xFFFF) >> 8
        );
        return false;
    }

    if (in_regs->eax == APM_INSTALLATION_CHECK) {
        u16 signature = out_regs->ebx & 0xFFFF;

        if (unlikely(signature != APM_SIGNATURE)) {
            print_warn("bad APM signature 0x%04X\n", signature);
            return false;
        }
    }

    return true;
}

bool services_setup_apm(struct apm_info *out_info)
{
    struct real_mode_regs out_regs, in_regs = { 0 };

    // All queries will be for the APM BIOS "device"
    in_regs.ebx = APM_POWER_DEVICE_ID_APM_BIOS;

    // 1. Check if APM exists at all
    in_regs.eax = APM_INSTALLATION_CHECK;
    bios_call(APM_INT, &in_regs, &out_regs);
    if (!check_apm_call(&in_regs, &out_regs))
        return false;

    if (!(out_regs.ecx & APM_FLAG_32BIT_INTERFACE_SUPPORTED)) {
        print_warn("APM doesn't support 32-bit interface\n");
        return false;
    }

    // 2. Disconnect if anything was connected previously, ignore return value
    in_regs.eax = APM_INTERFACE_DISCONNECT;
    bios_call(APM_INT, &in_regs, &out_regs);

    // 3. Connect the 32-bit interface
    in_regs.eax = APM_PM32_INTERFACE_CONNECT;
    bios_call(APM_INT, &in_regs, &out_regs);
    if (!check_apm_call(&in_regs, &out_regs))
        return false;

    print_info("32-bit PM interface connected\n");
    out_info->pm_code_segment = out_regs.eax & 0xFFFF;
    out_info->pm_code_segment_length = out_regs.esi & 0xFFFF;
    out_info->pm_offset = out_regs.ebx;

    out_info->rm_code_segment = out_regs.ecx & 0xFFFF;
    out_info->rm_code_segment_length = out_regs.esi >> 16;

    out_info->data_segment = out_regs.edx & 0xFFFF;
    out_info->data_segment_length = out_regs.edi & 0xFFFF;

    // 4. Recheck flags, as they might change after 32-bit interface install
    in_regs.eax = APM_INSTALLATION_CHECK;
    bios_call(APM_INT, &in_regs, &out_regs);
    if (unlikely(!check_apm_call(&in_regs, &out_regs))) {
        in_regs.eax = APM_INTERFACE_DISCONNECT;
        bios_call(APM_INT, &in_regs, &out_regs);
        return false;
    }

    out_info->version = out_regs.eax & 0xFFFF;
    out_info->flags = out_regs.ecx & 0xFFFF;
    return true;
}
