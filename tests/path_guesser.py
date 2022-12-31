import os
import platform
import subprocess


def abs_path_to_current_dir() -> str:
    return os.path.dirname(os.path.abspath(__file__))


def abs_path_to_project_root() -> str:
    curdir = abs_path_to_current_dir()
    pardir = os.path.join(curdir, os.path.pardir)

    return os.path.abspath(pardir)


def __guess_or_none(guess, validity_check=os.F_OK):
    return guess if os.access(guess, validity_check) else None


def __guess_with_middle_parts_or_none(middle_parts, postfix,
                                      prefix=abs_path_to_project_root(),
                                      validity_check=os.F_OK):
    for mp in middle_parts:
        path = os.path.join(prefix, mp, postfix)

        res = __guess_or_none(path, validity_check)
        if res is not None:
            return res

    return None


def guess_path_to_kernel_binaries():
    guess = os.path.join(abs_path_to_current_dir(), "kernel/build")

    kernels = [
        "kernel_i686_lower_half",
        "kernel_i686_higher_half",
        "kernel_amd64_higher_half",
        "kernel_amd64_lower_half"
    ]
    for kernel in kernels:
        if not os.access(os.path.join(guess, kernel), os.F_OK):
            return None

    return guess


def guess_path_to_interm_dir():
    return os.path.join(abs_path_to_current_dir(), "temp-data")

def guess_path_to_installer():
    guess = os.path.join(abs_path_to_project_root(),
                         "installer/hyper_install")
    return __guess_or_none(guess, os.X_OK)


def guess_path_to_hyper_uefi():
    middle_parts = [
        "build-clang-uefi",
        "build-gcc-uefi",
    ]
    postfix = os.path.join("loader", "hyper_uefi")

    return __guess_with_middle_parts_or_none(middle_parts, postfix)


def guess_path_to_hyper_iso_br():
    middle_parts = [
        "build-clang-bios",
        "build-gcc-bios",
    ]
    postfix = os.path.join("loader", "hyper_iso_boot")

    return __guess_with_middle_parts_or_none(middle_parts, postfix)


def guess_path_to_uefi_firmware():
    middle_parts = [
        "ovmf",
        os.path.join("edk2-ovmf", "x64"),
    ]
    prefix = os.path.join("/usr", "share")
    postfix = "OVMF.fd"

    res = __guess_with_middle_parts_or_none(middle_parts, postfix, prefix)

    if res is None and platform.system() == "Darwin":
        bp = subprocess.run(["brew", "--prefix", "qemu"],
                            stdout=subprocess.PIPE,
                            universal_newlines=True)
        if bp.returncode == 0:
            guess = os.path.join(bp.stdout.strip(), "share", "qemu",
                                 "edk2-x86_64-code.fd")
            res = __guess_or_none(guess)

    return res
