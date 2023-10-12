import shutil
import subprocess
import os
import pytest
import options
import disk_image as di
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
    qemu_args = ["-M", "virt", "-cpu", "max",
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
