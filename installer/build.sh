#!/bin/bash

set -e

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

if [ -z "$1" ] || [ -z "$2" ]
then
    echo "Usage: $0 <mbr-path> <stage2-path>"
    exit 1
fi

cmake_args="-DMBR_PATH=$1 -DSTAGE2_PATH=$2"

pushd $true_path

mkdir -p build
pushd build
cmake .. $cmake_args
cmake --build .
popd

rm -rf build

popd