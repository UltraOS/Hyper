import shutil
import subprocess
import os
import tempfile
import pytest
import options
import disk_image as di
from image_utils import multipart as mp
import pxe_image as pxe
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
    # The kernel appends a non-ASCII result magic; don't choke decoding it
    print(stdout.decode("ascii", errors="replace"))


def run_qemu_command(qemu_args: List[str], config: str, timeout: int) -> bytes:
    with_gui = config.getoption(options.QEMU_GUI_OPT)
    if not with_gui:
        qemu_args.append("-nographic")

    qp = subprocess.Popen(qemu_args, stdout=subprocess.PIPE)
    try:
        qp.wait(timeout)
    except:
        print("Test timeout!")
        qp.kill()

        stdout, _ = qp.communicate()
        print_output(stdout)

        raise

    stdout, _ = qp.communicate()
    return stdout


def do_run_qemu(
    arch_postfix: str, args: List[str], disk_image: ultra.DiskImage,
    config: str, timeout: int
) -> bytes:
    qemu_args = [f"qemu-system-{arch_postfix}",
                 "-cdrom" if disk_image.is_cd() else "-hda",
                 disk_image.path]
    qemu_args.extend(args)

    return run_qemu_command(qemu_args, config, timeout)


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


def run_qemu_aarch64(
    disk_image: ultra.DiskImage, config: str, el2: bool = False
) -> bytes:
    # By default the loader is handed EL1. 'el2' boots at EL2 instead
    # (virtualization=on) to exercise the VHE handover path, which needs a
    # FEAT_VHE-capable core: cortex-a72 (the EL1 default) is ARMv8.0 and lacks
    # it, while -cpu max has hung EDK2 on boot, so use cortex-a76.
    if el2:
        machine, cpu = "virt,virtualization=on", "cortex-a76"
    else:
        machine, cpu = "virt", "cortex-a72"

    qemu_args = ["-M", machine, "-cpu", cpu, "-device", "ramfb",
                 "-bios", config.getoption(options.AA64_UEFI_FIRMWARE_OPT)]
    return do_run_qemu("aarch64", qemu_args, disk_image, config, 30)


def run_qemu_x86_pxe(
    tftp_dir: str, boot_name: str, is_uefi: bool, config: str
) -> bytes:
    # QEMU's user-mode networking is its own DHCP + TFTP server: it hands the
    # firmware `boot_name` off `tftp_dir`, and the loader then TFTPs the config,
    # kernel and modules from there too.
    qemu_args = ["qemu-system-x86_64",
                 "-netdev", f"user,id=n0,tftp={tftp_dir},bootfile={boot_name}",
                 "-boot", "n",
                 "-debugcon", "stdio", "-serial", "mon:null",
                 "-cpu", "qemu64,la57=on"]

    if is_uefi:
        firmware_path = config.getoption(options.X64_UEFI_FIRMWARE_OPT)
        if not firmware_path:
            pytest.skip("No UEFI firmware provided")

        qemu_args += [
            "-drive", f"file={firmware_path},if=pflash,format=raw,readonly=on",
            "-device", "virtio-net-pci,netdev=n0",
            # EDK2's UEFI network stack won't come up (so it never PXE boots)
            # without an entropy source to seed it.
            "-device", "virtio-rng-pci",
        ]
    else:
        # A NIC with a PXE option ROM; QEMU ships iPXE for rtl8139.
        qemu_args += ["-device", "rtl8139,netdev=n0"]

    return run_qemu_command(qemu_args, config, 30)


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


# Fragmented file regression tests.
#
# Freshly created images only ever produce contiguous files, which take the
# single-range fast path in the FAT driver, so none of the range bookkeeping for
# fragmented files is otherwise exercised. The image builder scatters a
# fill-validated module ("55-fill", see validate_modules in the test kernel)
# across the requested number of single-cluster fragments, with the holes between
# them holding 0xAA padding, so a lookup that resolves any part of the file to a
# wrong cluster is caught by the kernel-side fill check.
_FRAG_MODULE_NAME = "55-fill"
_FRAG_MODULE_FILL = 0x55
_FRAG_FILE_NAME = "frag.bin"

# Enough fragments that the range array spills out of the in-place capacity
# (~500 for FAT32) into the separately allocated one, covering that machinery
# too. FAT12/16 ranges are half the size with about double the in-place capacity;
# spilling those would need a subdirectory-based layout (their fixed root
# directory can't hold enough padding entries), so they only cover the in-place
# path.
_FRAG_COUNT_SPILL = 600


@pytest.fixture
def fragmented_disk_image(request, fs_root, default_modules):
    br_type, fs_type, kernel_type, fragments = request.param
    cfg = request.config

    options.check_availability(cfg.getoption)

    modules = default_modules + [
        ultra.Module(_FRAG_MODULE_NAME, True, path="/" + _FRAG_FILE_NAME),
    ]
    boot_cfg = di.make_normal_boot_config(kernel_type, kernel_type, modules)

    yield from di.build(
        cfg.getoption, br_type, fs_type, boot_cfg,
        fragmented_files=[(_FRAG_FILE_NAME, _FRAG_MODULE_FILL, fragments)]
    )


def fragmented_image_pretty_name(param):
    br_type, fs_type, kernel_type, fragments = param
    return f"{br_type}-{fs_type}-{kernel_type}-{fragments}-fragments"


@pytest.mark.parametrize(
    'fragmented_disk_image',
    (
        ("MBR", "FAT12", "i686_lower_half", 64),
        ("MBR", "FAT16", "amd64_lower_half", 128),
        ("MBR", "FAT32", "amd64_higher_half", _FRAG_COUNT_SPILL),
    ),
    indirect=True,
    ids=fragmented_image_pretty_name
)
@pytest.mark.bios
@pytest.mark.fat
@pytest.mark.hdd
def test_fragmented_file_bios(fragmented_disk_image: ultra.DiskImage, pytestconfig):
    res = run_qemu_x86(fragmented_disk_image, False, pytestconfig)
    check_qemu_run(res)


@pytest.mark.parametrize(
    'fragmented_disk_image',
    (
        ("GPT", "FAT32", "amd64_higher_half", _FRAG_COUNT_SPILL),
    ),
    indirect=True,
    ids=fragmented_image_pretty_name
)
@pytest.mark.uefi_x64
@pytest.mark.fat
@pytest.mark.hdd
def test_fragmented_file_uefi_x64(fragmented_disk_image: ultra.DiskImage,
                                  pytestconfig):
    res = run_qemu_x86(fragmented_disk_image, True, pytestconfig)
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
        # A higher-half kernel exercises the lower-half unmap after the VHE
        # transition; a lower-half one keeps the identity map.
        ("MBR", "FAT32", "aarch64_higher_half"),
        ("MBR", "FAT16", "aarch64_lower_half"),
    ),
    indirect=True,
    ids=disk_image_pretty_name
)
@pytest.mark.uefi_aarch64
@pytest.mark.fat
@pytest.mark.hdd
def test_uefi_aarch64_boot_el2(disk_image: ultra.DiskImage, pytestconfig):
    # Boot at EL2 so the VHE (HCR_EL2.{E2H,TGE}) handover path is exercised.
    res = run_qemu_aarch64(disk_image, pytestconfig, el2=True)
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
        ], GPT_DISK_GUID, installer_path=installer)
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
# A partition GUID is globally unique, so it can be addressed on its own with no
# disk prefix at all.
_GPT_PARTUUID_BARE = dict(
    layout="gpt",
    binary=_kernel_at(f"partuuid-{GPT_PART_GUIDS[1]}"),
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
    # GPT works under both firmwares now that the installer can house stage2 in
    # a synthesized BIOS boot partition. BIOS exercises the core addressing
    # modes (index, partuuid, diskuuid); the path-parsing variants (bare,
    # caseless) are firmware-independent and stay UEFI-only.
    pytest.param(_GPT_INDEX, False, marks=_BIOS_MARKS, id="gpt-index-bios"),
    pytest.param(_GPT_INDEX, True, marks=_UEFI_MARKS, id="gpt-index-uefi"),
    pytest.param(_GPT_PARTUUID, False, marks=_BIOS_MARKS, id="gpt-partuuid-bios"),
    pytest.param(_GPT_PARTUUID, True, marks=_UEFI_MARKS, id="gpt-partuuid-uefi"),
    pytest.param(_GPT_PARTUUID_BARE, True, marks=_UEFI_MARKS,
                 id="gpt-partuuid-bare-uefi"),
    pytest.param(_GPT_DISKUUID, False, marks=_BIOS_MARKS, id="gpt-diskuuid-bios"),
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


#
# Whole-disk (raw) addressing of a hybrid image.
#
# An ISO9660 image built with a UEFI ESP is a hybrid: it carries a GPT (the ESP
# plus the El Torito gap partitions) *and* a whole-disk ISO9660 filesystem. Now
# that fs_detect_all no longer short circuits, such a disk exposes both the GPT
# partition entries and a raw entry at once, so a raw selector has to resolve to
# the ISO9660 filesystem rather than trip over the partitions that share the
# disk. Load the kernel through an explicit whole-disk prefix and confirm the
# loader reports the origin as raw (partition 0, no GUIDs).
#
# The same image is addressed as cd0::/ when booted off a CD (-cdrom) and as
# hd0::/ when booted as a hard disk (-hda); the disk kind the loader assigns
# follows how the firmware handed it to us.
#
_RAW_KERNEL = "amd64_higher_half"


def _hybrid_raw_image(br_type: str, prefix: str):
    cfg = di.make_single_entry_config(
        f"{prefix}::/boot/kernel_{_RAW_KERNEL}",
        "part-type=raw disk-index=0 part-index=0")
    return (br_type, "ISO9660", cfg)


@pytest.mark.parametrize(
    "feature_image,firmware",
    (
        pytest.param(_hybrid_raw_image("CD", "cd0"), "bios",
                     marks=[pytest.mark.bios, pytest.mark.iso], id="cd0-bios"),
        pytest.param(_hybrid_raw_image("HDD", "hd0"), "bios",
                     marks=[pytest.mark.bios, pytest.mark.iso, pytest.mark.hdd],
                     id="hd0-bios"),
        pytest.param(_hybrid_raw_image("CD", "cd0"), "uefi_x64",
                     marks=[pytest.mark.uefi_x64, pytest.mark.iso], id="cd0-uefi"),
        pytest.param(_hybrid_raw_image("HDD", "hd0"), "uefi_x64",
                     marks=[pytest.mark.uefi_x64, pytest.mark.iso, pytest.mark.hdd],
                     id="hd0-uefi"),
    ),
    indirect=["feature_image"],
)
def test_hybrid_raw_addressing(feature_image: ultra.DiskImage, firmware,
                               pytestconfig):
    boot_and_check(feature_image, firmware, pytestconfig)


#
# Partition embedded inside a hybrid image.
#
# The counterpart to the raw test above: the very same hybrid also has to let
# the partitions that share the disk be addressed. The EFI system partition of a
# UEFI ISO lives inside the ISO9660 data yet is described by the GPT, so it is
# reachable as hd0-part1::/ (GPT slot 1) once the disk is scanned past its first
# matching scheme. With the old short-circuit the ISO9660 detection returned
# first, the GPT was never parsed, and this partition was completely invisible.
# Seed a kernel into the ESP and load it through the partition prefix to prove it
# resolves (and reports a gpt origin), which would fail outright before the fix.
#
# The ESP lands at GPT partition 2 (Gap0, ESP, Gap1); hyper numbers GPT slots
# from zero, so the loader sees it as partition 1.
_HYBRID_ESP_PART_NUM = 2
_HYBRID_ESP_PART_INDEX = 1


@pytest.mark.uefi_x64
@pytest.mark.iso
@pytest.mark.hdd
def test_hybrid_embedded_partition(pytestconfig):
    getopt = pytestconfig.getoption
    options.check_availability(getopt)
    if getopt(options.X64_HYPER_UEFI_OPT) is None:
        pytest.skip("no x64 UEFI loader to embed an ESP with")

    kernel_src = os.path.join(getopt(options.KERNEL_DIR_OPT),
                              f"kernel_{_RAW_KERNEL}")
    kernel_arc = f"boot/kernel_{_RAW_KERNEL}"
    cfg = di.make_single_entry_config(
        f"hd0-part{_HYBRID_ESP_PART_INDEX}::/{kernel_arc}",
        f"part-type=gpt disk-index=0 part-index={_HYBRID_ESP_PART_INDEX}")

    for image in di.build(getopt, "HDD", "ISO9660", cfg):
        mp.inject_into_esp(image.path, _HYBRID_ESP_PART_NUM,
                           {kernel_arc: kernel_src})
        boot_and_check(image, "uefi_x64", pytestconfig)


#
# Boot partition auto-selection.
#
# Unlike test_partition_addressing (which loads the kernel via an explicit
# hdN-partN prefix), these verify that when several partitions each carry a
# config the loader picks the one it was *booted* from as the origin. The config
# is byte-for-byte identical on every partition and loads the kernel through a
# leading '/' (origin-relative), so the partition the kernel reports being
# loaded from is whichever one the loader chose as the origin. Every config pins
# the same expected index on the command line, so a loader that fell back to the
# lowest-indexed partition instead would load the kernel from the wrong place
# and the kernel's own validation would fail.
#
# BIOS learns the index from the installer (--boot-partition), which bakes it
# into stage2; UEFI detects it from the loaded image device path, so there the
# loader lives only on the pinned partition and nothing else is bootable.
#
def _build_boot_partition_image(getopt, layout: str, boot_index: int,
                                is_uefi: bool):
    kernel_src = os.path.join(getopt(options.KERNEL_DIR_OPT),
                              f"kernel_{PART_KERNEL}")
    kernel_arc = f"boot/kernel_{PART_KERNEL}"

    if layout == "gpt":
        extra = (f" disk-guid={GPT_DISK_GUID} "
                 f"part-guid={GPT_PART_GUIDS[boot_index]}")
        part_type = "gpt"
    else:
        extra = ""
        part_type = "mbr"

    tmp = tempfile.mkdtemp()
    cfg_path = os.path.join(tmp, "hyper.cfg")
    with open(cfg_path, "w") as f:
        f.write(di.make_single_entry_config(
            f"/boot/kernel_{PART_KERNEL}",
            f"part-type={part_type} disk-index=0 part-index={boot_index}{extra}"))

    common = {"hyper.cfg": cfg_path, kernel_arc: kernel_src}

    def files_for(idx: int):
        files = dict(common)
        # The loader only goes on the pinned partition so the firmware has no
        # choice but to boot from it.
        if is_uefi and idx == boot_index:
            files["EFI/BOOT/BOOTX64.EFI"] = getopt(options.X64_HYPER_UEFI_OPT)
        return files

    img = os.path.join(tmp, "disk.img")
    installer = None if is_uefi else getopt(options.INSTALLER_OPT)

    if layout == "primaries":
        mp.build_mbr_image(
            img, [mp.Partition(files=files_for(i)) for i in range(3)],
            installer_path=installer,
            boot_partition=None if is_uefi else boot_index)
    elif layout == "gpt":
        mp.build_gpt_image(img, [
            mp.Partition(files=files_for(i), esp=(i == boot_index),
                         unique_guid=GPT_PART_GUIDS[i])
            for i in range(2)
        ], GPT_DISK_GUID, installer_path=installer,
           boot_partition=None if is_uefi else boot_index)
    else:
        raise RuntimeError(f"unknown layout {layout}")

    return tmp, img


_BOOT_PART_PARAMS = [
    # (layout, boot_index, is_uefi)
    pytest.param("primaries", 1, False, marks=_BIOS_MARKS,
                 id="boot-part-mbr-bios"),
    pytest.param("primaries", 1, True, marks=_UEFI_MARKS,
                 id="boot-part-mbr-uefi"),
    pytest.param("gpt", 1, False, marks=_BIOS_MARKS, id="boot-part-gpt-bios"),
    pytest.param("gpt", 1, True, marks=_UEFI_MARKS, id="boot-part-gpt-uefi"),
]


@pytest.mark.parametrize("layout,boot_index,is_uefi", _BOOT_PART_PARAMS)
def test_boot_partition_selection(layout, boot_index, is_uefi, pytestconfig):
    tmp, img = _build_boot_partition_image(pytestconfig.getoption, layout,
                                           boot_index, is_uefi)
    try:
        boot_and_check(_RawImage(img), "uefi_x64" if is_uefi else "bios",
                       pytestconfig)
    finally:
        shutil.rmtree(tmp)


#
# Network (PXE) boot.
#
# There is no disk at all: QEMU's user-mode networking acts as the DHCP + TFTP
# server, handing the firmware the boot image (the BIOS PXE blob, or the UEFI
# binary) and then serving the config, kernel and modules the loader TFTPs. The
# UEFI path needs a virtio-rng device or EDK2's network stack never comes up.
#
_PXE_BIOS_MARKS = [pytest.mark.bios, pytest.mark.pxe]
_PXE_UEFI_MARKS = [pytest.mark.uefi_x64, pytest.mark.pxe]

_PXE_PARAMS = [
    # (kernel_type, is_uefi)
    pytest.param("amd64_higher_half", False, marks=_PXE_BIOS_MARKS,
                 id="pxe-bios-amd64-higher"),
    pytest.param("i686_lower_half", False, marks=_PXE_BIOS_MARKS,
                 id="pxe-bios-i686-lower"),
    pytest.param("amd64_higher_half", True, marks=_PXE_UEFI_MARKS,
                 id="pxe-uefi-x64-amd64-higher"),
]


def _pxe_boot_image(getopt, is_uefi):
    """(source path, TFTP file name) of the image the firmware fetches."""
    if is_uefi:
        return getopt(options.X64_HYPER_UEFI_OPT), "BOOTX64.EFI"
    return getopt(options.HYPER_PXE_OPT), "hyper_pxe"


@pytest.mark.parametrize("kernel_type,is_uefi", _PXE_PARAMS)
def test_pxe_boot(kernel_type, is_uefi, pytestconfig):
    getopt = pytestconfig.getoption
    boot_image, boot_name = _pxe_boot_image(getopt, is_uefi)

    tftp = pxe.build_pxe_root(getopt(options.KERNEL_DIR_OPT), kernel_type,
                              boot_image, boot_name)
    try:
        res = run_qemu_x86_pxe(tftp, boot_name, is_uefi, pytestconfig)
        check_qemu_run(res)
    finally:
        shutil.rmtree(tftp)


#
# Huge command line test.
#
# The ultra protocol places no limit on the command line length and the loader
# allocates the storage dynamically, so make sure a command line far larger than
# any fixed-size buffer survives the trip to the kernel intact. The command line
# is "cmdline-check=<len> <filler>", where <filler> is a deterministic 'A'..'Z'
# pattern; the kernel validates both the total length and every filler byte (see
# validate_ki_expectations in tests/kernel/kernel.c).
#
# UEFI gets the truly huge (multi-MiB) case; BIOS uses a smaller-but-still-large
# size, as reading a multi-MiB config over slow INT13 disk I/O blows the tight
# BIOS boot timeout. Both are orders of magnitude past the old fixed 256-byte
# buffer and span multiple pages.
#
HUGE_CMDLINE_KERNEL = "amd64_higher_half"
BIOS_CMDLINE_LEN = 64 * 1024        # 64 KiB
UEFI_CMDLINE_LEN = 4 * 1024 * 1024  # 4 MiB


def _make_huge_cmdline(total_len: int) -> str:
    prefix = f"cmdline-check={total_len} "
    filler = bytes(ord('A') + (j % 26)
                   for j in range(total_len - len(prefix))).decode("ascii")
    cmdline = prefix + filler
    assert len(cmdline) == total_len
    return cmdline


def _huge_cmdline_image(br_type: str, fs_type: str, cmdline_len: int):
    cfg = di.make_single_entry_config(f"/boot/kernel_{HUGE_CMDLINE_KERNEL}",
                                      _make_huge_cmdline(cmdline_len))
    return (br_type, fs_type, cfg)


@pytest.mark.parametrize(
    "feature_image,firmware",
    (
        pytest.param(_huge_cmdline_image("CD", "ISO9660", BIOS_CMDLINE_LEN),
                     "bios", marks=[pytest.mark.bios, pytest.mark.iso],
                     id=f"CD-ISO9660-{BIOS_CMDLINE_LEN}"),
        pytest.param(_huge_cmdline_image("GPT", "FAT32", UEFI_CMDLINE_LEN),
                     "uefi_x64", marks=_UEFI_MARKS,
                     id=f"GPT-FAT32-{UEFI_CMDLINE_LEN}"),
    ),
    indirect=["feature_image"],
)
def test_huge_cmdline(feature_image: ultra.DiskImage, firmware, pytestconfig):
    boot_and_check(feature_image, firmware, pytestconfig)


#
# Module description test.
#
# The ultra_module_info_attribute carries an optional variable-length,
# NUL-terminated description after the fixed struct. Boot an image whose config
# gives some modules a description (and leaves one without), then let the kernel
# validate that each described module carries "desc: <name>" and that exactly the
# expected number of modules are described (see validate_modules in
# tests/kernel/kernel.c). This covers both the "has description" and the "no
# description" (header.size == sizeof(attr)) encodings in one go.
#


# Two described modules and one plain one, to exercise both the "has
# description" and the "no description" attribute encodings in one boot.
_MODULE_DESC_BLOCKS = (
    "module:\n"
    '    name = "described-a"\n'
    '    type = "memory"\n'
    "    size = 0x1000\n"
    '    description = "desc: described-a"\n'
    "module:\n"
    '    name = "plain"\n'
    '    type = "memory"\n'
    "    size = 0x1000\n"
    "module:\n"
    '    name = "described-b"\n'
    '    type = "memory"\n'
    "    size = 0x2000\n"
    '    description = "desc: described-b"\n'
)


def _module_desc_image(br_type: str, fs_type: str, kernel_type: str):
    # 'described-modules=2' tells the kernel how many modules must carry a
    # description, so a dropped description is caught instead of passing as an
    # absent one.
    cfg = di.make_single_entry_config(f"/boot/kernel_{kernel_type}",
                                      "described-modules=2",
                                      extra=_MODULE_DESC_BLOCKS)
    return (br_type, fs_type, cfg)


@pytest.mark.parametrize(
    "feature_image,firmware",
    (
        pytest.param(_module_desc_image("MBR", "FAT32", "amd64_higher_half"),
                     "bios", marks=_BIOS_MARKS, id="MBR-FAT32-amd64_higher_half"),
        pytest.param(_module_desc_image("GPT", "FAT32", "amd64_higher_half"),
                     "uefi_x64", marks=_UEFI_MARKS,
                     id="GPT-FAT32-amd64_higher_half"),
        pytest.param(_module_desc_image("MBR", "FAT32", "aarch64_higher_half"),
                     "uefi_aarch64", marks=_AA64_MARKS,
                     id="MBR-FAT32-aarch64_higher_half"),
    ),
    indirect=["feature_image"],
)
def test_module_description(feature_image: ultra.DiskImage, firmware,
                            pytestconfig):
    boot_and_check(feature_image, firmware, pytestconfig)


#
# UEFI info pass-through test.
#
# With `pass-uefi-info = true` the loader hands the kernel the EFI system table
# plus the raw firmware memory map (as returned by GetMemoryMap()) in a single
# ultra_uefi_info_attribute, captured right before ExitBootServices(). The kernel
# validates the attribute (descriptor version/size, a coherent map, the system
# table living in a runtime region) and cross-checks its presence against the
# `expect-uefi-info` command line token (see validate_uefi_info in
# tests/kernel/kernel.c). The BIOS case asks for it too, to confirm the loader
# quietly ignores the option on a platform that has no such info to pass.
#


def _uefi_info_image(br_type: str, fs_type: str, kernel_type: str, expect: int):
    cfg = di.make_single_entry_config(f"/boot/kernel_{kernel_type}",
                                      f"expect-uefi-info={expect}",
                                      extra="pass-uefi-info = true\n")
    return (br_type, fs_type, cfg)


@pytest.mark.parametrize(
    "feature_image,firmware",
    (
        pytest.param(_uefi_info_image("GPT", "FAT32", "amd64_higher_half", 1),
                     "uefi_x64", marks=_UEFI_MARKS,
                     id="GPT-FAT32-amd64_higher_half"),
        pytest.param(_uefi_info_image("MBR", "FAT32", "aarch64_higher_half", 1),
                     "uefi_aarch64", marks=_AA64_MARKS,
                     id="MBR-FAT32-aarch64_higher_half"),
        # BIOS asks for it too, to confirm the loader quietly ignores the option
        # on a platform that has no such info to pass.
        pytest.param(_uefi_info_image("MBR", "FAT32", "amd64_higher_half", 0),
                     "bios", marks=_BIOS_MARKS, id="MBR-FAT32-amd64_higher_half"),
    ),
    indirect=["feature_image"],
)
def test_uefi_info(feature_image: ultra.DiskImage, firmware, pytestconfig):
    boot_and_check(feature_image, firmware, pytestconfig)
