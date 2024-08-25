#pragma once

#include <common/types.h>

#include "structures.h"

typedef void (*relocated_cb_t)(void *user, u64 addr);

struct relocation_entry {
    // The size field is used if 'end' is set to NULL
    union {
      struct {
          void *begin;
          void *end;
      };

      u64 size;
    };

    EFI_PHYSICAL_ADDRESS max_address;
    EFI_MEMORY_TYPE memory_type;

    void *user;
    relocated_cb_t cb;
};

void relocate_entries(struct relocation_entry *entries);
