import path_guesser

KERNEL_DIR_OPT = "--kernel-dir"
INTERM_DIR_OPT = "--intermediate-dir"
INSTALLER_OPT = "--hyper-install-path"
X64_HYPER_UEFI_OPT = "--x64-hyper-uefi-path"
AA64_HYPER_UEFI_OPT = "--aa64-hyper-uefi-path"
HYPER_ISO_BR_OPT = "--hyper-iso-br-path"
X64_UEFI_FIRMWARE_OPT = "--x64-uefi-firmware-path"
AA64_UEFI_FIRMWARE_OPT = "--aa64-uefi-firmware-path"
QEMU_GUI_OPT = "--qemu-enable-gui"


def add_base_options(add_opt_cb):
    binaries_path = path_guesser.guess_path_to_kernel_binaries()
    installer_path = path_guesser.guess_path_to_installer()
    hyper_x64_uefi_path = path_guesser.guess_path_to_hyper_uefi("amd64")
    hyper_aa64_uefi_path = path_guesser.guess_path_to_hyper_uefi("aarch64")
    hyper_iso_br_path = path_guesser.guess_path_to_hyper_iso_br()

    add_opt_cb(KERNEL_DIR_OPT, type=str,
               default=binaries_path,
               help="Path to the directory with kernel binaries",
               required=binaries_path is None)
    add_opt_cb(INTERM_DIR_OPT, type=str,
               default=path_guesser.guess_path_to_interm_dir(),
               help="Path to the intermediate data directory")
    add_opt_cb(INSTALLER_OPT, type=str,
               default=installer_path,
               help="Path to the hyper installer")
    add_opt_cb(X64_HYPER_UEFI_OPT, type=str,
               default=hyper_x64_uefi_path,
               help="Path to the x64 hyper UEFI binary")
    add_opt_cb(AA64_HYPER_UEFI_OPT, type=str,
               default=hyper_aa64_uefi_path,
               help="Path to the aa64 hyper UEFI binary")
    add_opt_cb(HYPER_ISO_BR_OPT, type=str,
                default=hyper_iso_br_path,
                help="Path to the hyper ISO boot record")


def add_test_options(add_opt_cb):
    add_opt_cb(X64_UEFI_FIRMWARE_OPT, type=str,
               default=path_guesser.guess_path_to_uefi_firmware("amd64"),
               help="Path to the x64 UEFI firmware")
    add_opt_cb(AA64_UEFI_FIRMWARE_OPT, type=str,
               default=path_guesser.guess_path_to_uefi_firmware("aarch64"),
               help="Path to the aarch64 UEFI firmware")
    add_opt_cb("--qemu-enable-gui", action="store_true",
               help="Run QEMU with GUI")


def check_availability(get_opt_cb, hdd_only=False, gpt_only=False,
                       need_firmware=True):
    has_uefi_x64 = get_opt_cb(X64_HYPER_UEFI_OPT) is not None
    has_uefi_aa64 = get_opt_cb(AA64_HYPER_UEFI_OPT) is not None

    if need_firmware:
        if has_uefi_x64:
            has_uefi_x64 = get_opt_cb(X64_UEFI_FIRMWARE_OPT) is not None
        if has_uefi_aa64:
            has_uefi_aa64 = get_opt_cb(AA64_UEFI_FIRMWARE_OPT) is not None

    has_uefi = (has_uefi_x64 or has_uefi_aa64)
    has_bios = get_opt_cb(INSTALLER_OPT) is not None
    has_bios_iso = get_opt_cb(HYPER_ISO_BR_OPT) is not None

    if not has_uefi and (not has_bios or gpt_only) and \
      (not has_bios_iso or hdd_only):
        raise FileNotFoundError("Couldn't find any viable boot options! "
                                "Please specify paths manually.")

    return has_uefi_x64, has_uefi_aa64, has_bios, has_bios_iso
