#!/usr/bin/python3
import shutil
import os
import options
from image_utils import ultra
from typing import Optional, List, Callable, Generator

normal_boot_cfgs = \
"""
default-entry = {default_entry}

[i686_lower_half]
protocol=ultra
setup-apm = true

cmdline    = {cmdline}
binary     = "/boot/kernel_i686_lower_half"

video-mode:
    format = xrgb8888

{extra_cfg_entries}

[i686_lower_half_pae]
protocol=ultra

cmdline    = {cmdline}
binary     = "/boot/kernel_i686_lower_half"

page-table:
    levels     = 3 
    constraint = exactly

video-mode:
    format = xrgb8888

{extra_cfg_entries}

[i686_higher_half]
protocol=ultra
setup-apm = true

cmdline    = {cmdline}
binary:
    path = "/boot/kernel_i686_higher_half"

video-mode:
    format = xrgb8888
    
higher-half-exclusive = true

{extra_cfg_entries}

[i686_higher_half_pae]
protocol=ultra

cmdline    = {cmdline}
binary     = "/boot/kernel_i686_higher_half"

page-table:
    levels     = 3 
    constraint = exactly

video-mode:
    format = xrgb8888

higher-half-exclusive = true

{extra_cfg_entries}

[amd64_lower_half]
protocol=ultra
setup-apm = true

cmdline    = {cmdline}
binary     = "/boot/kernel_amd64_lower_half"

video-mode:
    format = xrgb8888

{extra_cfg_entries}

[amd64_lower_half_5lvl]
protocol=ultra

cmdline    = {cmdline}
binary     = "/boot/kernel_amd64_lower_half"

page-table:
    levels     = 5 
    constraint = exactly

video-mode:
    format = xrgb8888

{extra_cfg_entries}

[amd64_higher_half]
protocol=ultra
setup-apm = true

cmdline = {cmdline}
binary:
    path = "/boot/kernel_amd64_higher_half"
    allocate-anywhere = true

video-mode:
    format = xrgb8888

stack:
    size = 0x4000

higher-half-exclusive = true

{extra_cfg_entries}

[amd64_higher_half_5lvl]
protocol=ultra
setup-apm = true

cmdline = {cmdline}
binary:
    path = "/boot/kernel_amd64_higher_half"
    allocate-anywhere = true

page-table:
    levels     = 5 
    constraint = exactly

video-mode:
    format = xrgb8888

higher-half-exclusive = true

{extra_cfg_entries}

[aarch64_lower_half]
protocol=ultra

cmdline = {cmdline}
binary:
    path = "/boot/kernel_aarch64_higher_half"
    allocate-anywhere = true

# UEFI implementations on QEMU don't provide consistent GOP
video-mode = unset

{extra_cfg_entries}

[aarch64_higher_half]
protocol=ultra

cmdline = {cmdline}
binary:
    path = "/boot/kernel_aarch64_higher_half"
    allocate-anywhere = true

higher-half-exclusive = true

# UEFI implementations on QEMU don't provide consistent GOP
video-mode = unset

{extra_cfg_entries}

[aarch64_lower_half_5lvl]
protocol=ultra

cmdline = {cmdline}
binary:
    path = "/boot/kernel_aarch64_higher_half"
    allocate-anywhere = true

page-table:
    levels     = 5
    # We set this to 'maximum' and not 'exactly' because not all QEMU
    # packages currently support 52 bit input addresses, most notably
    # the 'ubuntu-latest' version of QEMU on github actions doesn't. 
    constraint = maximum

# UEFI implementations on QEMU don't provide consistent GOP
video-mode = unset

{extra_cfg_entries}

[aarch64_higher_half_5lvl]
protocol=ultra

cmdline = {cmdline}
binary:
    path = "/boot/kernel_aarch64_higher_half"
    allocate-anywhere = true

page-table:
    levels     = 5
    # We set this to 'maximum' and not 'exactly' because not all QEMU
    # packages currently support 52 bit input addresses, most notably
    # the 'ubuntu-latest' version of QEMU on github actions doesn't. 
    constraint = maximum

higher-half-exclusive = true

# UEFI implementations on QEMU don't provide consistent GOP
video-mode = unset

{extra_cfg_entries}
"""


def make_normal_boot_config(
    default_entry: str, cmdline: str,
    modules: Optional[List['ultra.Module']] = None
) -> str:
    extra_cfg_entries = ""
    modules = modules or []

    for module in modules:
        extra_cfg_entries += str(module)
        extra_cfg_entries += "\n"

    return normal_boot_cfgs.format(default_entry=default_entry,
                                   cmdline=cmdline,
                                   extra_cfg_entries=extra_cfg_entries)


def build(
    opt_getter: Callable, br_type: str, fs_type: str,
    config: str, out_path: Optional[str] = None,
    cleanup: bool = True
) -> Generator[ultra.DiskImage, None, None]:
    uefi_paths = [
        opt_getter(options.X64_HYPER_UEFI_OPT),
        opt_getter(options.AA64_HYPER_UEFI_OPT),
    ]
    uefi_paths = list(filter(lambda x: x is not None, uefi_paths))

    kernel_dir = opt_getter(options.KERNEL_DIR_OPT)
    root_dir = opt_getter(options.INTERM_DIR_OPT)

    kernel_paths = [
        os.path.join(kernel_dir, "kernel_i686_lower_half"),
        os.path.join(kernel_dir, "kernel_i686_higher_half"),
        os.path.join(kernel_dir, "kernel_amd64_lower_half"),
        os.path.join(kernel_dir, "kernel_amd64_higher_half"),
        os.path.join(kernel_dir, "kernel_aarch64_lower_half"),
        os.path.join(kernel_dir, "kernel_aarch64_higher_half"),
    ]

    kernel_image_dir = os.path.join(root_dir, "boot")
    os.makedirs(kernel_image_dir, exist_ok=True)

    for kernel in kernel_paths:
        shutil.copy(kernel, kernel_image_dir)

    with ultra.DiskImage(
        root_dir, br_type, fs_type,
        hyper_config=config,
        hyper_uefi_binary_paths=uefi_paths,
        hyper_iso_br_path=opt_getter(options.HYPER_ISO_BR_OPT),
        hyper_installer_path=opt_getter(options.INSTALLER_OPT),
        out_path=out_path, cleanup=cleanup
    ) as di:
        yield di
