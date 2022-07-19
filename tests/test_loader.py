import shutil
import subprocess
import pytest
import options
import disk_image as di
from disk_image import DiskImage


@pytest.fixture(scope="session")
def fs_root(request) -> str:
    cfg = request.config

    fs_root = di.prepare_test_fs_root(cfg.getoption)
    yield fs_root
    shutil.rmtree(fs_root)


@pytest.fixture
def disk_image(request, fs_root) -> DiskImage:
    br_type, fs_type, kernel_type = request.param
    cfg = request.config

    has_uefi, _, has_bios_iso = options.check_availability(cfg.getoption)
    installer_path = cfg.getoption(options.INSTALLER_OPT)

    cfg = di.make_normal_boot_config(kernel_type, kernel_type)
    di.fs_root_set_cfg(fs_root, cfg)

    with DiskImage(fs_root, br_type, fs_type, has_uefi, has_bios_iso,
                   installer_path) as d:
        yield d


def disk_image_pretty_name(param):
    br_type, fs_type, kernel_type = param
    return f"{br_type}-{fs_type}-{kernel_type}"


def print_output(stdout):
    print("Kernel output:")
    print(stdout.decode("ascii"))


def run_qemu(disk_image: DiskImage, is_uefi: bool, config):
    qemu_args = ["qemu-system-x86_64",
                 "-cdrom" if disk_image.is_cd() else "-hda",
                 disk_image.path, "-debugcon", "stdio",
                 "-serial", "mon:null"]

    with_gui = config.getoption(options.QEMU_GUI_OPT)
    if not with_gui:
        qemu_args.append("-nographic")

    if is_uefi:
        firmware_path = config.getoption(options.UEFI_FIRMWARE_OPT)
        if not firmware_path:
            pytest.skip("No UEFI firmware provided")

        drive_opts = f"file={firmware_path},if=pflash,format=raw,readonly=on"
        qemu_args.extend(["-drive", drive_opts])

    qp = subprocess.Popen(qemu_args, stdout=subprocess.PIPE)
    try:
        ret = qp.wait(30 if is_uefi else 3)
    except:
        print("Test timeout!")
        qp.kill()

        stdout, _ = qp.communicate()
        print_output(stdout)

        raise

    stdout, _ = qp.communicate()
    return stdout


TEST_SUCCESS = b'\xCA\xFE\xBA\xBE'
TEST_FAIL    = b'\xDE\xAD\xBE\xEF'

def check_qemu_run(stdout):
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
        ("MBR", "FAT12", "i386"),
        ("MBR", "FAT32", "amd64_higher_half"),
        ("GPT", "FAT16", "amd64_lower_half"),
        ("GPT", "FAT32", "i386"),
    ),
    indirect=True,
    ids=disk_image_pretty_name
)
@pytest.mark.bios
@pytest.mark.fat
@pytest.mark.hdd
def test_normal_bios_boot_fat(disk_image: DiskImage, pytestconfig):
    # TODO
    if disk_image.br_type == "GPT":
        pytest.skip("BIOS boot from GPT is unsupported")

    res = run_qemu(disk_image, False, pytestconfig)
    check_qemu_run(res)


@pytest.mark.parametrize(
    'disk_image',
    (
        ("CD",  "ISO9660", "i386"),
        ("CD",  "ISO9660", "amd64_higher_half"),
    ),
    indirect=True,
    ids=disk_image_pretty_name
)
@pytest.mark.bios
@pytest.mark.iso
def test_normal_bios_boot_iso_cd(disk_image: DiskImage, pytestconfig):
    res = run_qemu(disk_image, False, pytestconfig)
    check_qemu_run(res)


@pytest.mark.parametrize(
    'disk_image',
    (
        ("HDD", "ISO9660", "amd64_higher_half"),
        ("HDD", "ISO9660", "amd64_lower_half"),
    ),
    indirect=True,
    ids=disk_image_pretty_name
)
@pytest.mark.bios
@pytest.mark.iso
@pytest.mark.hdd
def test_normal_bios_boot_iso_hdd(disk_image: DiskImage, pytestconfig):
    res = run_qemu(disk_image, False, pytestconfig)
    check_qemu_run(res)


@pytest.mark.parametrize(
    'disk_image',
    (
        ("MBR", "FAT12",   "i386"),
        ("MBR", "FAT32",   "amd64_higher_half"),
        ("GPT", "FAT16",   "amd64_lower_half"),
        ("GPT", "FAT32",   "i386"),
    ),
    indirect=True,
    ids=disk_image_pretty_name
)
@pytest.mark.uefi
@pytest.mark.fat
@pytest.mark.hdd
def test_normal_uefi_boot_fat(disk_image: DiskImage, pytestconfig):
    res = run_qemu(disk_image, True, pytestconfig)
    check_qemu_run(res)


@pytest.mark.parametrize(
    'disk_image',
    (
        ("CD",  "ISO9660", "i386"),
        ("HDD", "ISO9660", "amd64_higher_half"),
        ("HDD", "ISO9660", "amd64_lower_half")
    ),
    indirect=True,
    ids=disk_image_pretty_name
)
@pytest.mark.uefi
@pytest.mark.iso
def test_normal_uefi_boot_iso(disk_image: DiskImage, pytestconfig):
    res = run_qemu(disk_image, True, pytestconfig)
    check_qemu_run(res)
