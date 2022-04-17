#pragma once

#include "common/types.h"

enum service_provider {
    SERVICE_PROVIDER_INVALID,
    SERVICE_PROVIDER_BIOS,
    SERVICE_PROVIDER_UEFI
};
enum service_provider services_get_provider(void);

/*
 * Attempts to retrieve the RSDP structure location.
 * Returns a 16-byte aligned address of the structure if successful, NULL otherwise.
 */
ptr_t services_find_rsdp(void);

/*
 * Disables all services and makes the caller the owner of all system resources.
 * map_key -> expected id of the current state of the memory map.
 * Returns true if key handover was successful and key matched the internal state,
 * otherwise key didn't match the internal state and memory map must be re-fetched.
 */
bool services_exit_all(size_t map_key);

/*
 * Aborts the loader execution in a platform-specific manner.
 * Must be used for unrecoverable errors.
 */
NORETURN void loader_abort(void);

/*
 * Platform-agnostic loader entrypoint.
 */
NORETURN void loader_entry(void);
