#!/usr/bin/python3
import shutil
import subprocess
import argparse
import platform
import urllib.request
import contextlib
import os

GCC_VERSION      = "11.2.0"
BINUTILS_VERSION = "2.37"

SUPPORTED_PACKAGE_MANGERS = [ "apt", "pacman", "brew" ]
SUPPORTED_SYSTEMS = [ "Linux", "Darwin" ]

# Needed to build on M1 and other aarch64 chips
GCC_DARWIN_PATCH = \
"""     
--- gcc/config/host-darwin.c
+++ gcc/config/host-darwin.c
@@ -22,6 +22,10 @@
 #include "coretypes.h"
 #include "diagnostic-core.h"
 #include "config/host-darwin.h"
+#include "hosthooks.h"
+#include "hosthooks-def.h"
+
+const struct host_hooks host_hooks = HOST_HOOKS_INITIALIZER;
 
 /* Yes, this is really supposed to work.  */
 /* This allows for a pagesize of 16384, which we have on Darwin20, but should
"""


def apply_patch(target_dir, patch):
    subprocess.check_output(["patch", "-p0"], input=patch, cwd=target_dir, text=True)


def get_compiler_prefix(platform) -> str:
    platform_to_prefix = {
        "uefi": "x86_64-w64-mingw32",
        "bios": "i686-elf"
    }
    
    return platform_to_prefix[platform]


def is_toolchain_built(tc_root, prefix) -> bool:
    full_path = os.path.join(tc_root, "bin", f"{prefix}-")

    # TODO: a more "reliable" check?
    return os.path.isfile(full_path + "gcc") and \
           os.path.isfile(full_path + "ld")


def get_package_manager(platform) -> str:
    if platform == "Darwin":
        return "brew"

    for pm in SUPPORTED_PACKAGE_MANGERS:
        ret = subprocess.run(["which", pm], stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL)
        if ret.returncode != 0:
            continue
        return pm

    raise RuntimeError("Couldn't detect a supported package manager")


def download_and_extract(url, target_file, target_dir, platform):
    if os.path.exists(target_dir):
        print(f"{target_dir} already exists")
        return False

    if not os.path.exists(target_file):
        print(f"Downloading {url}...")
        urllib.request.urlretrieve(url, target_file)
    else:
        print(f"{target_file} already exists, not downloading")

    os.mkdir(target_dir)

    command = ["tar", "-xf", target_file, "-C", target_dir, "--strip-components", "1"]
    if platform != "Darwin":
        command.append("--checkpoint=.250")

    print(f"Unpacking {target_file}...")
    subprocess.run(command, check=True)

    # line feed after tar printing '....' for progress
    print("")
    return True


def download_toolchain_sources(platform, workdir, gcc_target_dir, binutils_target_dir):
    gcc_url = f"ftp://ftp.gnu.org/gnu/gcc/gcc-{GCC_VERSION}/gcc-{GCC_VERSION}.tar.gz"
    binutils_url = f"https://ftp.gnu.org/gnu/binutils/binutils-{BINUTILS_VERSION}.tar.gz"

    full_gcc_tarball_path = os.path.join(workdir, "gcc.tar.gz")
    full_binutils_tarball_path = os.path.join(workdir, "binutils.tar.gz")

    is_new = download_and_extract(gcc_url, full_gcc_tarball_path, gcc_target_dir, platform)
    with contextlib.suppress(FileNotFoundError):
        os.remove(full_gcc_tarball_path)

    if is_new and platform == "Darwin":
        apply_patch(gcc_target_dir, GCC_DARWIN_PATCH)

    download_and_extract(binutils_url, full_binutils_tarball_path, binutils_target_dir, platform)
    with contextlib.suppress(FileNotFoundError):
        os.remove(full_binutils_tarball_path)


def pacman_is_dependency_installed(dependency):
    ret = subprocess.run(["pacman", "-Qs", dependency], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return ret.returncode == 0


def pacman_install(dependency):
    subprocess.run(["sudo", "pacman", "-Sy", dependency, "--noconfirm"], check=True)


def apt_is_dependency_installed(dependency):
    out = subprocess.check_output(["apt", "--installed", "list", dependency, "-qq"],
                                  stderr=subprocess.DEVNULL, text=True)

    # could be [installed] or [installed,...], maybe something else too?
    return "[installed" in out


def apt_install(dependency):
    subprocess.run(["sudo", "apt-get", "install", "-y", dependency], check=True)


def brew_is_dependency_installed(dependency):
    ret = subprocess.run(["brew", "list", dependency], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return ret.returncode == 0


def brew_install(dependency):
    subprocess.run(["brew", "install", dependency], check=True)


def brew_prefix(dependency):
    return subprocess.check_output(["brew", "--prefix", dependency], text=True).strip()


def do_fetch_dependencies(package_manager):
    pm_to_dependencies = {
        "apt": {
            "dependencies": [
                "build-essential",
                "bison",
                "flex",
                "libgmp-dev",
                "libmpc-dev",
                "libmpfr-dev",
                "texinfo",
                "libisl-dev",
                "nasm",
            ],
            "is_dependency_installed": apt_is_dependency_installed,
            "install": apt_install
        },
        "pacman": {
            "dependencies": [
                "base-devel",
                "gmp",
                "libmpc",
                "mpfr",
                "nasm",
            ],
            "is_dependency_installed": pacman_is_dependency_installed,
            "install": pacman_install
        },
        "brew": {
            "dependencies": [
                "coreutils",
                "bison",
                "flex",
                "gmp",
                "libmpc",
                "mpfr",
                "texinfo",
                "isl",
                "nasm",
            ],
            "is_dependency_installed": brew_is_dependency_installed,
            "install": brew_install
        }
    }

    do_install = pm_to_dependencies[package_manager]["install"]
    is_insalled = pm_to_dependencies[package_manager]["is_dependency_installed"]

    for dep in pm_to_dependencies[package_manager]["dependencies"]:
        if is_insalled(dep):
            print(f"{dep} is already installed")
            continue

        print(f"Installing {dep}...")
        do_install(dep)


def build_binutils(binutils_sources, binutils_target_dir, target, platform_root, env):
    configure_full_path = os.path.join(binutils_sources, "configure")

    print("Building binutils...")
    subprocess.run([configure_full_path,
                    f"--target={target}",
                    f"--prefix={platform_root}",
                    "--with-sysroot"
                    "--disable-nls",
                    "--disable-multilib",
                    "--disable-werror"
                   ], cwd=binutils_target_dir, env=env, check=True)
    subprocess.run(["make", "-j{}".format(os.cpu_count())], env=env,
                   cwd=binutils_target_dir, check=True)
    subprocess.run(["make", "install"], cwd=binutils_target_dir, check=True)


def build_gcc(gcc_sources, gcc_target_dir, this_platform, target_platform, platform_root, env):
    configure_full_path = os.path.join(gcc_sources, "configure")

    configure_command = [configure_full_path,
                         f"--target={target_platform}",
                         f"--prefix={platform_root}",
                         "--disable-nls",
                         "--enable-languages=c,c++",
                         "--disable-multilib"]

    if this_platform == "Darwin":
        configure_command.extend([
            f"--with-gmp={brew_prefix('gmp')}",
            f"--with-mpc={brew_prefix('libmpc')}",
            f"--with-mpfr={brew_prefix('mpfr')}"
        ])

    print("Building GCC...")
    subprocess.run(configure_command, cwd=gcc_target_dir, env=env, check=True)
    subprocess.run(["make", "all-gcc", "-j{}".format(os.cpu_count())],
                   cwd=gcc_target_dir, env=env, check=True)
    subprocess.run(["make", "install-gcc"], cwd=gcc_target_dir, env=env, check=True)


def build_libgcc(gcc_dir):
    subprocess.run(["make", "all-target-libgcc", "-j{}".format(os.cpu_count())],
                   cwd=gcc_dir, check=True)
    subprocess.run(["make", "install-target-libgcc"], cwd=gcc_dir, check=True)


def clone_mingw_w64(target_dir):
    if os.path.exists(target_dir):
        print("mingw-w64 already cloned, skipping")
        return

    print("Downloading mingw-w64...")
    subprocess.run(["git", "clone", "https://github.com/mingw-w64/mingw-w64",
                    target_dir], check=True)


def install_mingw_headers(source_dir, platform_dir, target_dir, env):
    mingw_headers_dir = os.path.join(target_dir, "mingw_headers")
    os.makedirs(mingw_headers_dir, exist_ok=True)
    configure_path = os.path.join(source_dir, "mingw-w64-headers", "configure")

    print("Installing mingw headers...")
    subprocess.run([configure_path, "--prefix={}".format(platform_dir)],
                   check=True, cwd=mingw_headers_dir, env=env)
    subprocess.run(["make", "install"], cwd=mingw_headers_dir, check=True, env=env)

    shutil.rmtree(mingw_headers_dir)


def install_mingw_libs(source_dir, target, platform_dir, target_dir, env):
    mingw_crt_dir = os.path.join(target_dir, "mingw_crt")
    os.makedirs(mingw_crt_dir, exist_ok=True)
    configure_path = os.path.join(source_dir, "mingw-w64-crt", "configure")

    print("Compiling mingw crt...")
    subprocess.run([configure_path,
                    f"--prefix={platform_dir}",
                    f"--host={target}",
                    f"--with-sysroot={platform_dir}",
                    "--enable-lib64", "--disable-lib32"], cwd=mingw_crt_dir, check=True, env=env)
    subprocess.run(["make", "-j{}".format(os.cpu_count())], cwd=mingw_crt_dir, check=True, env=env)
    subprocess.run(["make", "install"], cwd=mingw_crt_dir, check=True, env=env)

    shutil.rmtree(mingw_crt_dir)


def build_toolchain(gcc_sources, binutils_sources, target_dir, this_platform,
                    target_platform, keep_sources, keep_build_dirs, optimize_for_native):
    compiler_prefix = get_compiler_prefix(target_platform)
    binutils_build_dir = os.path.join(target_dir, f"binutils_{target_platform}_build")
    gcc_build_dir = os.path.join(target_dir, f"gcc_{target_platform}_build")
    is_mingw = target_platform == "uefi"

    if is_mingw:
        mingw_w64_dir = os.path.join(target_dir, "mingw-w64")
        mingw_target_dir = os.path.join(target_dir, compiler_prefix)
        os.makedirs(mingw_target_dir, exist_ok=True)
        clone_mingw_w64(mingw_w64_dir)

    env = os.environ.copy()

    cflags = ["-g", "-O2"]
    
    if optimize_for_native:
        # -march=native doesn't work on M1 clang for some reason
        if this_platform == "Darwin":
            cflags.append("-mtune=native")
        else:
            cflags.append("-march=native")

    env["CFLAGS"] = env.get("CFLAGS", "") + " ".join(cflags)
    env["CXXFLAGS"] = env.get("CXXFLAGS", "") + " ".join(cflags)
    env["PATH"] = os.path.join(target_dir, "bin") + ":" + env.get("PATH", "")

    print(f"Building the toolchain for {target_platform} (gcc for {compiler_prefix})...")

    os.makedirs(binutils_build_dir, exist_ok=True)
    build_binutils(binutils_sources, binutils_build_dir, compiler_prefix, target_dir, env)

    if is_mingw:
        install_mingw_headers(mingw_w64_dir, mingw_target_dir, target_dir, env)

    os.makedirs(gcc_build_dir, exist_ok=True)
    build_gcc(gcc_sources, gcc_build_dir, this_platform, compiler_prefix, target_dir, env)

    if is_mingw:
        install_mingw_libs(mingw_w64_dir, compiler_prefix, mingw_target_dir, target_dir, env)

    build_libgcc(gcc_build_dir)

    print(f"Toolchain for {target_platform} built succesfully!")

    if is_mingw and not keep_sources:
        shutil.rmtree(mingw_w64_dir)

    if not keep_build_dirs:
        print("Removing build directories...")
        shutil.rmtree(binutils_build_dir)
        shutil.rmtree(gcc_build_dir)


def fetch_dependencies(platform):
    pm = get_package_manager(platform)
    print(f"Detected package manager '{pm}'")
    do_fetch_dependencies(pm)

def main():
    parser = argparse.ArgumentParser("Build hyper toolchain")
    parser.add_argument("platform", help="platform to build the toolchain for (BIOS/UEFI)")
    parser.add_argument("--skip-dependencies", action="store_true",
                        help="don't attempt to fetch the dependencies")
    parser.add_argument("--keep-sources", action="store_true",
                        help="don't remove the sources after build")
    parser.add_argument("--keep-build", action="store_true",
                        help="don't remove the build directories")
    parser.add_argument("--force-fetch-dependencies", action="store_true",
                        help="fetch dependencies even if toolchain is already built")
    parser.add_argument("--no-tune-native", action="store_true",
                        help="don't optimize the toolchain for the current CPU")
    args = parser.parse_args()

    build_platform = args.platform.lower()
    if build_platform != "bios" and build_platform != "uefi":
        print(f"Unknown platform {build_platform}")
        exit(1)

    native_platform = platform.system()
    if native_platform not in SUPPORTED_SYSTEMS:
        print(f"Unsupported system '{native_platform}'")
        exit(1)

    if args.force_fetch_dependencies:
        fetch_dependencies(native_platform)

    tc_root_path = os.path.dirname(os.path.abspath(__file__))
    tc_platform_root_path = os.path.join(tc_root_path, f"tools_{build_platform}")

    if is_toolchain_built(tc_platform_root_path, get_compiler_prefix(build_platform)):
        print(f"Toolchain for {build_platform} is already built")
        return

    gcc_dir = "gcc_sources"
    binutils_dir = "binutils_sources"
    gcc_dir_full_path = os.path.join(tc_root_path, gcc_dir)
    binutils_dir_full_path = os.path.join(tc_root_path, binutils_dir)

    download_toolchain_sources(native_platform, tc_root_path, gcc_dir_full_path, binutils_dir_full_path)

    if not args.skip_dependencies and not args.force_fetch_dependencies:
        fetch_dependencies(native_platform)

    os.makedirs(tc_platform_root_path, exist_ok=True)
    build_toolchain(gcc_dir_full_path, binutils_dir_full_path, tc_platform_root_path,
                    native_platform, build_platform, args.keep_sources, args.keep_build,
                    not args.no_tune_native)

    if not args.keep_sources:
        print("Removing source directories...")
        shutil.rmtree(gcc_dir_full_path)
        shutil.rmtree(binutils_dir_full_path)

    print("Done!")

if __name__ == "__main__":
    main()
