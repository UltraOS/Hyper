#define MSG_FMT(msg) "BIOS-PXE: " msg

#include "common/attributes.h"
#include "common/helpers.h"
#include "common/log.h"
#include "common/string.h"
#include "common/string_view.h"
#include "bios_call.h"
#include "ip.h"
#include "pxe_services.h"
#include "scratch_buffer.h"
#include "services_impl.h"

// INT 1Ah PXE installation check
#define PXE_INSTALL_CHECK    0x5650
#define PXE_INSTALL_CHECK_OK 0x564E

// PXE API opcodes (PXE spec 2.1)
#define PXENV_TFTP_OPEN       0x0020
#define PXENV_TFTP_CLOSE      0x0021
#define PXENV_TFTP_READ       0x0022
#define PXENV_TFTP_GET_FSIZE  0x0025
#define PXENV_GET_CACHED_INFO 0x0071

#define PXENV_EXIT_SUCCESS   0x0000
#define PXENV_STATUS_SUCCESS 0x00

#define PXENV_PACKET_TYPE_DHCP_ACK     2
#define PXENV_PACKET_TYPE_CACHED_REPLY 3

#define TFTP_PORT 69

/*
 * The largest TFTP packet we are willing to negotiate. Anything bigger doesn't
 * fit a single Ethernet frame, and the server may hand back a smaller size.
 */
#define TFTP_MAX_PACKET_SIZE 1456
BUILD_BUG_ON(TFTP_MAX_PACKET_SIZE < 512);
BUILD_BUG_ON(TFTP_MAX_PACKET_SIZE > SCRATCH_BUFFER_SIZE);

#define TFTP_FILENAME_SIZE 128

struct PACKED pxenv_plus {
    char signature[6];
    u16 version;
    u8 length;
    u8 checksum;
    u32 rm_entry;
    u32 pm_entry_offset;
    u16 pm_entry_segment;
    u16 stack_segment;
    u16 stack_size;
    u16 bc_code_segment;
    u16 bc_code_size;
    u16 bc_data_segment;
    u16 bc_data_size;
    u16 undi_data_segment;
    u16 undi_data_size;
    u16 undi_code_segment;
    u16 undi_code_size;
    u32 pxe_ptr;
};
BUILD_BUG_ON(sizeof(struct pxenv_plus) != 0x2C);

struct PACKED pxe_struct {
    char signature[4];
    u8 length;
    u8 checksum;
    u8 revision;
    u8 reserved;
    u32 undi_rom_id;
    u32 base_rom_id;
    u32 entry_point_sp;
    u32 entry_point_esp;
    u32 status_callout;
    u8 reserved2;
    u8 seg_desc_cnt;
    u16 first_selector;
};
BUILD_BUG_ON(sizeof(struct pxe_struct) != 0x20);

#define BOOTP_HEADER_SIZE 236
#define BOOTP_PACKET_SIZE 1472

// The cached BOOTP/DHCP reply, laid out exactly as on the wire
struct PACKED bootp_packet {
    u8 opcode;
    u8 hardware;
    u8 hardware_len;
    u8 gate_hops;
    u32 ident;
    u16 seconds;
    u16 flags;
    ipv4_addr client_ip;
    ipv4_addr your_ip;
    ipv4_addr server_ip;
    ipv4_addr gateway_ip;
    u8 client_hw_addr[16];
    u8 server_name[64];
    u8 boot_file[128];
    u8 vendor[BOOTP_PACKET_SIZE - BOOTP_HEADER_SIZE];
};
BUILD_BUG_ON(sizeof(struct bootp_packet) != BOOTP_PACKET_SIZE);

struct PACKED pxenv_get_cached_info {
    u16 status;
    u16 packet_type;
    u16 buffer_size;
    u16 buffer_offset;
    u16 buffer_segment;
    u16 buffer_limit;
};
BUILD_BUG_ON(sizeof(struct pxenv_get_cached_info) != 0x0C);

struct PACKED pxenv_tftp_open {
    u16 status;
    ipv4_addr server_ip;
    ipv4_addr gateway_ip;
    char filename[TFTP_FILENAME_SIZE];
    u16 tftp_port;
    u16 packet_size;
};
BUILD_BUG_ON(sizeof(struct pxenv_tftp_open) != 0x8E);

struct PACKED pxenv_tftp_close {
    u16 status;
};
BUILD_BUG_ON(sizeof(struct pxenv_tftp_close) != 0x02);

struct PACKED pxenv_tftp_read {
    u16 status;
    u16 packet_number;
    u16 buffer_size;
    u16 buffer_offset;
    u16 buffer_segment;
};
BUILD_BUG_ON(sizeof(struct pxenv_tftp_read) != 0x0A);

struct PACKED pxenv_tftp_get_fsize {
    u16 status;
    ipv4_addr server_ip;
    ipv4_addr gateway_ip;
    char filename[TFTP_FILENAME_SIZE];
    u32 file_size;
};
BUILD_BUG_ON(sizeof(struct pxenv_tftp_get_fsize) != 0x8E);

static ipv4_addr s_server_ip;
static ipv4_addr s_gateway_ip;

static struct bootp_packet cached_packet;

// The DHCP option area is preceded by a 4-byte magic cookie
#define DHCP_MAGIC_COOKIE_SIZE 4

static u16 pxe_call(u16 opcode, void *param)
{
    struct real_mode_addr addr;

    as_real_mode_addr((ptr_t)param, &addr);
    return bios_pxe_call(opcode, addr.segment, addr.offset);
}

/*
 * The DHCP option area lives in the BOOTP 'vendor' field, right after a 4-byte
 * magic cookie (0x63 0x82 0x53 0x63). A plain BOOTP reply has no cookie and
 * thus no options.
 */
static const u8 *dhcp_option_area(const struct bootp_packet *pkt,
                                  size_t *out_size)
{
    static const u8 cookie[DHCP_MAGIC_COOKIE_SIZE] = {
        0x63, 0x82, 0x53, 0x63
    };

    if (memcmp(pkt->vendor, cookie, sizeof(cookie)) != 0)
        return NULL;

    *out_size = sizeof(pkt->vendor) - DHCP_MAGIC_COOKIE_SIZE;
    return pkt->vendor + DHCP_MAGIC_COOKIE_SIZE;
}

static bool resolve_server_ip(
    const struct bootp_packet *pkt, ipv4_addr *out
)
{
    size_t opts_size = 0;
    const u8 *opts = dhcp_option_area(pkt, &opts_size);

    return dhcp_resolve_server_ip(opts, opts_size, &pkt->server_ip, out);
}

/*
 * Determine the gateway needed to reach 'server'. The client's network config
 * (subnet mask + router) comes from the address-assigning DHCP ack, which may
 * be a different packet than the one that named the TFTP server. A server on
 * our own subnet needs no gateway, leaving 'out' zeroed.
 */
static void resolve_gateway_ip(
    const struct bootp_packet *ack, const ipv4_addr *server,
    ipv4_addr *out
)
{
    const u8 *opts;
    ipv4_addr mask;
    size_t opts_size = 0;

    memzero(out, IPV4_ADDR_LEN);

    opts = dhcp_option_area(ack, &opts_size);
    if (!opts)
        return;

    if (!dhcp_find_ipv4_option(opts, opts_size, DHCP_OPT_SUBNET_MASK, &mask))
        return;

    if (ipv4_same_subnet(server, &ack->your_ip, &mask))
        return;

    // Off-subnet: route via the default router, or the relay agent as a backup
    if (dhcp_find_ipv4_option(opts, opts_size, DHCP_OPT_ROUTER, out))
        return;
    if (!ipv4_is_unset(&ack->gateway_ip))
        *out = ack->gateway_ip;
}

static bool get_cached_packet(u16 packet_type, struct bootp_packet *out)
{
    struct pxenv_get_cached_info info = { 0 };
    struct real_mode_addr buf_addr;
    u16 ret;

    memzero(out, sizeof(*out));
    as_real_mode_addr((ptr_t)out, &buf_addr);
    info.packet_type = packet_type;
    info.buffer_size = sizeof(*out);
    info.buffer_segment = buf_addr.segment;
    info.buffer_offset = buf_addr.offset;

    ret = pxe_call(PXENV_GET_CACHED_INFO, &info);

    /*
     * Failure isn't necessarily fatal here: a single-server setup caches no
     * boot reply, so the caller falls back to the next packet type.
     */
    if (ret != PXENV_EXIT_SUCCESS || info.status != PXENV_STATUS_SUCCESS)
        return false;

    /*
     * Some stacks ignore the supplied buffer and instead point Buffer at their
     * own cached copy; mirror whatever Buffer ends up referring to into 'out'.
     */
    if (info.buffer_segment || info.buffer_offset) {
        const void *src =
            from_real_mode_addr(info.buffer_segment, info.buffer_offset);
        size_t len = info.buffer_size;

        if (len > sizeof(*out))
            len = sizeof(*out);
        if (src != out)
            memcpy(out, src, len);
    }

    return true;
}

/*
 * On a PXE 2.1+ stack the full base-code API lives behind the !PXE structure's
 * EntryPointSP, which we prefer; the PXENV+ rm_entry may expose only a subset.
 */
static u32 pxe_pick_entry(const struct pxenv_plus *pxenv)
{
    const struct pxe_struct *px;

    if (pxenv->version >= 0x0201 && pxenv->pxe_ptr &&
        pxenv->pxe_ptr != 0xFFFFFFFF) {
        px = from_real_mode_addr(pxenv->pxe_ptr >> 16, pxenv->pxe_ptr & 0xFFFF);
        if (memcmp(px->signature, "!PXE", sizeof(px->signature)) == 0)
            return px->entry_point_sp;

        print_warn("invalid !PXE signature, falling back to rm_entry\n");
    }

    return pxenv->rm_entry;
}

bool pxe_services_setup(void)
{
    /*
     * Prefer the cached PXE boot reply: with proxyDHCP the regular DHCP ack
     * comes from the address server (and points option 54 / siaddr at it),
     * while the actual TFTP next-server lives in the separate boot reply. Fall
     * back to the DHCP ack for the common single-server case (e.g. QEMU), where
     * no boot reply is cached.
     */
    static const u16 packet_types[] = {
        PXENV_PACKET_TYPE_CACHED_REPLY,
        PXENV_PACKET_TYPE_DHCP_ACK,
    };
    struct real_mode_regs regs = { 0 };
    struct pxenv_plus *pxenv;
    size_t i;

    regs.eax = PXE_INSTALL_CHECK;
    bios_call(0x1A, &regs, &regs);

    if (is_carry_set(&regs) || (regs.eax & 0xFFFF) != PXE_INSTALL_CHECK_OK) {
        print_info("PXE is not available\n");
        return false;
    }

    pxenv = from_real_mode_addr(regs.es, regs.ebx & 0xFFFF);
    if (memcmp(pxenv->signature, "PXENV+", sizeof(pxenv->signature)) != 0) {
        print_warn("invalid PXENV+ signature\n");
        return false;
    }

    g_bios_pxe_entry = pxe_pick_entry(pxenv);
    for (i = 0; i < ARRAY_SIZE(packet_types); ++i) {
        if (!get_cached_packet(packet_types[i], &cached_packet))
            continue;

        if (!resolve_server_ip(&cached_packet, &s_server_ip))
            continue;

        /*
         * Work out the gateway (if any) from the DHCP ack's network config,
         * which carries the subnet mask and router that the boot reply may not.
         * Re-fetching clobbers cached_packet, but the server is already saved.
         */
        if (get_cached_packet(PXENV_PACKET_TYPE_DHCP_ACK, &cached_packet))
            resolve_gateway_ip(&cached_packet, &s_server_ip, &s_gateway_ip);

        print_info("detected PXE: server %pIP4\n", &s_server_ip);
        return true;
    }

    print_warn("could not determine TFTP server address\n");
    return false;
}

static bool copy_filename(char *dst, struct string_view path)
{
    if (path.size >= TFTP_FILENAME_SIZE) {
        print_warn("path '%pSV' is too long\n", &path);
        return false;
    }

    sv_terminated_copy(dst, path);
    return true;
}

bool pxe_get_file_size(struct string_view path, u64 *out_size)
{
    SERVICE_FUNCTION();

    struct pxenv_tftp_get_fsize fsize = {
        .server_ip = s_server_ip,
        .gateway_ip = s_gateway_ip,
    };
    u16 ret;

    *out_size = 0;

    if (!copy_filename(fsize.filename, path))
        return false;

    ret = pxe_call(PXENV_TFTP_GET_FSIZE, &fsize);
    if (ret != PXENV_EXIT_SUCCESS || fsize.status != PXENV_STATUS_SUCCESS) {
        print_warn(
            "TFTP_GET_FSIZE('%pSV') failed (ret=%u, status=0x%04X)\n",
            &path, ret, fsize.status
        );
        return false;
    }

    *out_size = fsize.file_size;
    return true;
}

static void tftp_close(void)
{
    struct pxenv_tftp_close close = { 0 };
    pxe_call(PXENV_TFTP_CLOSE, &close);
}

bool pxe_read_file(struct string_view path, void *out_buf, u64 expected_size)
{
    SERVICE_FUNCTION();

    struct pxenv_tftp_open open = {
        .gateway_ip = s_gateway_ip,
        .server_ip = s_server_ip,
    };
    struct real_mode_addr buf_addr;
    u8 *dst = out_buf;
    u8 *tftp_buffer;
    u64 total = 0;
    u16 packet_size, ret;

    if (!copy_filename(open.filename, path))
        return false;

    open.tftp_port = __builtin_bswap16(TFTP_PORT);
    open.packet_size = TFTP_MAX_PACKET_SIZE;

    ret = pxe_call(PXENV_TFTP_OPEN, &open);
    if (ret != PXENV_EXIT_SUCCESS || open.status != PXENV_STATUS_SUCCESS) {
        print_warn(
            "TFTP_OPEN('%pSV') failed (ret=%u, status=0x%04X)\n",
            &path, ret, open.status
        );
        return false;
    }

    packet_size = open.packet_size;
    if (packet_size == 0 || packet_size > TFTP_MAX_PACKET_SIZE) {
        print_warn("server negotiated a bogus packet size (%u)\n", packet_size);
        tftp_close();
        return false;
    }

    tftp_buffer = scratch_buffer_borrow(TFTP_MAX_PACKET_SIZE, NULL, NULL);
    as_real_mode_addr((ptr_t)tftp_buffer, &buf_addr);

    for (;;) {
        struct pxenv_tftp_read read = { 0 };

        read.buffer_segment = buf_addr.segment;
        read.buffer_offset = buf_addr.offset;

        ret = pxe_call(PXENV_TFTP_READ, &read);
        if (ret != PXENV_EXIT_SUCCESS || read.status != PXENV_STATUS_SUCCESS) {
            print_warn(
                "TFTP_READ('%pSV') failed (ret=%u, status=0x%04X)\n",
                &path, ret, read.status
            );
            tftp_close();
            return false;
        }

        if (total + read.buffer_size > expected_size) {
            print_warn(
                "'%pSV' is larger than the expected %llu bytes\n",
                &path, expected_size
            );
            tftp_close();
            return false;
        }

        memcpy(dst + total, tftp_buffer, read.buffer_size);
        total += read.buffer_size;

        if (read.buffer_size < packet_size)
            break;
    }

    tftp_close();

    if (unlikely(total != expected_size)) {
        print_warn(
            "'%pSV' size mismatch: expected %llu, got %llu\n",
            &path, expected_size, total
        );
        return false;
    }

    return true;
}

void pxe_server_ip_address(struct ip_addr *out)
{
    SERVICE_FUNCTION();

    out->type = IP_TYPE_V4;
    out->v4 = s_server_ip;
}
