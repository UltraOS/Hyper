#include "ip.h"
#include "common/conversions.h"
#include "common/string.h"
#include "common/string_view.h"

// An option is a tag byte and a length byte, then that many value bytes
#define DHCP_OPT_HEADER_SIZE 2

bool ipv4_is_unset(const ipv4_addr *ip)
{
    size_t i;

    for (i = 0; i < IPV4_ADDR_LEN; ++i) {
        if (ip->addr[i])
            return false;
    }

    return true;
}

bool ipv4_same_subnet(
    const ipv4_addr *a, const ipv4_addr *b, const ipv4_addr *mask
)
{
    size_t i;

    for (i = 0; i < IPV4_ADDR_LEN; ++i) {
        if ((a->addr[i] & mask->addr[i]) != (b->addr[i] & mask->addr[i]))
            return false;
    }

    return true;
}

bool ipv4_parse(const u8 *str, size_t len, ipv4_addr *out)
{
    struct string_view tail = { (const char*)str, len };
    size_t i;

    for (i = 0; i < IPV4_ADDR_LEN; ++i) {
        struct string_view octet = tail;
        ssize_t dot = sv_find(tail, SV("."), 0);

        if (dot < 0) {
            sv_clear(&tail);
        } else {
            octet.size = dot;
            sv_offset_by(&tail, dot + 1);
        }

        if (!str_to_u8_with_base(octet, &out->addr[i], 10))
            return false;
    }

    // Reject anything trailing the fourth octet, e.g. "1.2.3.4.5"
    return sv_empty(tail);
}

const u8 *dhcp_find_option(const u8 *opts, size_t size, u8 tag, u8 *out_len)
{
    size_t i = 0;

    if (opts == NULL)
        return NULL;

    while (i < size) {
        u8 type = opts[i];
        u8 len;

        if (type == DHCP_OPT_END)
            break;
        if (type == DHCP_OPT_PAD) {
            i += 1;
            continue;
        }

        if (i + DHCP_OPT_HEADER_SIZE > size)
            break;

        len = opts[i + 1];
        if (i + DHCP_OPT_HEADER_SIZE + len > size)
            break;

        if (type == tag) {
            *out_len = len;
            return &opts[i + DHCP_OPT_HEADER_SIZE];
        }

        i += DHCP_OPT_HEADER_SIZE + len;
    }

    return NULL;
}

bool dhcp_find_ipv4_option(const u8 *opts, size_t size, u8 tag, ipv4_addr *out)
{
    u8 out_len;
    const u8 *data;

    data = dhcp_find_option(opts, size, tag, &out_len);
    if (data == NULL || out_len < IPV4_ADDR_LEN)
        return false;

    memcpy(out, data, IPV4_ADDR_LEN);
    return true;
}

bool dhcp_resolve_server_ip(
    const u8 *opts, size_t opts_size, const ipv4_addr *siaddr,
    ipv4_addr *out
)
{
    const u8 *val;
    u8 len;

    // Option 66 (TFTP server name) wins, when it's a dotted-quad literal
    val = dhcp_find_option(opts, opts_size, DHCP_OPT_TFTP_SERVER, &len);
    if (val && ipv4_parse(val, len, out))
        return true;

    // Otherwise the BOOTP next-server (siaddr), if set
    if (!ipv4_is_unset(siaddr)) {
        *out = *siaddr;
        return true;
    }

    // Last resort: option 54 (DHCP server identifier)
    if (dhcp_find_ipv4_option(opts, opts_size, DHCP_OPT_SERVER_ID, out))
        return true;

    return false;
}
