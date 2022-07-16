#include "boot_protocol.h"

#define PROTOCOL_KEY SV("protocol")

void boot(struct config *cfg, struct loadable_entry *le)
{
    struct string_view protocol_name;
    struct boot_protocol *proto = NULL;
    boot_protocol_entry *entry;

    CFG_MANDATORY_GET(string, cfg, le, PROTOCOL_KEY, &protocol_name);

    for (entry = boot_protocols_begin; entry < boot_protocols_end; ++entry) {
        if (!sv_equals_caseless((*entry)->name, protocol_name))
            continue;

        proto = *entry;
        break;
    }

    if (!proto)
        oops("unsupported boot protocol: %pSV\n", &protocol_name);

    if (proto->known_mm_types)
        mm_declare_known_mm_types(proto->known_mm_types);

    proto->boot(cfg, le);
}
