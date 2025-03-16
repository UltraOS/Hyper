#define MSG_FMT(msg) "UEFI-STUBS: " msg

#include "common/log.h"
#include "common/types.h"
#include "apm.h"

bool services_setup_apm(struct apm_info *out_info)
{
    UNUSED(out_info);

    print_warn("APM setup is unsupported!\n");
    return false;
}
