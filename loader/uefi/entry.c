#include "structures.h"

#include "common/types.h"
#include "services.h"

EFI_STATUS EFIAPI EfiMain (
    IN EFI_HANDLE ImageHandle,
    IN EFI_SYSTEM_TABLE *SystemTable
)
{
    (void) ImageHandle;
    (void) SystemTable;

    for (;;);
}
