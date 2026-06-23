#pragma once

#include "common/types.h"
#include "common/string_view.h"
#include "ip.h"

/*
 * Sets up PXE services on the current platform. A return value of 'true'
 * indicates a valid PXE server was found and is usable.
 */
bool pxe_services_setup(void);

/*
 * Retrieves the size of a file pointed to by 'path'.
 * Returns 'true' if the result was retrieved successfully, 'false' otherwise.
 */
bool pxe_get_file_size(struct string_view path, u64 *out_size);

/*
 * Reads a file pointed to by 'path' into 'out_buf'. 'expected_size' specifies
 * the size of 'out_buf', as well as the expected amount of bytes the TFTP
 * server is supposed to transfer. Returns 'true' iff the transfer was
 * successful and the number of bytes transferred is exactly 'expected_size'.
 */
bool pxe_read_file(struct string_view path, void *out_buf, u64 expected_size);

/*
 * Retrieves the address of the PXE server that is currently detected.
 * This is expected to be usable and always successful after a (successful)
 * call to pxe_services_setup().
 */
void pxe_server_ip_address(struct ip_addr *out);
