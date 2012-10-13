#!/bin/bash

set -e

if [ -d coverage_build ] ; then
	rm -rf coverage_build
fi
mkdir coverage_build
pushd coverage_build
CC=/usr/bin/gcc CFLAGS="-fprofile-arcs -ftest-coverage" cmake -DCMAKE_BUILD_TYPE=Debug ..
make
lcov -c -i -d . -o evfibers_base.info
./test/evfibers_test
lcov -c -d . -o evfibers_test.info
lcov -a evfibers_base.info -a evfibers_test.info -o evfibers_total.info
mkdir html
genhtml -o html -b evfibers_base.info -t "libevfibers" --highlight evfibers_total.info
popd
xdg-open file://$(pwd)/coverage_build/html/index.html
