#!/usr/bin/python3

import argparse
import os
import subprocess
import build_utils.wsl_wrap as ww
import build_utils.package_manager as pm
import build_utils.toolchain_builder as tb
import build_utils.toolchain_args as ta


BASE_DEPS = {
    "apt": [
        "nasm",
        "cmake",
    ],
    "pacman": [
        "nasm",
        "cmake",
    ],
    "brew": [
        "nasm",
        "cmake",
    ],
}

TEST_DEPS = {
    "apt": [
        "parted",
        "python3-pytest",
        "clang",
        "lld",
        "xorriso",
        "mtools",
        "qemu-system-x86",
        "qemu-system-aarch64",
    ],
    "pacman": [
        "parted",
        "python-pytest",
        "clang",
        "lld",
        "xorriso",
        "mtools",
        "qemu-system-x86",
        "qemu-system-aarch64",
    ],
    "brew": [
        "llvm",
        "xorriso",
        "mtools",
        "gdisk",
        "qemu",
    ],
}


def get_project_root():
    return os.path.dirname(os.path.abspath(__file__))


def get_toolchain_dir():
    return os.path.join(get_project_root(), "toolchain")


def get_build_dir(arch, platform, toolchain):
    return os.path.join(get_project_root(),
                        f"build-{toolchain}-{arch}-{platform}")


def build_toolchain(args):
    toolchain_arch = args.arch

    if args.platform == "bios":
        toolchain_platform = "elf"
    elif args.platform == "uefi":
        toolchain_platform = "w64-mingw32"

        if toolchain_arch == "amd64":
            toolchain_arch = "x86_64"

    if not tb.is_supported_system():
        exit(1)

    tc_root_path = get_toolchain_dir()
    tools_dir = f"tools_{args.arch}_{args.platform}_{args.toolchain}"
    tc_platform_root_path = os.path.join(tc_root_path, tools_dir)

    tp = ta.params_from_args(args, toolchain_platform, tc_platform_root_path,
                             tc_root_path, toolchain_arch)

    if not args.skip_base_dependencies:
        pm.install_dependencies(BASE_DEPS)

    if args.fetch_test_dependencies:
        pm.install_dependencies(TEST_DEPS)

        # Brew doesn't have a pytest package
        if pm.get_package_manager().name == "brew":
            subprocess.check_call([
                "python3", "-m", "pip", "install", "pytest",
                "--break-system-packages"
            ])

    tb.build_toolchain(tp)


def make_hyper_option_arg(arg, setting):
    return f"-DHYPER_{arg}={setting.upper()}"


def build_hyper(args):
    build_dir = get_build_dir(args.arch, args.platform, args.toolchain)
    has_build_dir = os.path.isdir(build_dir)
    extra_cmake_args = []

    if args.e9_debug_log:
        opt = make_hyper_option_arg("E9_LOG", args.e9_debug_log)
        extra_cmake_args.append(opt)
    if args.serial_debug_log:
        opt = make_hyper_option_arg("SERIAL_LOG", args.serial_debug_log)
        extra_cmake_args.append(opt)
    if args.serial_debug_baud_rate:
        opt = make_hyper_option_arg("SERIAL_BAUD_RATE", args.serial_debug_baud_rate)
        extra_cmake_args.append(opt)
    if args.allocation_audit:
        opt = make_hyper_option_arg("ALLOCATION_AUDIT", args.allocation_audit)
        extra_cmake_args.append(opt)
    if args.strip_info_log:
        opt = make_hyper_option_arg("STRIP_INFO_LOG", args.strip_info_log)
        extra_cmake_args.append(opt)

    if args.reconfigure or extra_cmake_args or not has_build_dir:
        # Only rerun toolchain builder if reconfigure is not artificial
        if not has_build_dir:
            build_toolchain(args)

        os.makedirs(build_dir, exist_ok=True)
        cmake_args = [f"-DHYPER_ARCH={args.arch}",
                      f"-DHYPER_PLATFORM={args.platform}",
                      f"-DHYPER_TOOLCHAIN={args.toolchain}"]
        cmake_args.extend(extra_cmake_args)
        subprocess.run(["cmake", "..", *cmake_args], check=True, cwd=build_dir)
    else:
        print("Not rerunning cmake since build directory already exists "
              "(--reconfigure)")

    subprocess.run(["cmake", "--build", ".", "-j", str(os.cpu_count())],
                   cwd=build_dir, check=True)


def pick_arch_and_platform(arch, platform):
    if not arch and not platform:
        return ("i686", "bios")

    def invalid_arch_platform_combo():
        print(f"Error: {arch} is invalid with {platform}")
        exit(1)

    if arch and platform:
        if arch == "i686":
            if platform != "bios":
                invalid_arch_platform_combo()
        elif platform != "uefi":
            invalid_arch_platform_combo()

        return (arch, platform)

    if not arch:
        return ("i686" if platform == "bios" else "amd64", platform)

    return (arch, "bios" if arch == "i686" else "uefi")


def main():
    ww.relaunch_in_wsl_if_windows()

    parser = argparse.ArgumentParser("Build Hyper & compiler toolchains")
    ta.add_base_args(parser)

    parser.add_argument("--arch", help="architecture to build the tooolchain for",
                        choices=["i686", "amd64", "aarch64"])
    parser.add_argument("--platform", help="platform to build the toolchain for",
                        choices=["bios", "uefi"], type=str.lower)
    parser.add_argument("--skip-base-dependencies", action="store_true",
                        help="skip base dependencies")
    parser.add_argument("--fetch-test-dependencies", action="store_true",
                        help="also fetch packages used for running the loader tests")
    parser.add_argument("--reconfigure", action="store_true",
                        help="Reconfigure cmake before building")
    parser.add_argument("--e9-debug-log", choices=["on", "off"],
                        help="Enable/disable 0xE9 logging")
    parser.add_argument("--serial-debug-log", choices=["on", "off"],
                        help="Enable/disable serial logging")
    parser.add_argument("--serial-debug-baud-rate", type=int,
                        help="Sets the baud rate for serial debug logging")
    parser.add_argument("--allocation-audit", choices=["on", "off"],
                        help="Enable/disable dynamic allocation audit logging")
    parser.add_argument("--strip-info-log", choices=["on", "off"],
                        help="Enable/disable info-level log stripping")
    args = parser.parse_args()

    args.arch, args.platform = pick_arch_and_platform(args.arch, args.platform)

    build_hyper(args)


if __name__ == "__main__":
    main()
