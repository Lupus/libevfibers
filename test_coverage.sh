#!/bin/bash

set -e

function remove_extra {
	lcov -q --remove $1 "coro/*" -o $1
	lcov -q --remove $1 "bench/*" -o $1
	lcov -q --remove $1 "build/CMakeFiles/*" -o $1
	lcov -q --remove $1 "/usr/include/*" -o $1
}

./build.sh +eio cov

pushd build
lcov -c -i -d . -o evfibers_base.info
remove_extra evfibers_base.info
make test
lcov -c -d . -o evfibers_test.info
remove_extra evfibers_test.info
lcov -a evfibers_base.info -a evfibers_test.info -o evfibers_total.info
mkdir html
genhtml -o html -b evfibers_base.info -t "libevfibers" --highlight evfibers_total.info
popd
xdg-open file://$(pwd)/build/html/index.html
