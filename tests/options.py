import path_guesser

KERNEL_DIR_OPT = "--kernel-dir"
INTERM_DIR_OPT = "--intermediate-dir"
INSTALLER_OPT = "--hyper-install-path"
HYPER_UEFI_OPT = "--hyper-uefi-path"
HYPER_ISO_BR_OPT = "--hyper-iso-br-path"
UEFI_FIRMWARE_OPT = "--uefi-firmware-path"
QEMU_GUI_OPT = "--qemu-enable-gui"

def __on_guess_failed(optname):
    raise FileNotFoundError(f"Failed to guess {optname}! "
                             "Please specify manually.")
    

def add_base_options(add_opt_cb):
    binaries_path = path_guesser.guess_path_to_kernel_binaries()
    if binaries_path is None:
        __on_guess_failed(KERNEL_DIR_OPT)

    installer_path = path_guesser.guess_path_to_installer()
    hyper_uefi_path = path_guesser.guess_path_to_hyper_uefi()
    hyper_iso_br_path = path_guesser.guess_path_to_hyper_iso_br()

    add_opt_cb(KERNEL_DIR_OPT, type=str,
               default=binaries_path,
               help="Path to the directory with kernel binaries")
    add_opt_cb(INTERM_DIR_OPT, type=str,
               default=path_guesser.guess_path_to_interm_dir(),
               help="Path to the intermediate data directory")
    add_opt_cb(INSTALLER_OPT, type=str,
               default=installer_path,
               help="Path to the hyper installer")
    add_opt_cb(HYPER_UEFI_OPT, type=str,
               default=hyper_uefi_path,
               help="Path to the hyper UEFI binary")
    add_opt_cb(HYPER_ISO_BR_OPT, type=str,
                default=hyper_iso_br_path,
                help="Path to the hyper ISO boot record")


def add_test_options(add_opt_cb):
    add_opt_cb(UEFI_FIRMWARE_OPT, type=str,
               default=path_guesser.guess_path_to_uefi_firmware(),
               help="Path to UEFI firmware")
    add_opt_cb("--qemu-enable-gui", action="store_true",
               help="Run QEMU with GUI")


def check_availability(get_opt_cb, hdd_only=False, gpt_only=False,
                       need_firmware=True):
    has_uefi = get_opt_cb(HYPER_UEFI_OPT) is not None
    if need_firmware:
        has_uefi = has_uefi and get_opt_cb(UEFI_FIRMWARE_OPT)

    has_bios = get_opt_cb(INSTALLER_OPT) is not None
    has_bios_iso = get_opt_cb(HYPER_ISO_BR_OPT) is not None

    if not has_uefi and (not has_bios or gpt_only) and \
      (not has_bios_iso or hdd_only):
        raise FileNotFoundError("Couldn't find any viable boot options! "
                                "Please specify paths manually.")

    return has_uefi, has_bios, has_bios_iso
