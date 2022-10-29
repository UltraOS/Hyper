#!/usr/bin/python3
import subprocess
import tempfile
import shutil
import os
import options
import platform

HYPER_ISO_BOOT_RECORD = "hyper_iso_boot"
FS_IMAGE_RELPATH = "boot"

normal_boot_cfgs = \
"""
default-entry = {default_entry}

[i686_lower_half]
protocol=ultra

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

cmdline = {cmdline}
binary:
    path = "/boot/kernel_amd64_higher_half"
    allocate-anywhere = true

video-mode:
    format = xrgb8888

higher-half-exclusive = true

{extra_cfg_entries}

[amd64_higher_half_5lvl]
protocol=ultra

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
"""

class UltraModule:
    format_string = \
"""
module:
    name = "{name}"
    type = "{type}"
    path = "{path}"
    size = "{size}"
"""

    def __init__(self, name, is_file, path=None, size=None):
        self.name = name
        self.is_file = is_file
        self.path = path
        self.size = size

    def __str__(self):
        if self.name == "__KERNEL__":
            return "kernel-as-module = true"

        type = "file" if self.is_file else "memory"
        path = self.path or ""
        size = self.size or "auto"

        return UltraModule.format_string.format(name=self.name, type=type,
                                                path=path, size=size)


def dd_mebibyte_postfix():
    if platform.system() == "Darwin":
        return "m"

    return "M"


def make_normal_boot_config(default_entry, cmdline, modules=[]):
    extra_cfg_entries = ""

    for module in modules:
        extra_cfg_entries += str(module)
        extra_cfg_entries += "\n"

    return normal_boot_cfgs.format(default_entry=default_entry,
                                   cmdline=cmdline,
                                   extra_cfg_entries=extra_cfg_entries)


def file_resize_to_mib(path, mib):
    subprocess.check_call(["dd", "if=/dev/zero", f"of={path}",
                           f"bs=1{dd_mebibyte_postfix()}", f"count={mib}"])


def linux_image_partition(path, br_type, fs_type, align_mib, part_len_mib):
    label = "gpt" if br_type == "GPT" else "msdos"
    subprocess.check_call(["parted", "-s", path, "mklabel", label])

    part_type = "primary" if br_type == "MBR" else "test-partition"

    fs_type = fs_type.lower()
    if fs_type == "fat12":
        fs_type = "fat16" # parted doesn't support fat12 labels

    subprocess.check_call(["parted", "-s", path,
                           "mkpart", part_type,
                           fs_type, f"{align_mib}M",
                           f"{align_mib + part_len_mib}M"])


def darwin_image_partition_gpt(path, fs_type, align_mib, part_len_mib):
    part_begin = (align_mib * 1024 * 1024) // 512
    part_end = part_begin + ((part_len_mib * 1024 * 1024) // 512)

    gdisk_fmt = "n\n" # New partition
    gdisk_fmt += "1\n" # At index 1
    gdisk_fmt += f"{part_begin}\n" # Starts here
    gdisk_fmt += f"{part_end}\n" # Ends here
    gdisk_fmt += "\n" # With default GUID (some Apple thing)
    gdisk_fmt += "w\n" # Write the new changes
    gdisk_fmt += "y\n" # Yes, overwrite everything

    gdp = subprocess.Popen(["gdisk", path], stdin=subprocess.PIPE)
    gdp.stdin.write(gdisk_fmt.encode("ascii"))
    gdp.stdin.close()

    gdp.wait(5)
    if gdp.returncode != 0:
        raise RuntimeError("gdisk exited with error")


# Darwin doesn't have 'parted', instead it ships with a weird version of 'fdisk'
def darwin_image_partition_mbr(path, fs_type, align_mib, part_len_mib):
    fs_type_to_id = {
        "FAT12": 0x01,
        "FAT16": 0x04,
        "FAT32": 0x0C
    }

    part_begin = (align_mib * 1024 * 1024) // 512
    part_len_mib = (part_len_mib * 1024 * 1024) // 512
    id = "{:02X}".format(fs_type_to_id[fs_type])

    fdisk_fmt = f"{part_begin},{part_len_mib},{id}\n"

    fdp = subprocess.Popen(["fdisk", "-yr", path], stdin=subprocess.PIPE)
    fdp.stdin.write(fdisk_fmt.encode("ascii"))
    fdp.stdin.close()

    fdp.wait(5)
    if fdp.returncode != 0:
        raise RuntimeError("fdisk exited with error")


def darwin_image_partition(path, br_type, fs_type, align_mib, part_len_mib):
    if br_type == "MBR":
        darwin_image_partition_mbr(path, fs_type, align_mib, part_len_mib)
    else:
        darwin_image_partition_gpt(path, fs_type, align_mib, part_len_mib)


SYSTEM_TO_IMAGE_PARTITION = {
    "Linux": linux_image_partition,
    "Darwin": darwin_image_partition,
}


def image_partition(path, br_type, fs_type, align_mib, part_len_mib):
    part_fn = SYSTEM_TO_IMAGE_PARTITION[platform.system()]
    part_fn(path, br_type, fs_type, align_mib, part_len_mib)


def get_fs_mib_size_for_type(fs_type):
    if fs_type == "FAT12":
        return 3

    if fs_type == "FAT16":
        return 32

    if fs_type == "FAT32":
        return 64

    raise RuntimeError(f"Unknown filesystem type {fs_type}")


def fat_recursive_copy(raw_fs_path, file):
    subprocess.check_call(["mcopy", "-Q", "-i", raw_fs_path, "-s", file, "::"])


def fat_fill(raw_fs_path, root_dir):
    for f in os.listdir(root_dir):
        full_path = os.path.abspath(os.path.join(root_dir, f))
        fat_recursive_copy(raw_fs_path, full_path)


def make_fat(raw_fs_path, size, force_fat32):
    cr_args = ["mformat", "-i", raw_fs_path]
    if force_fat32:
        cr_args.append("-F")

    subprocess.check_call(cr_args)


def make_iso(image_path, root_path, has_uefi, has_bios):
    # Make the disk itself
    xorriso_args = ["xorriso", "-as", "mkisofs"]

    bios_args = [
        "-b", f"{FS_IMAGE_RELPATH}/{HYPER_ISO_BOOT_RECORD}",
        "-no-emul-boot", "-boot-load-size", "4", "-boot-info-table"
    ]
    if has_bios:
        xorriso_args.extend(bios_args)

    uefi_args = [
        "--efi-boot", "efi_esp", "-efi-boot-part", "--efi-boot-image"
    ]
    if has_uefi:
        # Make the EFI ESP partition
        fat_image = os.path.join(root_path, "efi_esp")
        file_resize_to_mib(fat_image, 1)
        make_fat(fat_image, 1, False)
        fat_recursive_copy(fat_image, os.path.join(root_path, "EFI"))

        xorriso_args.extend(uefi_args)

    xorriso_args.extend(["--protective-msdos-label", root_path,
                         "-o", image_path])

    subprocess.check_call(xorriso_args)


def image_embed(image_path, mib_offset, fs_image):
    subprocess.check_call(["dd", f"if={fs_image}", f"seek={mib_offset}",
                           f"bs=1{dd_mebibyte_postfix()}", f"of={image_path}",
                           "conv=notrunc"])


def make_fs(image_path, fs_type, image_mib_offset, size, root_path,
            has_uefi, has_iso_br):
    if fs_type == "ISO9660":
        return make_iso(image_path, root_path, has_uefi, has_iso_br)

    with tempfile.NamedTemporaryFile() as tf:
        file_resize_to_mib(tf.name, size)

        if fs_type.startswith("FAT"):
            make_fat(tf.name, size, fs_type == "FAT32")
            fat_fill(tf.name, root_path)
        else:
            raise RuntimeError(f"Unknown filesystem type {fs_type}")

        image_embed(image_path, image_mib_offset, tf.name)


class DiskImage:
    # always align partitions at 1 MiB
    part_align_mibs = 1

    def __init__(self, fs_root_dir, br_type, fs_type, has_uefi, has_iso_br,
                 installer_path=None, out_path=None):
        self.__fs_root_dir = fs_root_dir
        self.__br_type = br_type
        self.__fs_type = fs_type
        self.__path = out_path if out_path else tempfile.mkstemp()[1]
        fs_size = 0

        is_iso = self.fs_type == "ISO9660"

        if not is_iso:
            fs_size = get_fs_mib_size_for_type(self.fs_type)
            image_size = fs_size + DiskImage.part_align_mibs

            # Make sure the backup header is intact
            if br_type == "GPT":
                image_size += 1

            file_resize_to_mib(self.__path, image_size)

            if self.__br_type == "MBR" or self.__br_type == "GPT":
                image_partition(self.__path, self.__br_type, self.__fs_type,
                                DiskImage.part_align_mibs, fs_size)

        make_fs(self.__path, self.__fs_type, DiskImage.part_align_mibs,
                fs_size, self.__fs_root_dir, has_uefi, has_iso_br)

        should_install = installer_path is not None and self.__br_type != "GPT"

        # Hybrid boot depends on having stage2 pointed to by el-torito
        if is_iso:
            should_install = should_install and has_iso_br

        if should_install:
            subprocess.check_call([installer_path, self.__path])

    @property
    def br_type(self):
        return self.__br_type

    def is_cd(self):
        return self.__br_type == "CD"

    @property
    def fs_type(self):
        return self.__fs_type

    @property
    def path(self):
        return self.__path

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        os.remove(self.__path)


def prepare_test_fs_root(opt_getter) -> str:
    uefi_path = opt_getter(options.HYPER_UEFI_OPT)
    iso_br_path = opt_getter(options.HYPER_ISO_BR_OPT)
    kernel_dir = opt_getter(options.KERNEL_DIR_OPT)
    test_dir = opt_getter(options.INTERM_DIR_OPT)

    root_dir = os.path.join(test_dir, FS_IMAGE_RELPATH)
    uefi_dir = os.path.join(test_dir, "EFI/BOOT")

    i686_lh_krnl = os.path.join(kernel_dir, "kernel_i686_lower_half")
    i686_hh_krnl = os.path.join(kernel_dir, "kernel_i686_higher_half")
    amd64_lh_krnl = os.path.join(kernel_dir, "kernel_amd64_lower_half")
    amd64_hh_krnl = os.path.join(kernel_dir, "kernel_amd64_higher_half")

    os.mkdir(test_dir)
    os.mkdir(root_dir)

    if uefi_path:
        os.makedirs(uefi_dir)
        shutil.copy(uefi_path, os.path.join(uefi_dir, "BOOTX64.EFI"))

    if iso_br_path:
        shutil.copy(iso_br_path, os.path.join(root_dir, HYPER_ISO_BOOT_RECORD))

    shutil.copy(i686_lh_krnl, root_dir)
    shutil.copy(i686_hh_krnl, root_dir)
    shutil.copy(amd64_lh_krnl, root_dir)
    shutil.copy(amd64_hh_krnl, root_dir)

    return test_dir


def fs_root_set_cfg(root_path, cfg):
    path = os.path.join(root_path, FS_IMAGE_RELPATH)
    path = os.path.join(path, "hyper.cfg")

    with open(path, "w") as hc:
        hc.write(cfg)
