#!/bin/bash

if [[ "$OSTYPE" == "darwin"* ]]; then
  realpath() {
      [[ $1 = /* ]] && echo "$1" || echo "$PWD/${1#./}"
  }

  cores=$(sysctl -n hw.physicalcpu)
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
  cores=$(nproc)
fi

pushd () { command pushd "$@" > /dev/null ; }
popd () { command popd "$@" > /dev/null ; }

true_path="$(dirname "$(realpath "$0")")"
root_path=$true_path/..

on_error()
{
    echo "Cross-compiler build failed!"
    exit 1
}

platform="BIOS"

if [ "$1" ]
  then
    if [ $1 != "UEFI" ] && [ $1 != "BIOS" ]
    then
      echo "Unknown platform $1"
      on_error
    else
      platform="$1"
    fi
fi

compiler_prefix="i686-elf"

if [ $platform = "UEFI" ]
then
    compiler_prefix="mingw64"
fi

pushd $true_path

if [ -e "Tools$platform/bin/$compiler_prefix-g++" ]
then
  exit 0
else
  echo "Building the cross-compiler for $compiler_prefix ($platform)..."
fi

if [[ "$OSTYPE" == "linux-gnu"* ]]; then
  sudo apt update

  declare -a dependencies=(
              "bison"
              "flex"
              "libgmp-dev"
              "libmpc-dev"
              "libmpfr-dev"
              "texinfo"
              "libisl-dev"
              "build-essential"
          )
elif [[ "$OSTYPE" == "darwin"* ]]; then
  declare -a dependencies=(
              "bison"
              "flex"
              "gmp"
              "libmpc"
              "mpfr"
              "texinfo"
              "isl"
              "libmpc"
              "wget"
          )
fi

for dependency in "${dependencies[@]}"
do
   echo -n $dependency
   if [[ "$OSTYPE" == "linux-gnu"* ]]; then
      is_dependency_installed=$(dpkg-query -l | grep $dependency)
   elif [[ "$OSTYPE" == "darwin"* ]]; then
      is_dependency_installed=$(brew list $dependency)
   fi

   if [ -z "$is_dependency_installed" ]
    then
      echo " - not installed"
      if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        sudo apt install -y $dependency || on_error
      elif [[ "$OSTYPE" == "darwin"* ]]; then
        brew install $dependency || on_error
      fi
    else
      echo " - installed"
    fi
done

gcc_version="gcc-11.2.0"
binutils_version="binutils-2.37"
gcc_url="ftp://ftp.gnu.org/gnu/gcc/$gcc_version/$gcc_version.tar.gz"
binutils_url="https://ftp.gnu.org/gnu/binutils/$binutils_version.tar.gz"

gcc_sources_dir="gcc_sources"

if [ ! -e "gcc_sources/configure" ]
then
  echo "Downloading GCC source files..."
  mkdir -p $gcc_sources_dir || on_error
  wget -O "gcc.tar.gz" $gcc_url || on_error
  echo "Unpacking GCC source files..."
  tar -xf "gcc.tar.gz" \
      -C  "$gcc_sources_dir" --strip-components 1  || on_error
  rm "gcc.tar.gz"
else
  echo "GCC is already downloaded!"
fi

binutils_sources_dir="binutils_sources"

if [ ! -e "$binutils_sources_dir/configure" ]
then
  echo "Downloading binutils source files..."
  mkdir -p  $binutils_sources_dir || on_error
  wget -O "binutils.tar.gz" $binutils_url || on_error
  echo "Unpacking binutils source files..."
  tar -xf "binutils.tar.gz" \
      -C $binutils_sources_dir --strip-components 1 || on_error
  rm "binutils.tar.gz"
else
  echo "binutils is already downloaded!"
fi

export PREFIX="$true_path/Tools$platform"
export TARGET="$compiler_prefix"
export PATH="$PREFIX/bin:$PATH"

# Build with optimizations and no debug information
export CFLAGS="-g0 -O2"
export CXXFLAGS="-g0 -O2"

binutils_build_dir="binutils_build_$platform"

echo "Building binutils..."
mkdir -p $binutils_build_dir || on_error

pushd $binutils_build_dir
../$binutils_sources_dir/configure --target=$TARGET \
                                   --prefix="$PREFIX" \
                                   --with-sysroot \
                                   --disable-nls \
                                   --disable-werror || on_error
make         -j$cores || on_error
make install          || on_error
popd

rm -rf $binutils_sources_dir
rm -rf $binutils_build_dir

gcc_build_dir="gcc_build_$platform"

echo "Building GCC..."
mkdir -p $gcc_build_dir || on_error
pushd $gcc_build_dir

if [[ "$OSTYPE" == "linux-gnu"* ]]; then
  ../$gcc_sources_dir/configure --target=$TARGET \
                                --prefix="$PREFIX" \
                                --disable-nls \
                                --enable-languages=c,c++ \
                                --without-headers || on_error
elif [[ "$OSTYPE" == "darwin"* ]]; then
  ../$gcc_sources_dir/configure --target=$TARGET \
                                --prefix="$PREFIX" \
                                --disable-nls \
                                --enable-languages=c,c++ \
                                --without-headers \
                                --with-gmp=/usr/local/opt/gmp \
                                --with-mpc=/usr/local/opt/libmpc \
                                --with-mpfr=/usr/local/opt/mpfr || on_error
fi

make all-gcc               -j$cores || on_error
make install-gcc                    || on_error

if [ $platform = "BIOS" ]
then
  make all-target-libgcc     -j$cores || on_error
  make install-target-libgcc          || on_error
fi

popd

rm -rf $gcc_sources_dir
rm -rf $gcc_build_dir

popd # true_path

echo "Cross-compiler built successfully!"
