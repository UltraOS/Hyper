# Hyper

A fast & lightweight bootloader for the x86 & ARM architectures.

[![CI](https://github.com/UltraOS/Hyper/actions/workflows/main.yml/badge.svg?branch=master)](https://github.com/UltraOS/Hyper/actions/workflows/main.yml)

## Features & Support

Boot sectors for HDD, El Torito & hybrid ISO formats.  
Support for ELF binaries targeting i386, amd64 and aarch64.

### Platforms
- BIOS
- UEFI

### Partition Layouts
- GPT
- MBR/EBR
- RAW
- PXE (network boot)

### File Systems
- FAT12/16/32
- ISO9660

### Boot Protocols
- [Ultra](https://github.com/UltraOS/UltraProtocol)

## Usage

Grab all the files from the latest release and proceed to the steps below
depending on your target.

### BIOS boot with MBR/EBR
1. Create an MBR partitioned image with at least one file system.
2. Run `./hyper_install-<myplatform> ./my-image`.

### UEFI boot with MBR/EBR/GPT
1. Create an MBR/GPT partitioned image.
2. Create a FAT-formatted EFI system partition.
3. Copy `BOOTX64.EFI` (for x86) or `BOOTAA64.EFI` (for aarch64) to `/EFI/BOOT/` on that partition.

### BIOS (+UEFI) boot with hybrid ISO
1. Create a directory to use as the root of the ISO image.
2. Copy `hyper_iso_boot` into the directory from step 1.
3. (optionally) for UEFI support:
    - Create a raw FAT image containing at least `EFI/BOOT/BOOT{X64,AA64}.EFI` to use as the ESP.
    - Copy the raw image into the directory created in step 1.
4. Create an ISO image with the following parameters:
    - `hyper_iso_boot` as the El Torito boot record, with 4 sectors preloaded and the boot information table enabled, without emulation.
    - (optionally) `<my-fat-esp-image>` as the EFI El Torito boot option.
5. (optionally) Run `./hyper_install-<myplatform> ./my-image.iso` to make the image bootable as an HDD under BIOS.

An example of such a command using the `xorriso` utility:
```
xorriso -as mkisofs \
  -b <relative path to hyper_iso_boot from step 2 within the directory> \
  -no-emul-boot \
  -boot-load-size 4 \
  -boot-info-table \
  --efi-boot <relative path to image from step 3 within the directory> \
  -efi-boot-part --efi-boot-image \
  --protective-msdos-label <path to the directory from step 1> \
  -o <out-image.iso>
```

## Configuration & Boot

Hyper operates based on the instructions given in the boot configuration file.
Its format, as well as the expected location of the file, is described below.

### Location On Disk

The configuration file must be called `hyper.cfg` and located at one of the
following paths:
- `/hyper.cfg`
- `/boot/hyper.cfg`
- `/boot/hyper/hyper.cfg`

The loader uses the first configuration file it finds, searching in the order
listed above. Configuration files cannot currently be chained, but it's a
planned feature.

The file can reside on any disk & partition supported by the loader.

### Format & Syntax

The configuration file format loosely resembles YAML, but simplified:
- `#` denotes a comment, which spans the entire line and can contain any characters.
- `x = y` denotes a key/value pair; `x` doesn't have to be unique.
- `x:` denotes an object called `x`, which continues on the next line and has at least one key/value pair inside.
- `[Name]` denotes a loadable entry with a unique name that the loader can act on depending on the protocol type.

The configuration file is predominantly ASCII, with some exceptions described
below.

The reserved characters are `[`, `]`, `:`, `=` and space.  
They are, however, allowed to appear inside quoted values, along with any other
characters outside the ASCII set, such as UTF-8 encoded code points.

A value can be one of the following types:
- Integer, both signed & unsigned, in decimal, hex, octal or binary (up to 64 bits).
- Boolean, literal `true`/`false` in any case.
- Null, literal `null` in any case.
- String, both quoted (with `'` or `"`) and unquoted. Note that string escaping is not supported; combine `'` and `"` instead.

The loader attempts to interpret each value in the order above, stopping at the
first type that converts successfully.

The format relies on whitespace to determine scopes & nesting levels, so it is
expected to be consistent throughout the configuration file.

Every configuration file consists of the following:
- (optionally) any number of global variables at the top;
- at least one loadable entry, denoting a new scope, which contains at least the `protocol` key/value pair.

Paths use a special format that combines POSIX with hyper-specific extensions,
and must always be absolute.  
A leading `/` refers to the disk & partition the current configuration file was
loaded from.

Paths can optionally start with a prefix, such as:
- `::/` - same as `/`
- `hd0::/` - first hard disk, treated as unpartitioned media
- `cd0::/` - first optical disc (the disc the loader booted from, under BIOS)
- `hd0-part0::/` - first hard disk, partition 0
- `hd0-partuuid-e0e0d5fb-48fa-4428-b73d-43d3f7e49a8a::/` - first hard disk, partition with this GPT UUID
- `partuuid-e0e0d5fb-48fa-4428-b73d-43d3f7e49a8a::/` - partition with this GPT UUID on any disk
- `diskuuid-e0e0d5fb-48fa-4428-b73d-43d3f7e49a8a-part0::/` - disk with this GPT disk UUID, partition 0
- `pxe::/` (or `tftp::/`) - the PXE/TFTP server the loader booted from (network boot)

All prefixes are case-insensitive; the lowercase form shown here is the
canonical one. All numbers are specified in hexadecimal.

Disks are addressed by kind plus a 0-based index within that kind: `hdN` for
hard disks and `cdN` for optical drives, in the order the loader enumerates
them. `DISKUUID-<guid>` addresses a disk by its GPT disk GUID, regardless of
kind.

A GPT partition GUID is globally unique, so a partition can be addressed by
`PARTUUID-<guid>` on its own, without naming a disk. A partition index or the
raw selector, on the other hand, always needs a disk to resolve against.

Under UEFI, disks are classified by their device paths (like GRUB), so any
optical drive is exposed as `cdN`. Under BIOS the only disc that can be reliably
recognized as optical is the one the loader booted from (it becomes `cd0`); all
other drives are treated as hard disks.

An example of a configuration file using the `Ultra` protocol:
```py
# Not necessary, but we specify it for good measure
default-entry = "MyOS"

[MyOS]
protocol = ultra

cmdline  = "--some-option --some-option2=true"

binary:
    path              = "/boot/os.bin"
    allocate-anywhere = true

module:
    name = "kmap"
    path = "hd0-part0::/boot/symbols.bin"

module:
    type = "memory"
    name = "allocator-bootstrap"

    # 1M
    size=0x100000

video-mode:
    width=1024
    height=768
    bpp=32
    format=xrgb8888
```

## Building

Run `./build.py`, optionally with the `--platform {bios,uefi}`,
`--arch {i686,amd64,aarch64}` and `--toolchain {gcc,clang}` flags.

*The build script assumes that you already have cmake & git installed.*

### Supported Systems
- Linux
- macOS (x86/aarch64)
- Windows (via WSL)

### Dependencies & Package Managers

The build script attempts to automatically fetch all the dependencies needed to
build the cross-compiler. This step can be skipped by passing the
`--skip-dependencies` flag.

Currently supported package managers:
- apt
- pacman
- brew

Support for other systems/package managers can be trivially added by extending
the [BuildUtils](https://github.com/UltraOS/BuildUtils) library.
