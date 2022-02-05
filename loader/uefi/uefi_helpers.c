#include "uefi_helpers.h"
#include "uefi_globals.h"

#include "common/log.h"

#undef MSG_FMT
#define MSG_FMT(msg) "UEFI: " msg

bool uefi_pool_alloc(EFI_MEMORY_TYPE type, size_t elem_size, size_t count, VOID **out)
{
    EFI_STATUS ret;
    UINTN bytes_total = elem_size * count;
    BUG_ON(bytes_total == 0);

    ret = g_st->BootServices->AllocatePool(type, bytes_total, out);
    if (unlikely(EFI_ERROR(ret))) {
        struct string_view err_msg = uefi_status_to_string(ret);
        print_warn("AllocatePool(type=%u, bytes=%zu) failed: %pSV\n", type, bytes_total, &err_msg);
        return false;
    }

    return true;
}

struct string_view uefi_status_to_string(EFI_STATUS sts)
{
    switch (sts) {
    case EFI_SUCCESS:
        return SV("success");
    case EFI_WARN_UNKNOWN_GLYPH:
        return SV("unknown glyph");
    case EFI_WARN_DELETE_FAILURE:
        return SV("delete failure");
    case EFI_WARN_WRITE_FAILURE:
        return SV("write failure");
    case EFI_WARN_BUFFER_TOO_SMALL:
        return SV("buffer too small");
    case EFI_WARN_STALE_DATA:
        return SV("stale data");
    case EFI_WARN_FILE_SYSTEM:
        return SV("file system");
    case EFI_WARN_RESET_REQUIRED:
        return SV("reset required");
    case EFI_LOAD_ERROR:
        return SV("load error");
    case EFI_INVALID_PARAMETER:
        return SV("invalid parameter");
    case EFI_UNSUPPORTED:
        return SV("unsupported");
    case EFI_BAD_BUFFER_SIZE:
        return SV("bad buffer size");
    case EFI_BUFFER_TOO_SMALL:
        return SV("buffer too small");
    case EFI_NOT_READY:
        return SV("not ready");
    case EFI_DEVICE_ERROR:
        return SV("device error");
    case EFI_WRITE_PROTECTED:
        return SV("write protected");
    case EFI_OUT_OF_RESOURCES:
        return SV("out of resources");
    case EFI_VOLUME_CORRUPTED:
        return SV("volume corrupted");
    case EFI_VOLUME_FULL:
        return SV("volume full");
    case EFI_NO_MEDIA:
        return SV("no media");
    case EFI_MEDIA_CHANGED:
        return SV("media changed");
    case EFI_NOT_FOUND:
        return SV("not found");
    case EFI_ACCESS_DENIED:
        return SV("access denied");
    case EFI_NO_RESPONSE:
        return SV("no response");
    case EFI_NO_MAPPING:
        return SV("no mapping");
    case EFI_TIMEOUT:
        return SV("timeout");
    case EFI_NOT_STARTED:
        return SV("not started");
    case EFI_ALREADY_STARTED:
        return SV("already started");
    case EFI_ABORTED:
        return SV("aborted");
    case EFI_ICMP_ERROR:
        return SV("icmp error");
    case EFI_TFTP_ERROR:
        return SV("tftp error");
    case EFI_PROTOCOL_ERROR:
        return SV("protocol error");
    case EFI_INCOMPATIBLE_VERSION:
        return SV("incompatible version");
    case EFI_SECURITY_VIOLATION:
        return SV("security violation");
    case EFI_CRC_ERROR:
        return SV("crc error");
    case EFI_END_OF_MEDIA:
        return SV("end of media");
    case EFI_END_OF_FILE:
        return SV("end of file");
    case EFI_INVALID_LANGUAGE:
        return SV("invalid language");
    case EFI_COMPROMISED_DATA:
        return SV("compromised data");
    case EFI_IP_ADDRESS_CONFLICT:
        return SV("address conflict");
    case EFI_HTTP_ERROR:
        return SV("http error");
    default:
        return SV("<invalid status>");
    }
}

bool uefi_get_protocol_handles(EFI_GUID *guid, EFI_HANDLE **array, UINTN *count)
{
    EFI_BOOT_SERVICES *bs = g_st->BootServices;
    UINTN bytes_needed = 0;
    EFI_STATUS ret;
    struct string_view err_msg;
    *array = NULL;

    ret = bs->LocateHandle(ByProtocol, guid, NULL, &bytes_needed, NULL);
    if (EFI_ERROR(ret) && ret != EFI_BUFFER_TOO_SMALL)
        goto efi_error;
    if (unlikely(bytes_needed < sizeof(EFI_HANDLE)))
        return false;

    ret = bs->AllocatePool(EfiLoaderData, bytes_needed, (void**)array);
    if (unlikely(EFI_ERROR(ret)))
        goto efi_error;

    ret = bs->LocateHandle(ByProtocol, guid, NULL, &bytes_needed, *array);
    if (unlikely(EFI_ERROR(ret)))
        goto efi_error;

    *count = bytes_needed / sizeof(EFI_HANDLE);
    return true;

efi_error:
    if (*array)
        bs->FreePool(*array);

    err_msg = uefi_status_to_string(ret);
    print_warn("get_protocol_handles() error: %pSV\n", &err_msg);
    return false;
}
