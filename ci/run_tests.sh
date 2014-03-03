#!/bin/bash

set -e

BUILD_TYPE=Debug
EIO="-DWANT_EIO=TRUE -DWANT_EMBEDDED_EIO=TRUE"

if [ -d build ] ; then
        rm -rf build
fi
mkdir build

pushd build

echo cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE $EIO ..
echo
cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE $EIO ..
echo

make

echo "======================================"
echo "         Running unit tests"
echo "======================================"
echo

./test/evfibers_test

popd

echo "Test run has finished successfully"
