#pragma once

#include "common/string_view.h"
#include "common/attributes.h"
#include "common/helpers.h"

#include "config.h"

typedef struct boot_protocol *boot_protocol_entry;

#define DECLARE_BOOT_PROTOCOL(type) \
    static boot_protocol_entry CONCAT(type, hook) \
           SECTION(.boot_protos) USED = &type

extern boot_protocol_entry boot_protocols_begin[];
extern boot_protocol_entry boot_protocols_end[];

struct boot_protocol {
    struct string_view name;
    u64 *known_mm_types;
    NORETURN void (*boot) (struct config*, struct loadable_entry*);
};

NORETURN
void boot(struct config*, struct loadable_entry*);
