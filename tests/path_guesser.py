import os


def abs_path_to_current_dir() -> str:
    return os.path.dirname(os.path.abspath(__file__))


def abs_path_to_project_root() -> str:
    curdir = abs_path_to_current_dir()
    pardir = os.path.join(curdir, os.path.pardir)

    return os.path.abspath(pardir)


def __guess_or_none(guess, validity_check=os.F_OK):
    return guess if os.access(guess, validity_check) else None


def guess_path_to_kernel_binaries():
    guess = os.path.join(abs_path_to_current_dir(), "kernel/build")
    
    kernels = [
        "kernel_i686",
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
    guess = os.path.join(abs_path_to_project_root(),
                         "build_uefi/loader/BOOTX64.EFI")
    return __guess_or_none(guess)


def guess_path_to_hyper_iso_br():
    guess = os.path.join(abs_path_to_project_root(),
                         "build_bios/loader/hyper_iso_boot")
    return __guess_or_none(guess)


def guess_path_to_uefi_firmware():
    guess = "/usr/share/ovmf/OVMF.fd"
    return __guess_or_none(guess)
