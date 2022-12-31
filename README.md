# Hyper

A fast & lightweight bootloader for the x86 architecture.

[![CI](https://github.com/UltraOS/Hyper/actions/workflows/main.yml/badge.svg?branch=master)](https://github.com/UltraOS/Hyper/actions/workflows/main.yml)

## Features & Support
Boot sectors for HDD, el-torito & hybrid ISO formats.  
Support for ELF binaries targeting I386 and AMD64.

### Platforms
- BIOS
- UEFI

### Partition Layouts:
- GPT
- MBR/EBR
- RAW

### File Systems:
- FAT12/16/32
- ISO9660

### Boot Protocols:
- [Ultra](https://github.com/UltraOS/UltraProtocol)

## Usage
          
Grab all the files from the latest release, and proceed to the steps below depending on your target.

### BIOS boot with MBR/EBR:
1. Create an MBR partitioned image with at least one file system.
2. Run `./hyper_install-<myplatform> ./my-image`.
   
### UEFI boot with MBR/EBR/GPT:
1. Create an MBR/GPT partitioned image.
2. Create a FAT-formatted EFI system partition.
3. Copy `BOOTX64.EFI` to `/EFI/BOOT/` on that partition.

### BIOS (+UEFI) boot with ISO hybrid:
1. Create a directory that will be used as the root of the iso image.
2. Copy `hyper_iso_boot` into the directory from step 1.
3. (optionally) for UEFI support:
    - Create a raw FAT image with at least a `EFI/BOOT/BOOTX64.EFI` inside that will be used as the ESP.
    - Copy the raw image into the directory created in step 1.
4. Create an iso image with the following parameters:
    - `hyper_iso_boot` as the el-torito boot record with 4 sectors preloaded & boot information table enabled without emulation.
    - (optionally) `<my-fat-esp-image>` as the EFI el-torito boot option.
5. (optionally) Run `./hyper-install-<myplatform> ./my-image.iso` to make the image bootable as an HDD with BIOS.

An example of such command using the `xorriso` utility:
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
Hyper loader operates based on the instructions given in the boot configuration file.  
The format as well as expected location of the configuration file are described below.
     
### Location On Disk
The configuration file must be called `hyper.cfg` and contained in one the following paths:
- /hyper.cfg
- /boot/hyper.cfg
- /boot/hyper/hyper.cfg

The loader uses the first configuration file it finds, searching in the aforementioned order.  
Currently, configuration files cannot be chained, but it's a planned feature.

The file can be contained on any disk & partition as long as it's supported by the loader.

### Format & Syntax
The format of the configuration file somewhat resembles YAML but simplified:
- `#` denotes a comment, which spans the entire line and can contain any characters.
- `x = y` denotes a key/value pair, `x` doesn't have to be unique.
- `x:` denotes an object called `x`, which continues on the next line, and has at least one key/value pair inside.
- `[Name]` denotes a loadable entry with a unique name that the loader can act on depending on the protocol type.

The configuration file is predominantly ASCII with some exceptions described below.

Reserved characters are: `[` `]` `:` `=` ` `.  
However, these characters are allowed to appear inside quoted values, including
any other characters that are not part of the ASCII set, such as UTF-8 encoded code-points.

Value types can be one of the following:
- Integer, both signed & unsigned, including decimal, hex, octal and binary (up to 64 bits).
- Boolean, literal `true`/`false` in any case.
- Null, literal `null` in any case.
- String, both quoted (with `'` or `"`) and unquoted.

The loader attempts to interpret each value in the aforementioned order,
stopping at the first type that succeeds the conversion.

The format relies on whitespace to determine scopes & nesting levels, so it is
expected to be consistent throughout the configuration file.
                                                        
Every configuration file consists of the following:
- (optionally) Any number of global variables at the top.
- At least one loadable entry, denoting a new scope, which contains at least
the`protocol` key/value pair.

Paths are specified using a special format, which is a combination of POSIX and
hyper-specific extensions and must always be absolute.  
Leading `/` refers to the disk & partition where the current configuration file was loaded from.

Paths can optionally start with prefixes, such as:
- `::/` - same as `/`
- `DISK0::/` - disk 0 treated as unpartitioned media
- `DISK80-PART0::/` - disk 0x80, partition 0
- `DISK3-GPTUUID-E0E0D5FB-48FA-4428-B73D-43D3F7E49A8A::/` - disk 3, GPT partition with this UUID
- `DISKUUID-E0E0D5FB-48FA-4428-B73D-43D3F7E49A8A-GPT2::/` - disk with this UUID, GPT partition 2
- `DISKUUID-E0E0D5FB-48FA-4428-B73D-43D3F7E49A8A-PARTUUID-E0E0D5FB-48FA-4428-B73D-43D3F7E49A8A::/` - disk with this UUID, partition with this UUID
      
Note that all numbers are specified in hexadecimal.
             
Disk numbers are assigned as follows:
- For BIOS it's the index of a disk queried via `INT13, AH=48`
- For UEFI it's the index of the handle returned by querying `EFI_BLOCK_IO_PROTOCOL_GUID`

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
    path = "DISK80-PART0::/boot/symbols.bin"

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
    constraint=at-least
```

## Building
1. Run `./build.sh <PLATFORM>` where `<PLATFORM>` is either `BIOS` or `UEFI`.

*Build scripts assumes that you have cmake & git already installed.*
                              
### Supported systems:
- Linux
- MacOS (x86/aarch64)
- Windows (via WSL)
     
### Dependencies & Package Managers:
The build script attempts to automatically fetch all the dependencies needed to build
the cross-compiler. This step can be skipped by passing `--skip-dependencies` to `toolchain/build.py`.

Currently supported package managers:
- apt
- pacman
- brew

Support for other systems/package managers can be trivially added by extending `toolchain/build.py`.
