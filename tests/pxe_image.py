#!/usr/bin/python3
"""
Hyper-specific wiring for a (simulated) PXE boot: assembles the normal boot
config plus the same module set the disk tests use, then hands it to the generic
image_utils TFTP root builder to lay out the directory QEMU serves.
"""
import os
import tempfile

import disk_image as di
from image_utils import ultra
from image_utils import pxe

# Mirror disk_image's default module set so the same config/kernel validation
# the disk tests do runs over PXE too.
_CC_MODULE_PATH = "/modules/cc-fill.bin"
_CC_MODULE_SIZE = 0x2000
_CC_MODULE_FILL = 0x1234
_MEMORY_MODULE_SIZE = 0x3000


def build_pxe_root(kernel_dir: str, kernel_type: str,
                   boot_image: str, boot_name: str) -> str:
    """
    Create a TFTP root directory and return its path. `boot_image` is copied in
    as `boot_name` (the file the firmware fetches: the BIOS PXE image, or the
    UEFI binary as BOOTX64.EFI); `kernel_type` selects the config entry.
    """
    files = {}
    for name in os.listdir(kernel_dir):
        if name.startswith("kernel_"):
            files[os.path.join("boot", name)] = os.path.join(kernel_dir, name)

    cc_module = tempfile.mkstemp()[1]
    with open(cc_module, "wb") as f:
        f.write(b"\xCC" * _CC_MODULE_SIZE)
    files[_CC_MODULE_PATH.lstrip("/")] = cc_module

    modules = [
        ultra.Module("__KERNEL__", True),
        ultra.Module("memory0", False, size=_MEMORY_MODULE_SIZE),
        ultra.Module("cc-fill", True, path=_CC_MODULE_PATH,
                     size=_CC_MODULE_FILL),
    ]
    config = di.make_normal_boot_config(kernel_type, kernel_type, modules)

    try:
        return pxe.build_tftp_root(boot_image, boot_name, config, files)
    finally:
        os.remove(cc_module)
