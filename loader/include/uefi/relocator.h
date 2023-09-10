#pragma once

#include <common/types.h>

#include "structures.h"

typedef void (*relocated_cb_t)(void *user, EFI_PHYSICAL_ADDRESS addr);

void relocated_cb_write_u32(void *user, EFI_PHYSICAL_ADDRESS new_address);
void relocated_cb_write_u64(void *user, EFI_PHYSICAL_ADDRESS new_address);

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

    void *user;
    relocated_cb_t cb;
};

void relocate_entries(struct relocation_entry *entries);
