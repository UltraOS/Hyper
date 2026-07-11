import os

from image_utils import path_guesser as pg
from image_utils import uefi


def abs_path_to_current_dir():
    return os.path.dirname(os.path.abspath(__file__))


def abs_path_to_project_root():
    return os.path.abspath(os.path.join(abs_path_to_current_dir(), os.pardir))


def _hyper_build_or_none(middle_parts, postfix):
    return pg.valid_path_with_middle_parts_or_none(
        middle_parts, postfix, prefix=abs_path_to_project_root())


def guess_path_to_kernel_binaries():
    guess = os.path.join(abs_path_to_current_dir(), "kernel/build")

    kernels = [
        "kernel_i686_lower_half",
        "kernel_i686_higher_half",
        "kernel_amd64_higher_half",
        "kernel_amd64_lower_half",
        "kernel_aarch64_lower_half",
        "kernel_aarch64_higher_half",
    ]
    for kernel in kernels:
        if pg.valid_path_or_none(os.path.join(guess, kernel)) is None:
            return None

    return guess


def guess_path_to_interm_dir():
    return os.path.join(abs_path_to_current_dir(), "temp-data")


def guess_path_to_installer():
    guess = os.path.join(abs_path_to_project_root(), "installer/hyper_install")
    return pg.valid_path_or_none(guess, os.X_OK)


def guess_path_to_hyper_uefi(arch):
    return _hyper_build_or_none(
        [f"build-clang-{arch}-uefi", f"build-gcc-{arch}-uefi"],
        os.path.join("loader", "hyper_uefi")
    )


def guess_path_to_hyper_iso_br():
    return _hyper_build_or_none(
        ["build-clang-i686-bios", "build-gcc-i686-bios"],
        os.path.join("loader", "hyper_iso_boot")
    )


def guess_path_to_uefi_firmware(arch):
    return uefi.get_path_to_qemu_uefi_firmware(arch)
