#pragma once

#include "common/helpers.h"
#include "common/types.h"

#define IPV4_ADDR_LEN 4
#define IPV6_ADDR_LEN 16

typedef struct ipv4_addr {
    uint8_t addr[IPV4_ADDR_LEN];
} ipv4_addr;

typedef struct ipv6_addr {
    uint8_t addr[IPV6_ADDR_LEN];
} ipv6_addr;

typedef struct ip_addr {
    union {
        ipv4_addr v4;
        ipv6_addr v6;
    };

#define IP_TYPE_INVALID 0
#define IP_TYPE_V4 1
#define IP_TYPE_V6 2
    uint8_t type;
} ip_addr;

// DHCP option codes (RFC 2132)
#define DHCP_OPT_PAD         0
#define DHCP_OPT_SUBNET_MASK 1
#define DHCP_OPT_ROUTER      3
#define DHCP_OPT_SERVER_ID   54
#define DHCP_OPT_TFTP_SERVER 66
#define DHCP_OPT_END         255

bool ipv4_is_unset(const ipv4_addr *addr);

bool ipv4_same_subnet(
    const ipv4_addr *a, const ipv4_addr *b, const ipv4_addr *mask
);

// Parse "a.b.c.d" into 'out'. Returns false unless it's a complete dotted quad.
bool ipv4_parse(const u8 *str, size_t len, ipv4_addr *out);

/*
 * Walk a DHCP option blob (TLV) looking for 'tag'. Returns a pointer to its
 * value and stores its length in 'out_len', or NULL if the tag is absent.
 */
const u8 *dhcp_find_option(const u8 *opts, size_t size, u8 tag, u8 *out_len);

/*
 * Find a DHCP option which is supposed to contain an IPv4 address (in binary
 * form). Returns 'true' if parsed an IPv4 address successfully, 'false'
 * otherwise. Stores the result in 'out'.
 */
bool dhcp_find_ipv4_option(const u8 *opts, size_t size, u8 tag, ipv4_addr *out);

/*
 * Resolve the boot/TFTP server from a reply's DHCP option area and BOOTP
 * siaddr: option 66 -> siaddr -> option 54. 'opts' may be NULL when the reply
 * carries no options (a plain BOOTP reply).
 */
bool dhcp_resolve_server_ip(
    const u8 *opts, size_t opts_size, const ipv4_addr *siaddr,
    ipv4_addr *out
);
