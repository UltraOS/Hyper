#pragma once

#include "structures.h"
#include "common/string_view.h"

#define unlikely_efi_error(ret) unlikely(EFI_ERROR(ret))

bool uefi_pool_alloc(EFI_MEMORY_TYPE type, size_t elem_size, size_t count, VOID **out);

/*
 * The caller is responsible for freeing array with FreePool()
 * Count is guaranteed to be >0 if this returns true
 * No memory is allocated if this returns false
 */
bool uefi_get_protocol_handles(EFI_GUID *guid, EFI_HANDLE **array, UINTN *count);

struct string_view uefi_status_to_string(EFI_STATUS sts);

void *uefi_find_configuration(EFI_GUID *guid);
