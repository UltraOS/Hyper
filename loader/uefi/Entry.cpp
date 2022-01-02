#define EFIAPI
#define EFI_STATUS
#define IN
#define EFI_HANDLE void*
#define EFI_SYSTEM_TABLE void

extern "C" EFI_STATUS EFIAPI EfiMain (
    IN EFI_HANDLE ImageHandle,
    IN EFI_SYSTEM_TABLE* SystemTable
)
{
    (void) ImageHandle;
    (void) SystemTable;

    for (;;);
}