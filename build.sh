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

on_error()
{
    echo "Hyper build failed!"
    exit 1
}

platform="bios"

if [ "$1" ]
  then
    if [ $1 == "BIOS" ]; then
      platform="bios"
    elif [ $1 = "UEFI" ]; then
      platform="uefi"
    else
      echo "Unknown platform $1"
      on_error
    fi
fi

pushd $true_path

build_dir="build_$platform"

mkdir -p $build_dir || on_error

pushd $build_dir
cmake .. -DPLATFORM=$platform || on_error
cmake --build . -j$cores || on_error
popd

popd
