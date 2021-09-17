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

pushd $true_path

build_dir="build"

mkdir -p $build_dir || on_error

pushd $build_dir
cmake .. || on_error
cmake --build . -j$cores || on_error
popd

popd
