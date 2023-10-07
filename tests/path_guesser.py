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

def __guess_with_prefixes_or_none(prefixes, postfix, validity_check=os.F_OK):
    for prefix in prefixes:
        path = os.path.join(prefix, postfix)

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
        "kernel_amd64_lower_half",
        "kernel_aarch64_lower_half",
        "kernel_aarch64_higher_half",
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


def guess_path_to_hyper_uefi(arch):
    middle_parts = [
        f"build-clang-{arch}-uefi",
        f"build-gcc-{arch}-uefi",
    ]
    postfix = os.path.join("loader", "hyper_uefi")

    return __guess_with_middle_parts_or_none(middle_parts, postfix)


def guess_path_to_hyper_iso_br():
    middle_parts = [
        "build-clang-i686-bios",
        "build-gcc-i686-bios",
    ]
    postfix = os.path.join("loader", "hyper_iso_boot")

    return __guess_with_middle_parts_or_none(middle_parts, postfix)


def guess_path_to_uefi_firmware(arch):
    edk2_arch = arch
    if edk2_arch == "amd64":
        edk2_arch = "x86_64"

    prefixes = [
        "/usr",
    ]

    try:
        bp = subprocess.run(["brew", "--prefix", "qemu"],
                            stdout=subprocess.PIPE,
                            universal_newlines=True)
        if bp.returncode == 0:
            prefixes.append(bp.stdout.strip())
    except FileNotFoundError:
        pass

    res = __guess_with_prefixes_or_none(
        prefixes,
        f"share/qemu/firmware/60-edk2-{edk2_arch}.json"
    )
    if res is None:
        return None

    import json
    with open(res) as file:
        path_json = json.load(file)
    guess = path_json.get("mapping", {}).get("executable", {}).get("filename", {})

    return __guess_or_none(guess) if guess else None
