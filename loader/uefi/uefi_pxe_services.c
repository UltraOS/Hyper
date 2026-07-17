#define MSG_FMT(msg) "UEFI-PXE: " msg

#include "common/helpers.h"
#include "common/log.h"
#include "common/string.h"
#include "common/string_view.h"
#include "filesystem/path.h"
#include "ip.h"
#include "uefi/globals.h"
#include "uefi/helpers.h"
#include "pxe_services.h"
#include "services_impl.h"
#include "uefi/structures.h"

static EFI_PXE_BASE_CODE_PROTOCOL *s_pxe;
static EFI_IP_ADDRESS s_server_ip;

/*
 * Locate the variable-length DHCP option area within a packet. It begins right
 * after the fixed BOOTP header and 4-byte magic cookie and may run all the way
 * to the end of the packet, well past the DhcpOptions[] stub in the struct.
 */
static const u8 *dhcp_options(EFI_PXE_BASE_CODE_PACKET *pkt, UINTN *out_size)
{
    UINTN offset = offsetof(EFI_PXE_BASE_CODE_DHCPV4_PACKET, DhcpOptions);

    *out_size = sizeof(pkt->Raw) - offset;
    return pkt->Raw + offset;
}

static bool resolve_server_ip(
    EFI_PXE_BASE_CODE_PACKET *pkt, EFI_IPv4_ADDRESS *out
)
{
    UINTN opts_size;
    const u8 *opts = dhcp_options(pkt, &opts_size);

    return dhcp_resolve_server_ip(
        opts, opts_size, (const ipv4_addr*)pkt->Dhcpv4.BootpSiAddr,
        (ipv4_addr*)out
    );
}

bool pxe_services_setup(void)
{
    EFI_GUID pxe_guid = EFI_PXE_BASE_CODE_PROTOCOL_GUID;
    EFI_PXE_BASE_CODE_MODE *mode;
    EFI_PXE_BASE_CODE_PACKET *pkt;
    EFI_HANDLE *handles;
    UINTN handle_count, i;

    if (!uefi_get_protocol_handles_nowarn(&pxe_guid, &handles,
                                          &handle_count))
	    return false;

    for (i = 0; i < handle_count; ++i) {
        EFI_PXE_BASE_CODE_PROTOCOL *pxe = NULL;
        EFI_STATUS ret;

        ret = g_st->BootServices->HandleProtocol(
            handles[i], &pxe_guid, (void**)&pxe
        );
        if (unlikely_efi_error(ret)) {
            struct string_view err_msg = uefi_status_to_string(ret);
            print_warn(
                "pxe[%zu] HandleProtocol() error: %pSV\n", i, &err_msg
            );
            continue;
        }

        mode = pxe->Mode;
        if (unlikely(!mode))
            continue;

        // No completed DHCP exchange means there's no server to talk to
        if (!mode->Started || !mode->DhcpAckReceived)
            continue;

        // IPv6 uses a different (non-BOOTP) layout we don't parse
        if (mode->UsingIpv6)
            continue;

        s_pxe = pxe;
        break;
    }

    g_st->BootServices->FreePool(handles);

    if (!s_pxe) {
        print_info("no usable PXE handle found\n");
        return false;
    }

    mode = s_pxe->Mode;

    if (mode->PxeReplyReceived)
        pkt = &mode->PxeReply;
    else if (mode->ProxyOfferReceived)
        pkt = &mode->ProxyOffer;
    else
        pkt = &mode->DhcpAck;

    if (!resolve_server_ip(pkt, &s_server_ip.v4)) {
        print_warn("could not determine TFTP server address\n");
        return false;
    }

    print_info("detected PXE: server %pIP4\n", &s_server_ip.v4);

    return true;
}

static bool pxe_do_tftp(EFI_PXE_BASE_CODE_TFTP_OPCODE op,
                        struct string_view path, void *buffer,
                        u64 *buffer_size, bool dont_use_buffer)
{
    EFI_STATUS ret;
    char terminated_buf[MAX_PATH_SIZE + 1];

    BUG_ON(!s_pxe);

    if (path.size > MAX_PATH_SIZE)
        return false;
    sv_terminated_copy(terminated_buf, path);

    ret = s_pxe->Mtftp(
        s_pxe, op, buffer, FALSE, buffer_size, NULL, &s_server_ip,
        (UINT8*)terminated_buf, NULL, dont_use_buffer
    );
    if (unlikely_efi_error(ret)) {
        struct string_view err_msg = uefi_status_to_string(ret);
        print_warn(
            "Mtftp(op=%d, '%pSV') failed: %pSV\n", op, &path, &err_msg
        );
        return false;
    }

    return true;
}

bool pxe_get_file_size(struct string_view path, u64 *out_size)
{
    SERVICE_FUNCTION();

    /*
     * GET_FILE_SIZE stores no file data, but EDK2 before cf9ff46b wrongly
     * rejects the call when BufferPtr is NULL, so hand it a throwaway buffer.
     */
    u8 dummy[1];

    *out_size = 0;
    return pxe_do_tftp(
        EFI_PXE_BASE_CODE_TFTP_GET_FILE_SIZE, path, dummy, out_size, TRUE
    );
}

bool pxe_read_file(struct string_view path, void *out_buf, u64 expected_size)
{
    SERVICE_FUNCTION();

    u64 buffer_size = expected_size;

    if (!pxe_do_tftp(EFI_PXE_BASE_CODE_TFTP_READ_FILE, path, out_buf,
                     &buffer_size, FALSE))
        return false;

    if (unlikely(buffer_size != expected_size)) {
        print_warn(
            "'%pSV' size mismatch: expected %llu, got %llu\n",
            &path, expected_size, buffer_size
        );
        return false;
    }

    return true;
}

void pxe_server_ip_address(struct ip_addr *out)
{
    SERVICE_FUNCTION();

    out->type = IP_TYPE_V4;
    memcpy(&out->v4, &s_server_ip.v4, IPV4_ADDR_LEN);
}
