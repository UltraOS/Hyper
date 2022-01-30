#pragma once

#include "common/types.h"

#define IN
#define OUT
#define OPTIONAL
#define CONST const
#define EFIAPI

#define TRUE  1
#define FALSE 0
typedef u8 BOOLEAN;

typedef ssize_t INTN;
typedef size_t  UINTN;

typedef int8_t   INT8;
typedef uint8_t  UINT8;
typedef int16_t  INT16;
typedef uint16_t UINT16;
typedef int32_t  INT32;
typedef uint32_t UINT32;
typedef int64_t  INT64;
typedef uint64_t UINT64;

typedef char     CHAR8;
typedef u16      CHAR16;

typedef void VOID;

typedef UINTN  EFI_STATUS;
typedef VOID  *EFI_HANDLE;
typedef VOID  *EFI_EVENT;
typedef UINT64 EFI_LBA;
typedef UINTN  EFI_TPL;

typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8  Data4[8];
} EFI_GUID;

#define EFI_SYSTEM_TABLE_SIGNATURE 0x5453595320494249
#define EFI_2_90_SYSTEM_TABLE_REVISION ((2 << 16) | 90)
#define EFI_2_80_SYSTEM_TABLE_REVISION ((2 << 16) | 80)
#define EFI_2_70_SYSTEM_TABLE_REVISION ((2 << 16) | 70)
#define EFI_2_60_SYSTEM_TABLE_REVISION ((2 << 16) | 60)
#define EFI_2_50_SYSTEM_TABLE_REVISION ((2 << 16) | 50)
#define EFI_2_40_SYSTEM_TABLE_REVISION ((2 << 16) | 40)
#define EFI_2_31_SYSTEM_TABLE_REVISION ((2 << 16) | 31)
#define EFI_2_30_SYSTEM_TABLE_REVISION ((2 << 16) | 30)
#define EFI_2_20_SYSTEM_TABLE_REVISION ((2 << 16) | 20)
#define EFI_2_10_SYSTEM_TABLE_REVISION ((2 << 16) | 10)
#define EFI_2_00_SYSTEM_TABLE_REVISION ((2 << 16) | 00)
#define EFI_1_10_SYSTEM_TABLE_REVISION ((1 << 16) | 10)
#define EFI_1_02_SYSTEM_TABLE_REVISION ((1 << 16) | 02)

typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

typedef struct {
    UINT16 ScanCode;
    CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

typedef struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

// TODO: implement? if this becomes useful at some point
typedef struct _EFI_RUNTIME_SERVICES EFI_RUNTIME_SERVICES;

typedef
EFI_STATUS
(EFIAPI *EFI_INPUT_RESET) (
    IN EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This,
    IN BOOLEAN ExtendedVerification
);

typedef
EFI_STATUS
(EFIAPI *EFI_INPUT_READ_KEY) (
    IN EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This,
    OUT EFI_INPUT_KEY *Key
);

typedef struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    EFI_INPUT_RESET Reset;
    EFI_INPUT_READ_KEY ReadKeyStroke;
    EFI_EVENT WaitForKey;
} EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *EFI_TEXT_RESET) (
    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN BOOLEAN ExtendedVerification
);

typedef
EFI_STATUS
(EFIAPI *EFI_TEXT_STRING) (
    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN CHAR16 *String
);

typedef
EFI_STATUS
(EFIAPI *EFI_TEXT_TEST_STRING) (
    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN CHAR16 *String
);

typedef
EFI_STATUS
(EFIAPI *EFI_TEXT_QUERY_MODE) (
    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN UINTN ModeNumber,
    OUT UINTN *Columns,
    OUT UINTN *Rows
);

typedef
EFI_STATUS
(EFIAPI *EFI_TEXT_SET_MODE) (
    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN UINTN ModeNumber
);

#define EFI_BLACK        0x00
#define EFI_BLUE         0x01
#define EFI_GREEN        0x02
#define EFI_CYAN         0x03
#define EFI_RED          0x04
#define EFI_MAGENTA      0x05
#define EFI_BROWN        0x06
#define EFI_LIGHTGRAY    0x07
#define EFI_BRIGHT       0x08
#define EFI_DARKGRAY    (EFI_BLACK | EFI_BRIGHT)
#define EFI_LIGHTBLUE    0x09
#define EFI_LIGHTGREEN   0x0A
#define EFI_LIGHTCYAN    0x0B
#define EFI_LIGHTRED     0x0C
#define EFI_LIGHTMAGENTA 0x0D
#define EFI_YELLOW       0x0E
#define EFI_WHITE        0x0F

#define EFI_BACKGROUND_BLACK     0x00
#define EFI_BACKGROUND_BLUE      0x10
#define EFI_BACKGROUND_GREEN     0x20
#define EFI_BACKGROUND_CYAN      0x30
#define EFI_BACKGROUND_RED       0x40
#define EFI_BACKGROUND_MAGENTA   0x50
#define EFI_BACKGROUND_BROWN     0x60
#define EFI_BACKGROUND_LIGHTGRAY 0x70

//
// Macro to accept color values in their raw form to create
// a value that represents both a foreground and background
// color in a single byte.
// For Foreground, and EFI_* value is valid from EFI_BLACK(0x00)
// to EFI_WHITE (0x0F).
// For Background, only EFI_BLACK, EFI_BLUE, EFI_GREEN,
// EFI_CYAN, EFI_RED, EFI_MAGENTA, EFI_BROWN, and EFI_LIGHTGRAY
// are acceptable.
//
// Do not use EFI_BACKGROUND_xxx values with this macro.
#define EFI_TEXT_ATTR(Foreground, Background) ((Foreground) | ((Background) << 4))

typedef
EFI_STATUS
(EFIAPI *EFI_TEXT_SET_ATTRIBUTE) (
    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN UINTN Attribute
);

typedef
EFI_STATUS
(EFIAPI *EFI_TEXT_CLEAR_SCREEN) (
    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This
);

typedef
EFI_STATUS
(EFIAPI *EFI_TEXT_SET_CURSOR_POSITION) (
    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN UINTN Column,
    IN UINTN Row
);

typedef
EFI_STATUS
(EFIAPI *EFI_TEXT_ENABLE_CURSOR) (
    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN BOOLEAN Visible
);

typedef struct {
    INT32 MaxMode;
    // current settings
    INT32 Mode;
    INT32 Attribute;
    INT32 CursorColumn;
    INT32 CursorRow;
    BOOLEAN CursorVisible;
} SIMPLE_TEXT_OUTPUT_MODE;

typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_TEXT_RESET Reset;
    EFI_TEXT_STRING OutputString;
    EFI_TEXT_TEST_STRING TestString;
    EFI_TEXT_QUERY_MODE QueryMode;
    EFI_TEXT_SET_MODE SetMode;
    EFI_TEXT_SET_ATTRIBUTE SetAttribute;
    EFI_TEXT_CLEAR_SCREEN ClearScreen;
    EFI_TEXT_SET_CURSOR_POSITION SetCursorPosition;
    EFI_TEXT_ENABLE_CURSOR EnableCursor;
    SIMPLE_TEXT_OUTPUT_MODE *Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

#define EFI_BOOT_SERVICES_SIGNATURE 0x56524553544f4f42
#define EFI_BOOT_SERVICES_REVISION EFI_SPECIFICATION_VERSION

#define TPL_APPLICATION 4
#define TPL_CALLBACK 8
#define TPL_NOTIFY 16
#define TPL_HIGH_LEVEL 31
typedef
EFI_TPL
(EFIAPI *EFI_RAISE_TPL) (
    IN EFI_TPL NewTpl
);

typedef
VOID
(EFIAPI *EFI_RESTORE_TPL) (
    IN EFI_TPL OldTpl
);

typedef enum {
    AllocateAnyPages,
    AllocateMaxAddress,
    AllocateAddress,
    MaxAllocateType
} EFI_ALLOCATE_TYPE;

typedef enum {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
    EfiUnacceptedMemoryType,
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

typedef UINT64 EFI_PHYSICAL_ADDRESS;

typedef
EFI_STATUS
(EFIAPI *EFI_ALLOCATE_PAGES) (
    IN EFI_ALLOCATE_TYPE Type,
    IN EFI_MEMORY_TYPE MemoryType,
    IN UINTN Pages,
    IN OUT EFI_PHYSICAL_ADDRESS *Memory
);

typedef
EFI_STATUS
(EFIAPI *EFI_FREE_PAGES) (
    IN EFI_PHYSICAL_ADDRESS Memory,
    IN UINTN Pages
);

typedef UINT64 EFI_VIRTUAL_ADDRESS;
#define EFI_MEMORY_DESCRIPTOR_VERSION 1

typedef struct {
    UINT32 Type;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS VirtualStart;
    UINT64 NumberOfPages;
    UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef
EFI_STATUS
(EFIAPI *EFI_GET_MEMORY_MAP) (
    IN OUT UINTN *MemoryMapSize,
    OUT EFI_MEMORY_DESCRIPTOR *MemoryMap,
    OUT UINTN *MapKey,
    OUT UINTN *DescriptorSize,
    OUT UINT32 *DescriptorVersion
);

typedef
EFI_STATUS
(EFIAPI *EFI_ALLOCATE_POOL) (
    IN EFI_MEMORY_TYPE PoolType,
    IN UINTN Size,
    OUT VOID **Buffer
);

typedef
EFI_STATUS
(EFIAPI *EFI_FREE_POOL) (
    IN VOID *Buffer
);

#define EVT_TIMER                         0x80000000
#define EVT_RUNTIME                       0x40000000
#define EVT_NOTIFY_WAIT                   0x00000100
#define EVT_NOTIFY_SIGNAL                 0x00000200
#define EVT_SIGNAL_EXIT_BOOT_SERVICES     0x00000201
#define EVT_SIGNAL_VIRTUAL_ADDRESS_CHANGE 0x60000202

typedef
VOID
(EFIAPI *EFI_EVENT_NOTIFY) (
    IN EFI_EVENT Event,
    IN VOID *Context
);

typedef
EFI_STATUS
(EFIAPI *EFI_CREATE_EVENT) (
    IN UINT32 Type,
    IN EFI_TPL NotifyTpl,
    IN OPTIONAL EFI_EVENT_NOTIFY NotifyFunction,
    IN OPTIONAL VOID *NotifyContext,
    OUT EFI_EVENT *Event
);

typedef enum {
    TimerCancel,
    TimerPeriodic,
    TimerRelative
} EFI_TIMER_DELAY;

typedef
EFI_STATUS
(EFIAPI *EFI_SET_TIMER) (
    IN EFI_EVENT Event,
    IN EFI_TIMER_DELAY Type,
    IN UINT64 TriggerTime
);

typedef
EFI_STATUS
(EFIAPI *EFI_WAIT_FOR_EVENT) (
    IN UINTN NumberOfEvents,
    IN EFI_EVENT *Event,
    OUT UINTN *Index
);

typedef
EFI_STATUS
(EFIAPI *EFI_SIGNAL_EVENT) (
    IN EFI_EVENT Event
);

typedef
EFI_STATUS
(EFIAPI *EFI_CLOSE_EVENT) (
    IN EFI_EVENT Event
);

typedef
EFI_STATUS
(EFIAPI *EFI_CHECK_EVENT) (
    IN EFI_EVENT Event
);

typedef enum {
    EFI_NATIVE_INTERFACE
} EFI_INTERFACE_TYPE;

typedef
EFI_STATUS
(EFIAPI *EFI_INSTALL_PROTOCOL_INTERFACE) (
    IN OUT EFI_HANDLE *Handle,
    IN EFI_GUID *Protocol,
    IN EFI_INTERFACE_TYPE InterfaceType,
    IN VOID *Interface
);

typedef
EFI_STATUS
(EFIAPI *EFI_REINSTALL_PROTOCOL_INTERFACE) (
    IN EFI_HANDLE Handle,
    IN EFI_GUID *Protocol,
    IN VOID *OldInterface,
    IN VOID *NewInterface
);

typedef
EFI_STATUS
(EFIAPI *EFI_UNINSTALL_PROTOCOL_INTERFACE) (
    IN EFI_HANDLE Handle,
    IN EFI_GUID *Protocol,
    IN VOID *Interface
);

typedef
EFI_STATUS
(EFIAPI *EFI_HANDLE_PROTOCOL) (
    IN EFI_HANDLE Handle,
    IN EFI_GUID *Protocol,
    OUT VOID **Interface
);

typedef
EFI_STATUS
(EFIAPI *EFI_REGISTER_PROTOCOL_NOTIFY) (
    IN EFI_GUID *Protocol,
    IN EFI_EVENT Event,
    OUT VOID **Registration
);

typedef enum {
    AllHandles,
    ByRegisterNotify,
    ByProtocol
} EFI_LOCATE_SEARCH_TYPE;

typedef
EFI_STATUS
(EFIAPI *EFI_LOCATE_HANDLE) (
    IN EFI_LOCATE_SEARCH_TYPE SearchType,
    IN OPTIONAL EFI_GUID *Protocol,
    IN OPTIONAL VOID *SearchKey,
    IN OUT UINTN *BufferSize,
    OUT EFI_HANDLE *Buffer
);

typedef struct {
    UINT8 Type;
    UINT8 SubType;
    UINT8 Length[2];
} EFI_DEVICE_PATH_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *EFI_LOCATE_DEVICE_PATH) (
    IN EFI_GUID *Protocol,
    IN OUT EFI_DEVICE_PATH_PROTOCOL **DevicePath,
    OUT EFI_HANDLE *Device
);

typedef
EFI_STATUS
(EFIAPI *EFI_INSTALL_CONFIGURATION_TABLE) (
    IN EFI_GUID *Guid,
    IN VOID *Table
);

typedef
EFI_STATUS
(EFIAPI *EFI_IMAGE_LOAD) (
    IN BOOLEAN BootPolicy,
    IN EFI_HANDLE ParentImageHandle,
    IN EFI_DEVICE_PATH_PROTOCOL *DevicePath,
    IN OPTIONAL VOID *SourceBuffer,
    IN UINTN SourceSize,
    OUT EFI_HANDLE *ImageHandle
);

typedef
EFI_STATUS
(EFIAPI *EFI_IMAGE_START) (
    IN EFI_HANDLE ImageHandle,
    OUT UINTN *ExitDataSize,
    OUT OPTIONAL CHAR16 **ExitData
);


typedef
EFI_STATUS
(EFIAPI *EFI_EXIT) (
    IN EFI_HANDLE ImageHandle,
    IN EFI_STATUS ExitStatus,
    IN UINTN ExitDataSize,
    IN OPTIONAL CHAR16 *ExitData
);

typedef
EFI_STATUS
(EFIAPI *EFI_IMAGE_UNLOAD) (
    IN EFI_HANDLE ImageHandle
);

typedef
EFI_STATUS
(EFIAPI *EFI_EXIT_BOOT_SERVICES) (
    IN EFI_HANDLE ImageHandle,
    IN UINTN MapKey
);

typedef
EFI_STATUS
(EFIAPI *EFI_GET_NEXT_MONOTONIC_COUNT) (
    OUT UINT64 *Count
);

typedef
EFI_STATUS
(EFIAPI *EFI_STALL) (
    IN UINTN Microseconds
);

typedef
EFI_STATUS
(EFIAPI *EFI_SET_WATCHDOG_TIMER) (
    IN UINTN Timeout,
    IN UINT64 WatchdogCode,
    IN UINTN DataSize,
    IN OPTIONAL CHAR16 *WatchdogData
);

typedef
EFI_STATUS
(EFIAPI *EFI_CONNECT_CONTROLLER) (
    IN EFI_HANDLE ControllerHandle,
    IN OPTIONAL EFI_HANDLE *DriverImageHandle,
    IN OPTIONAL EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath,
    IN BOOLEAN Recursive
);

typedef
EFI_STATUS
(EFIAPI *EFI_DISCONNECT_CONTROLLER) (
    IN EFI_HANDLE ControllerHandle,
    IN EFI_HANDLE DriverImageHandle OPTIONAL,
    IN EFI_HANDLE ChildHandle OPTIONAL
);


#define EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL  0x00000001
#define EFI_OPEN_PROTOCOL_GET_PROTOCOL        0x00000002
#define EFI_OPEN_PROTOCOL_TEST_PROTOCOL       0x00000004
#define EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER 0x00000008
#define EFI_OPEN_PROTOCOL_BY_DRIVER           0x00000010
#define EFI_OPEN_PROTOCOL_EXCLUSIVE           0x00000020

typedef
EFI_STATUS
(EFIAPI *EFI_OPEN_PROTOCOL) (
    IN EFI_HANDLE Handle,
    IN EFI_GUID *Protocol,
    OUT VOID **Interface OPTIONAL,
    IN EFI_HANDLE AgentHandle,
    IN EFI_HANDLE ControllerHandle,
    IN UINT32 Attributes
);

typedef
EFI_STATUS
(EFIAPI *EFI_CLOSE_PROTOCOL) (
    IN EFI_HANDLE Handle,
    IN EFI_GUID *Protocol,
    IN EFI_HANDLE AgentHandle,
    IN EFI_HANDLE ControllerHandle
);

typedef struct {
    EFI_HANDLE AgentHandle;
    EFI_HANDLE ControllerHandle;
    UINT32 Attributes;
    UINT32 OpenCount;
} EFI_OPEN_PROTOCOL_INFORMATION_ENTRY;

typedef
EFI_STATUS
(EFIAPI *EFI_OPEN_PROTOCOL_INFORMATION) (
    IN EFI_HANDLE Handle,
    IN EFI_GUID *Protocol,
    OUT EFI_OPEN_PROTOCOL_INFORMATION_ENTRY **EntryBuffer,
    OUT UINTN *EntryCount
);

typedef
EFI_STATUS
(EFIAPI *EFI_PROTOCOLS_PER_HANDLE) (
    IN EFI_HANDLE Handle,
    OUT EFI_GUID ***ProtocolBuffer,
    OUT UINTN *ProtocolBufferCount
);

typedef
EFI_STATUS
(EFIAPI *EFI_LOCATE_HANDLE_BUFFER) (
    IN EFI_LOCATE_SEARCH_TYPE SearchType,
    IN OPTIONAL EFI_GUID *Protocol,
    IN OPTIONAL VOID *SearchKey,
    OUT UINTN *NoHandles,
    OUT EFI_HANDLE **Buffer
);

typedef
EFI_STATUS
(EFIAPI *EFI_LOCATE_PROTOCOL) (
    IN EFI_GUID *Protocol,
    IN OPTIONAL VOID *Registration,
    OUT VOID **Interface
);

typedef
EFI_STATUS
(EFIAPI *EFI_INSTALL_MULTIPLE_PROTOCOL_INTERFACES) (
    IN OUT EFI_HANDLE *Handle,
    ...
);

typedef
EFI_STATUS
(EFIAPI *EFI_UNINSTALL_MULTIPLE_PROTOCOL_INTERFACES) (
    IN EFI_HANDLE Handle,
    ...
);

typedef
EFI_STATUS
(EFIAPI *EFI_CALCULATE_CRC32) (
    IN VOID *Data,
    IN UINTN DataSize,
    OUT UINT32 *Crc32
);

typedef
VOID
(EFIAPI *EFI_COPY_MEM) (
    IN VOID *Destination,
    IN VOID *Source,
    IN UINTN Length
);

typedef
VOID
(EFIAPI *EFI_SET_MEM) (
    IN VOID *Buffer,
    IN UINTN Size,
    IN UINT8 Value
);

typedef
EFI_STATUS
(EFIAPI *EFI_CREATE_EVENT_EX) (
    IN UINT32 Type,
    IN EFI_TPL NotifyTpl,
    IN OPTIONAL EFI_EVENT_NOTIFY NotifyFunction,
    IN CONST OPTIONAL VOID *NotifyContext,
    IN CONST OPTIONAL EFI_GUID *EventGroup,
    OUT EFI_EVENT *Event
);

typedef struct {
    EFI_TABLE_HEADER Hdr;
    //
    // Task Priority Services
    //
    EFI_RAISE_TPL RaiseTPL; // EFI 1.0+
    EFI_RESTORE_TPL RestoreTPL; // EFI 1.0+

    //
    // Memory Services
    //
    EFI_ALLOCATE_PAGES AllocatePages; // EFI 1.0+
    EFI_FREE_PAGES FreePages; // EFI 1.0+
    EFI_GET_MEMORY_MAP GetMemoryMap; // EFI 1.0+
    EFI_ALLOCATE_POOL AllocatePool; // EFI 1.0+
    EFI_FREE_POOL FreePool; // EFI 1.0+

    //
    // Event & Timer Services
    //
    EFI_CREATE_EVENT CreateEvent; // EFI 1.0+
    EFI_SET_TIMER SetTimer; // EFI 1.0+
    EFI_WAIT_FOR_EVENT WaitForEvent; // EFI 1.0+
    EFI_SIGNAL_EVENT SignalEvent; // EFI 1.0+
    EFI_CLOSE_EVENT CloseEvent; // EFI 1.0+
    EFI_CHECK_EVENT CheckEvent; // EFI 1.0+

    //
    // Protocol Handler Services
    //
    EFI_INSTALL_PROTOCOL_INTERFACE InstallProtocolInterface; // EFI 1.0+
    EFI_REINSTALL_PROTOCOL_INTERFACE ReinstallProtocolInterface; // EFI 1.0+
    EFI_UNINSTALL_PROTOCOL_INTERFACE UninstallProtocolInterface; // EFI 1.0+
    EFI_HANDLE_PROTOCOL HandleProtocol; // EFI 1.0+
    VOID* Reserved; // EFI 1.0+
    EFI_REGISTER_PROTOCOL_NOTIFY RegisterProtocolNotify; // EFI 1.0+
    EFI_LOCATE_HANDLE LocateHandle; // EFI 1.0+
    EFI_LOCATE_DEVICE_PATH LocateDevicePath; // EFI 1.0+
    EFI_INSTALL_CONFIGURATION_TABLE InstallConfigurationTable; // EFI 1.0+

    //
    // Image Services
    //
    EFI_IMAGE_LOAD LoadImage; // EFI 1.0+
    EFI_IMAGE_START StartImage; // EFI 1.0+
    EFI_EXIT Exit; // EFI 1.0+
    EFI_IMAGE_UNLOAD UnloadImage; // EFI 1.0+
    EFI_EXIT_BOOT_SERVICES ExitBootServices; // EFI 1.0+

    //
    // Miscellaneous Services
    //
    EFI_GET_NEXT_MONOTONIC_COUNT GetNextMonotonicCount; // EFI 1.0+
    EFI_STALL Stall; // EFI 1.0+
    EFI_SET_WATCHDOG_TIMER SetWatchdogTimer; // EFI 1.0+

    //
    // DriverSupport Services
    //
    EFI_CONNECT_CONTROLLER ConnectController; // EFI 1.1
    EFI_DISCONNECT_CONTROLLER DisconnectController;// EFI 1.1+

    //
    // Open and Close Protocol Services
    //
    EFI_OPEN_PROTOCOL OpenProtocol; // EFI 1.1+
    EFI_CLOSE_PROTOCOL CloseProtocol; // EFI 1.1+
    EFI_OPEN_PROTOCOL_INFORMATION OpenProtocolInformation; // EFI 1.1+

    //
    // Library Services
    //
    EFI_PROTOCOLS_PER_HANDLE ProtocolsPerHandle; // EFI 1.1+
    EFI_LOCATE_HANDLE_BUFFER LocateHandleBuffer; // EFI 1.1+
    EFI_LOCATE_PROTOCOL LocateProtocol; // EFI 1.1+
    EFI_INSTALL_MULTIPLE_PROTOCOL_INTERFACES InstallMultipleProtocolInterfaces; // EFI 1.1+
    EFI_UNINSTALL_MULTIPLE_PROTOCOL_INTERFACES UninstallMultipleProtocolInterfaces; // EFI 1.1+

    //
    // 32-bit CRC Services
    //
    EFI_CALCULATE_CRC32 CalculateCrc32; // EFI 1.1+

    //
    // Miscellaneous Services
    //
    EFI_COPY_MEM CopyMem; // EFI 1.1+
    EFI_SET_MEM SetMem; // EFI 1.1+
    EFI_CREATE_EVENT_EX CreateEventEx; // UEFI 2.0+
} EFI_BOOT_SERVICES;

typedef struct{
    EFI_GUID VendorGuid;
    VOID *VendorTable;
} EFI_CONFIGURATION_TABLE;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    UINT32 FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    EFI_RUNTIME_SERVICES *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE *ConfigurationTable;
} EFI_SYSTEM_TABLE;
