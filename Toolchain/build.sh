#!/bin/bash

if [[ "$OSTYPE" == "darwin"* ]]; then
  realpath() {
      [[ $1 = /* ]] && echo "$1" || echo "$PWD/${1#./}"
  }

  cores=$(sysctl -n hw.physicalcpu)
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
  cores=$(nproc)
fi

pushd () {
    command pushd "$@" > /dev/null
}

popd () {
    command popd "$@" > /dev/null
}

true_path="$(dirname "$(realpath "$0")")"
root_path=$true_path/..

on_error()
{
    echo "Cross-compiler build failed!"
    exit 1
}

compiler_prefix="i686"

pushd $true_path

if [ -e "CrossCompiler/Tools/bin/$compiler_prefix-elf-g++" ]
then
  exit 0
else
  echo "Building the cross-compiler for $compiler_prefix..."
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

if [ ! -e "CrossCompiler/gcc/configure" ]
then
  echo "Downloading GCC source files..."
  mkdir -p "CrossCompiler"     || on_error
  mkdir -p "CrossCompiler/gcc" || on_error
  wget -O "CrossCompiler/gcc.tar.gz" $gcc_url || on_error
  echo "Unpacking GCC source files..."
  tar -xf "CrossCompiler/gcc.tar.gz" \
      -C  "CrossCompiler/gcc" --strip-components 1  || on_error
  rm "CrossCompiler/gcc.tar.gz"
else
  echo "GCC is already downloaded!"
fi

if [ ! -e "CrossCompiler/binutils/configure" ]
then
  echo "Downloading binutils source files..."
  mkdir -p  "CrossCompiler"          || on_error
  mkdir -p  "CrossCompiler/binutils" || on_error
  wget -O "CrossCompiler/binutils.tar.gz" $binutils_url || on_error
  echo "Unpacking binutils source files..."
  tar -xf "CrossCompiler/binutils.tar.gz" \
      -C "CrossCompiler/binutils" --strip-components 1 || on_error
  rm "CrossCompiler/binutils.tar.gz"
else
  echo "binutils is already downloaded!"
fi

export PREFIX="$true_path/CrossCompiler/Tools"
export TARGET=$compiler_prefix-elf
export PATH="$PREFIX/bin:$PATH"

# Build with optimizations and no debug information
export CFLAGS="-g0 -O2"
export CXXFLAGS="-g0 -O2"

echo "Building binutils..."
mkdir -p "CrossCompiler/binutils_build" || on_error

pushd "CrossCompiler/binutils_build"
../binutils/configure --target=$TARGET \
                      --prefix="$PREFIX" \
                      --with-sysroot \
                      --disable-nls \
                      --disable-werror || on_error
make         -j$cores || on_error
make install          || on_error
popd

echo "Building GCC..."
mkdir -p "CrossCompiler/gcc_build" || on_error
pushd "CrossCompiler/gcc_build"

if [[ "$OSTYPE" == "linux-gnu"* ]]; then
  ../gcc/configure --target=$TARGET \
                   --prefix="$PREFIX" \
                   --disable-nls \
                   --enable-languages=c,c++ \
                   --without-headers || on_error
elif [[ "$OSTYPE" == "darwin"* ]]; then
  ../gcc/configure --target=$TARGET \
                   --prefix="$PREFIX" \
                   --disable-nls \
                   --enable-languages=c,c++ \
                   --without-headers \
                   --with-gmp=/usr/local/opt/gmp \
                   --with-mpc=/usr/local/opt/libmpc \
                   --with-mpfr=/usr/local/opt/mpfr || on_error
fi

make all-gcc               -j$cores || on_error
make all-target-libgcc     -j$cores || on_error
make install-gcc                    || on_error
make install-target-libgcc          || on_error
popd

popd # true_path

echo "Cross-compiler built successfully!"
