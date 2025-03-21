#pragma once

#include "common/types.h"
#include "common/attributes.h"
#include "apm.h"

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
 * Attempts to retrieve the DTB structure location.
 * Returns an 8-byte aligned address of the structure if successful, NULL otherwise.
 */
ptr_t services_find_dtb(void);

/*
 * Attempts to retrieve the SMBIOS entry point structure location.
 * Returns a 16-byte aligned address of the structure if successful, NULL otherwise.
 */
ptr_t services_find_smbios(void);

/*
 * Attempts to setup the 32-bit protected-mode interface for APM if it exists.
 * Returns true if the interface was successfully installed, false otherwise.
 */
bool services_setup_apm(struct apm_info *out_info);

/*
 * Aborts the loader execution in a platform-specific manner.
 * Must be used for unrecoverable errors.
 */
NORETURN void loader_abort(void);

/*
 * Platform-agnostic loader entrypoint.
 */
NORETURN void loader_entry(void);

/*
 * Runs all registered cleanup handlers.
 * All services aside from memory management & handover are assumed to be
 * unusable after this function returns.
 */
void services_cleanup(void);
