#!/usr/bin/python3
import argparse
import options
import disk_image as di
import shutil


def argparse_optgetter(object):
    def do_getopt(name):
        return getattr(object, name[2:].replace("-", "_"))

    return do_getopt


if __name__ == "__main__":
    parser = argparse.ArgumentParser(usage="Make a loader image")
    options.add_base_options(parser.add_argument)

    parser.add_argument("fs_type", choices=["ISO9660", "FAT12", "FAT16", "FAT32"],
                        help="Type of file system to use for the image")
    parser.add_argument("out_path", type=str, help="Path to the output image")
    parser.add_argument("--br-type", default="MBR", choices=["MBR", "GPT"],
                        help="Type of boot record to use for the image")
    parser.add_argument("--kernel-type", default="amd64_higher_half",
                        help="Type of kernel to set as default in config",
                        choices=["i386", "amd64_lower_half", "amd64_higher_half"])
    args = parser.parse_args()

    getopt = argparse_optgetter(args)
    has_uefi, has_bios, has_bios_br = \
        options.check_availability(getopt, args.fs_type != "ISO9660",
                                   args.br_type == "GPT", False)

    root_dir = di.prepare_test_fs_root(getopt)
    cfg = di.make_normal_boot_config(args.kernel_type, "no-shutdown")

    try:
        di.fs_root_set_cfg(root_dir, cfg)
        image = di.DiskImage(root_dir, args.br_type, args.fs_type, has_uefi,
                             has_bios_br, getopt(options.INSTALLER_OPT),
                             args.out_path)
    finally:
        shutil.rmtree(root_dir)

    print(f"Created an image at {image.path}")
    print("The image can be booted with:")

    if has_uefi:
        print("- UEFI")

    if args.fs_type == "ISO9660":
        if has_bios_br:
            print("- BIOS as CD")

        if has_bios:
            print("- BIOS as HDD")
    elif has_bios and args.br_type != "GPT":
        print("- BIOS")
