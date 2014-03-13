#!/bin/bash

set -e

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

export CK_TIMEOUT_MULTIPLIER=25
./test/evfibers_test

popd

echo "Test run has finished successfully"
