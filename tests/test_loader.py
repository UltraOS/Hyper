import shutil
import subprocess
import os
import tempfile
import pytest
import options
import disk_image as di
from image_utils import multipart as mp
from typing import List
from image_utils import ultra


@pytest.fixture(scope="session")
def fs_root(request) -> str:
    cfg = request.config
    fs_root = cfg.getoption(options.INTERM_DIR_OPT)

    os.makedirs(fs_root, exist_ok=True)
    yield fs_root
    shutil.rmtree(fs_root)


@pytest.fixture(scope="session")
def default_modules(fs_root) -> List['ultra.Module']:
    modules_dir_name = "modules"
    modules_dir = os.path.join(fs_root, modules_dir_name)
    os.makedirs(modules_dir)

    cc_module_file_name = "cc-fill.bin"
    cc_fill_module = os.path.join(modules_dir, cc_module_file_name)
    cc_module_loader_path = os.path.join("/", modules_dir_name,
                                         cc_module_file_name)
    cc_module_size = 0x2000
    cc_module_fill_size = 0x1234

    with open(cc_fill_module, "wb") as f:
        f.write(b'\xCC' * cc_module_size)

    modules = [
        ultra.Module("__KERNEL__", True),
        ultra.Module("memory0", False, size=0x3000),
        ultra.Module("cc-fill", True, path=cc_module_loader_path,
                     size=cc_module_fill_size),
    ]

    return modules


@pytest.fixture
def disk_image(request, fs_root, default_modules):
    br_type, fs_type, kernel_type = request.param
    cfg = request.config

    options.check_availability(cfg.getoption)
    boot_cfg = di.make_normal_boot_config(kernel_type, kernel_type,
                                          default_modules)

    yield from di.build(cfg.getoption, br_type, fs_type, boot_cfg)


def disk_image_pretty_name(param):
    br_type, fs_type, kernel_type = param
    return f"{br_type}-{fs_type}-{kernel_type}"


def print_output(stdout):
    print("Kernel/loader output:")
    print(stdout.decode("ascii"))


def do_run_qemu(
    arch_postfix: str, args: List[str], disk_image: ultra.DiskImage,
    config: str, timeout: int
) -> bytes:
    qemu_args = [f"qemu-system-{arch_postfix}",
                 "-cdrom" if disk_image.is_cd() else "-hda",
                 disk_image.path]
    qemu_args.extend(args)

    with_gui = config.getoption(options.QEMU_GUI_OPT)
    if not with_gui:
        qemu_args.append("-nographic")

    qp = subprocess.Popen(qemu_args, stdout=subprocess.PIPE)
    try:
        ret = qp.wait(timeout)
    except:
        print("Test timeout!")
        qp.kill()

        stdout, _ = qp.communicate()
        print_output(stdout)

        raise

    stdout, _ = qp.communicate()
    return stdout


def run_qemu_x86(
    disk_image: ultra.DiskImage, is_uefi: bool, config: str
) -> bytes:
    qemu_args = ["-debugcon", "stdio", "-serial", "mon:null",
                 "-cpu", "qemu64,la57=on"]

    if is_uefi:
        firmware_path = config.getoption(options.X64_UEFI_FIRMWARE_OPT)
        if not firmware_path:
            pytest.skip("No UEFI firmware provided")

        drive_opts = f"file={firmware_path},if=pflash,format=raw,readonly=on"
        qemu_args.extend(["-drive", drive_opts])

    return do_run_qemu("x86_64", qemu_args, disk_image, config, 30 if is_uefi else 3)


def run_qemu_aarch64(disk_image: ultra.DiskImage, config: str) -> bytes:
    qemu_args = ["-M", "virt", "-cpu", "cortex-a72", "-device", "ramfb",
                 "-bios", config.getoption(options.AA64_UEFI_FIRMWARE_OPT)]
    return do_run_qemu("aarch64", qemu_args, disk_image, config, 30)


TEST_SUCCESS = b'\xCA\xFE\xBA\xBE'
TEST_FAIL    = b'\xDE\xAD\xBE\xEF'


def check_qemu_run(stdout: bytes) -> None:
    if len(stdout) < 4:
        raise RuntimeError(f"Invalid kernel output {stdout}")

    test_ret_part = len(stdout) - 4
    test_ret = stdout[test_ret_part:]
    kernel_log = stdout[:test_ret_part]

    print_output(kernel_log)

    if test_ret == TEST_SUCCESS:
        return

    if test_ret == TEST_FAIL:
        print(f"Test failed!")
    else:
        print(f"Invalid test result: {test_ret}")

    raise RuntimeError("Kernel reported an error")


def boot_and_check(disk_image, firmware: str, config) -> None:
    """
    Boot 'disk_image' under the given firmware ("bios", "uefi_x64" or
    "uefi_aarch64") and assert the kernel reported success. Collapses the
    per-test "run the right qemu, then check_qemu_run" boilerplate.
    """
    if firmware == "uefi_aarch64":
        res = run_qemu_aarch64(disk_image, config)
    else:
        res = run_qemu_x86(disk_image, firmware == "uefi_x64", config)

    check_qemu_run(res)


@pytest.fixture
def feature_image(request, fs_root):
    """
    Build a single-partition image from a pre-rendered (br_type, fs_type,
    config) param. Lets a feature test parametrize the whole boot config inline
    instead of each one growing its own near-identical fixture.
    """
    br_type, fs_type, boot_cfg = request.param
    cfg = request.config

    options.check_availability(cfg.getoption)
    yield from di.build(cfg.getoption, br_type, fs_type, boot_cfg)


@pytest.mark.parametrize(
    'disk_image',
    (
        ("MBR", "FAT12", "i686_lower_half"),
        ("MBR", "FAT12", "i686_lower_half_pae"),
        ("MBR", "FAT32", "amd64_higher_half"),
        ("MBR", "FAT32", "amd64_higher_half_5lvl"),
        ("MBR", "FAT16", "amd64_lower_half"),
        ("MBR", "FAT16", "amd64_lower_half_5lvl"),
        ("MBR", "FAT32", "i686_higher_half"),
        ("MBR", "FAT32", "i686_higher_half_pae"),
    ),
    indirect=True,
    ids=disk_image_pretty_name
)
@pytest.mark.bios
@pytest.mark.fat
@pytest.mark.hdd
def test_normal_bios_boot_fat(disk_image: ultra.DiskImage, pytestconfig):
    res = run_qemu_x86(disk_image, False, pytestconfig)
    check_qemu_run(res)


@pytest.mark.parametrize(
    'disk_image',
    (
        ("CD",  "ISO9660", "i686_lower_half"),
        ("CD",  "ISO9660", "i686_higher_half_pae"),
        ("CD",  "ISO9660", "amd64_lower_half"),
        ("CD",  "ISO9660", "amd64_higher_half_5lvl"),
    ),
    indirect=True,
    ids=disk_image_pretty_name
)
@pytest.mark.bios
@pytest.mark.iso
def test_normal_bios_boot_iso_cd(disk_image: ultra.DiskImage, pytestconfig):
    res = run_qemu_x86(disk_image, False, pytestconfig)
    check_qemu_run(res)


@pytest.mark.parametrize(
    'disk_image',
    (
        ("HDD", "ISO9660", "amd64_higher_half"),
        ("HDD", "ISO9660", "amd64_lower_half_5lvl"),
    ),
    indirect=True,
    ids=disk_image_pretty_name
)
@pytest.mark.bios
@pytest.mark.iso
@pytest.mark.hdd
def test_normal_bios_boot_iso_hdd(disk_image: ultra.DiskImage, pytestconfig):
    res = run_qemu_x86(disk_image, False, pytestconfig)
    check_qemu_run(res)


@pytest.mark.parametrize(
    'disk_image',
    (
        ("MBR", "FAT12", "i686_lower_half"),
        ("MBR", "FAT12", "i686_lower_half_pae"),
        ("MBR", "FAT32", "amd64_higher_half"),
        ("MBR", "FAT32", "amd64_higher_half_5lvl"),
        ("GPT", "FAT16", "amd64_lower_half"),
        ("GPT", "FAT16", "amd64_lower_half_5lvl"),
        ("GPT", "FAT32", "i686_higher_half"),
        ("GPT", "FAT32", "i686_higher_half_pae"),
    ),
    indirect=True,
    ids=disk_image_pretty_name
)
@pytest.mark.uefi_x64
@pytest.mark.fat
@pytest.mark.hdd
def test_normal_uefi_x64_boot_fat(disk_image: ultra.DiskImage, pytestconfig):
    res = run_qemu_x86(disk_image, True, pytestconfig)
    check_qemu_run(res)


@pytest.mark.parametrize(
    'disk_image',
    (
        ("CD",  "ISO9660", "i686_higher_half_pae"),
        ("HDD", "ISO9660", "amd64_higher_half"),
        ("HDD", "ISO9660", "amd64_lower_half_5lvl")
    ),
    indirect=True,
    ids=disk_image_pretty_name
)
@pytest.mark.uefi_x64
@pytest.mark.iso
def test_normal_uefi_x64_boot_iso(disk_image: ultra.DiskImage, pytestconfig):
    res = run_qemu_x86(disk_image, True, pytestconfig)
    check_qemu_run(res)

@pytest.mark.parametrize(
    'disk_image',
    (
        ("MBR", "FAT12", "aarch64_lower_half"),
        ("MBR", "FAT12", "aarch64_higher_half_5lvl"),
        ("MBR", "FAT16", "aarch64_lower_half_5lvl"),
        ("MBR", "FAT16", "aarch64_higher_half"),
        ("MBR", "FAT32", "aarch64_lower_half_5lvl"),
        ("MBR", "FAT32", "aarch64_higher_half"),
    ),
    indirect=True,
    ids=disk_image_pretty_name
)
@pytest.mark.uefi_aarch64
@pytest.mark.fat
@pytest.mark.hdd
def test_normal_uefi_aarch64_boot_fat(disk_image: ultra.DiskImage, pytestconfig):
    res = run_qemu_aarch64(disk_image, pytestconfig)
    check_qemu_run(res)


@pytest.mark.parametrize(
    'disk_image',
    (
        ("CD",  "ISO9660", "aarch64_higher_half"),
        ("CD",  "ISO9660", "aarch64_lower_half_5lvl"),
        ("HDD", "ISO9660", "aarch64_higher_half_5lvl"),
        ("HDD", "ISO9660", "aarch64_lower_half"),
    ),
    indirect=True,
    ids=disk_image_pretty_name
)
@pytest.mark.uefi_aarch64
@pytest.mark.iso
def test_normal_uefi_aarch64_boot_iso(
    disk_image: ultra.DiskImage, pytestconfig
):
    res = run_qemu_aarch64(disk_image, pytestconfig)
    check_qemu_run(res)


#
# Partition-addressing tests.
#
# These build multi-partition images and load the kernel from a specific
# partition addressed by a path prefix (hdN-partN, partuuid, diskuuid, ...).
# The expected disk/partition identity is passed through the command line and
# validated by the kernel against the ultra kernel-info attribute, which reports
# where the kernel binary was actually loaded from.
#
PART_KERNEL = "amd64_higher_half"

# Pinned GUIDs for the GPT images (lowercase is the canonical form).
GPT_DISK_GUID = "12345678-1234-1234-1234-1234567890ab"
GPT_PART_GUIDS = [
    "aabbccdd-eeff-0011-2233-445566778899",
    "99887766-5544-3322-1100-ffeeddccbbaa",
]


class _RawImage:
    """Minimal stand-in for ultra.DiskImage that run_qemu_x86 understands."""

    def __init__(self, path: str):
        self.path = path

    def is_cd(self) -> bool:
        return False


def _build_partition_image(getopt, scenario: dict, is_uefi: bool):
    kernel_src = os.path.join(getopt(options.KERNEL_DIR_OPT),
                              f"kernel_{PART_KERNEL}")

    tmp = tempfile.mkdtemp()
    cfg_path = os.path.join(tmp, "hyper.cfg")
    with open(cfg_path, "w") as f:
        f.write(di.make_single_entry_config(scenario["binary"],
                                            scenario["cmdline"]))

    kernel_arc = f"boot/kernel_{PART_KERNEL}"

    # Partition 0 is the boot partition: it holds the config (and, under UEFI,
    # the loader itself). Every partition carries a copy of the kernel so any of
    # them can be the addressing target.
    boot_files = {"hyper.cfg": cfg_path, kernel_arc: kernel_src}
    if is_uefi:
        boot_files["EFI/BOOT/BOOTX64.EFI"] = getopt(options.X64_HYPER_UEFI_OPT)
    other_files = {kernel_arc: kernel_src}

    img = os.path.join(tmp, "disk.img")
    installer = None if is_uefi else getopt(options.INSTALLER_OPT)
    layout = scenario["layout"]

    if layout == "primaries":
        mp.build_mbr_image(img, [
            mp.Partition(files=boot_files),
            mp.Partition(files=other_files),
            mp.Partition(files=other_files),
        ], installer_path=installer)
    elif layout == "ebr":
        mp.build_mbr_image(
            img, [mp.Partition(files=boot_files)],
            [mp.Partition(files=other_files),
             mp.Partition(files=other_files)],
            installer_path=installer)
    elif layout == "gpt":
        mp.build_gpt_image(img, [
            mp.Partition(files=boot_files, esp=True,
                         unique_guid=GPT_PART_GUIDS[0]),
            mp.Partition(files=other_files, unique_guid=GPT_PART_GUIDS[1]),
        ], GPT_DISK_GUID)
    else:
        raise RuntimeError(f"unknown layout {layout}")

    return tmp, img


def _kernel_at(prefix: str) -> str:
    return f"{prefix}::/boot/kernel_{PART_KERNEL}"


# Each scenario: how to lay out the image, the prefixed path used to load the
# kernel, and the disk/partition identity we expect the loader to report.
_MBR_INDEX = dict(
    layout="primaries",
    binary=_kernel_at("hd0-part2"),
    cmdline="part-type=mbr disk-index=0 part-index=2",
)
_EBR_LOGICAL = dict(
    layout="ebr",
    binary=_kernel_at("hd0-part5"),
    cmdline="part-type=mbr disk-index=0 part-index=5",
)
_GPT_INDEX = dict(
    layout="gpt",
    binary=_kernel_at("hd0-part1"),
    cmdline=(f"part-type=gpt disk-index=0 part-index=1 "
             f"disk-guid={GPT_DISK_GUID} part-guid={GPT_PART_GUIDS[1]}"),
)
_GPT_PARTUUID = dict(
    layout="gpt",
    binary=_kernel_at(f"hd0-partuuid-{GPT_PART_GUIDS[1]}"),
    cmdline=(f"part-type=gpt disk-index=0 part-index=1 "
             f"disk-guid={GPT_DISK_GUID} part-guid={GPT_PART_GUIDS[1]}"),
)
_GPT_DISKUUID = dict(
    layout="gpt",
    binary=_kernel_at(f"diskuuid-{GPT_DISK_GUID}-part0"),
    cmdline=(f"part-type=gpt disk-index=0 part-index=0 "
             f"disk-guid={GPT_DISK_GUID} part-guid={GPT_PART_GUIDS[0]}"),
)
# Mixed-case prefix, to guard the case-insensitive path parsing.
_GPT_CASELESS = dict(
    layout="gpt",
    binary=_kernel_at(f"Hd0-PartUUID-{GPT_PART_GUIDS[1].upper()}"),
    cmdline=(f"part-type=gpt disk-index=0 part-index=1 "
             f"disk-guid={GPT_DISK_GUID} part-guid={GPT_PART_GUIDS[1]}"),
)

_BIOS_MARKS = [pytest.mark.bios, pytest.mark.fat, pytest.mark.hdd]
_UEFI_MARKS = [pytest.mark.uefi_x64, pytest.mark.fat, pytest.mark.hdd]
_AA64_MARKS = [pytest.mark.uefi_aarch64, pytest.mark.fat, pytest.mark.hdd]

_PARTITION_PARAMS = [
    # MBR/EBR work under both BIOS and UEFI.
    pytest.param(_MBR_INDEX, False, marks=_BIOS_MARKS, id="mbr-index-bios"),
    pytest.param(_MBR_INDEX, True, marks=_UEFI_MARKS, id="mbr-index-uefi"),
    pytest.param(_EBR_LOGICAL, False, marks=_BIOS_MARKS, id="ebr-logical-bios"),
    pytest.param(_EBR_LOGICAL, True, marks=_UEFI_MARKS, id="ebr-logical-uefi"),
    # GPT can't be installed under BIOS (no gap in the protective MBR), so it's
    # UEFI-only.
    pytest.param(_GPT_INDEX, True, marks=_UEFI_MARKS, id="gpt-index-uefi"),
    pytest.param(_GPT_PARTUUID, True, marks=_UEFI_MARKS, id="gpt-partuuid-uefi"),
    pytest.param(_GPT_DISKUUID, True, marks=_UEFI_MARKS, id="gpt-diskuuid-uefi"),
    pytest.param(_GPT_CASELESS, True, marks=_UEFI_MARKS, id="gpt-caseless-uefi"),
]


@pytest.mark.parametrize("scenario,is_uefi", _PARTITION_PARAMS)
def test_partition_addressing(scenario, is_uefi, pytestconfig):
    tmp, img = _build_partition_image(pytestconfig.getoption, scenario, is_uefi)
    try:
        boot_and_check(_RawImage(img), "uefi_x64" if is_uefi else "bios",
                       pytestconfig)
    finally:
        shutil.rmtree(tmp)
