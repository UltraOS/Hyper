#!/usr/bin/python3
import argparse
import options
import disk_image as di
import shutil
import sys
from typing import Callable, Any


def argparse_optgetter(obj: Any) -> Callable:
    def do_getopt(name):
        return getattr(obj, name[2:].replace("-", "_"))

    return do_getopt


if __name__ == "__main__":
    parser = argparse.ArgumentParser(usage="Make a loader image")
    options.add_base_options(parser.add_argument)

    parser.add_argument("fs_type", choices=["ISO9660", "FAT12", "FAT16", "FAT32"],
                        help="Type of file system to use for the image")
    parser.add_argument("out_path", type=str, help="Path to the output image")
    parser.add_argument("--br-type", default="MBR", choices=["MBR", "GPT"],
                        help="Type of boot record to use for the image")

    kernel_types = [
        "i686_lower_half",
        "i686_higher_half",
        "i686_lower_half_pae",
        "i686_higher_half_pae",
        "amd64_lower_half",
        "amd64_higher_half",
        "amd64_lower_half_5lvl",
        "amd64_higher_half_5lvl",
        "aarch64_lower_half",
        "aarch64_higher_half",
        "aarch64_lower_half_5lvl",
        "aarch64_higher_half_5lvl",
    ]
    parser.add_argument("--kernel-type", default="amd64_higher_half",
                        help="Type of kernel to set as default in config",
                        choices=kernel_types)
    args = parser.parse_args()

    getopt = argparse_optgetter(args)
    has_uefi_x64, has_uefi_aa64, has_bios, has_bios_br = \
        options.check_availability(getopt, args.fs_type != "ISO9660",
                                   args.br_type == "GPT", False)
    has_uefi = has_uefi_x64 or has_uefi_aa64

    cfg = di.make_normal_boot_config(args.kernel_type, "no-shutdown")
    try:
        image = next(di.build(getopt, args.br_type, args.fs_type,
                     cfg, args.out_path, False))
    finally:
        shutil.rmtree(getopt(options.INTERM_DIR_OPT))

    print(f"Created an image at {image.path}")
    print("The image can be booted with:")

    kernel_is_x86 = "aarch64" not in args.kernel_type

    if kernel_is_x86:
        if has_uefi_x64:
            print("- UEFI")

        if args.fs_type == "ISO9660":
            if has_bios_br:
                print("- BIOS as CD")

        if has_bios:
            print("- BIOS as HDD")
        elif has_bios and args.br_type != "GPT":
            print("- BIOS")
    elif has_uefi_aa64:
        print("- UEFI")
