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
    ],
    "pacman": [
        "nasm",
    ],
    "brew": [
        "nasm",
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
    ],
    "pacman": [
        "parted",
        "python-pytest",
        "clang",
        "lld",
        "xorriso",
        "mtools",
        "qemu-system-x86",
    ],
    "brew": [
        "llvm",
        "xorriso",
        "mtools",
        "qemu",
    ],
}


def get_project_root():
    return os.path.dirname(os.path.abspath(__file__))


def get_toolchain_dir():
    return os.path.join(get_project_root(), "toolchain")


def get_build_dir(platform, toolchain):
    return os.path.join(get_project_root(), f"build-{toolchain}-{platform}")


def build_toolchain(args):
    build_platform = args.platform.lower()

    if build_platform == "bios":
        toolchain_platform = "elf"
        toolchain_arch = "i686"
    elif build_platform == "uefi":
        toolchain_platform = "w64-mingw32"
        toolchain_arch = "x86_64"
    else:
        print(f"Unknown platform {build_platform}")
        exit(1)

    if not tb.is_supported_system():
        exit(1)

    tc_root_path = get_toolchain_dir()
    tc_platform_root_path = os.path.join(tc_root_path, f"tools_{build_platform}")

    tp = ta.params_from_args(args, toolchain_platform, tc_platform_root_path,
                             tc_root_path, toolchain_arch)

    if not args.skip_base_dependencies:
        pm.install_dependencies(BASE_DEPS)

    if args.fetch_test_dependencies:
        pm.install_dependencies(TEST_DEPS)

        # Brew doesn't have a pytest package
        if pm.get_package_manager().name == "brew":
            subprocess.check_call(["python3", "-m", "pip", "install", "pytest"])

    tb.build_toolchain(tp)


def build_hyper(args):
    build_dir = get_build_dir(args.platform, args.toolchain)
    rerun_cmake = args.reconfigure or not os.path.isdir(build_dir)

    if rerun_cmake:
        # Only rerun toolchain builder if reconfigure is not artificial
        if not args.reconfigure:
            build_toolchain(args)

        os.makedirs(build_dir, exist_ok=True)
        cmake_args = [f"-DHYPER_PLATFORM={args.platform}",
                      f"-DHYPER_TOOLCHAIN={args.toolchain}"]
        subprocess.run(["cmake", "..", *cmake_args], check=True, cwd=build_dir)
    else:
        print("Not rerunning cmake since build directory already exists "
              "(--reconfigure)")

    subprocess.run(["cmake", "--build", ".", "-j", str(os.cpu_count())],
                   cwd=build_dir, check=True)


def main():
    ww.relaunch_in_wsl_if_windows()

    parser = argparse.ArgumentParser("Build Hyper & compiler toolchains")
    ta.add_base_args(parser)

    parser.add_argument("platform", help="platform to build the toolchain for (BIOS/UEFI)")
    parser.add_argument("--skip-base-dependencies", action="store_true",
                        help="skip base dependencies")
    parser.add_argument("--fetch-test-dependencies", action="store_true",
                        help="also fetch packages used for running the loader tests")
    parser.add_argument("--reconfigure", action="store_true",
                        help="Reconfigure cmake before building")
    args = parser.parse_args()

    build_hyper(args)


if __name__ == "__main__":
    main()
