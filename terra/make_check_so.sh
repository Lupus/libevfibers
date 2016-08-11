#!/bin/bash

set -e

mkdir check_so
pushd check_so
ar x /usr/lib/x86_64-linux-gnu/libcheck_pic.a
gcc -shared -Wl,--whole-archive *.o -Wl,--no-whole-archive -pthread -lcheck_pic -lrt -lm -o ../libcheck.so
popd
rm -rf check_so
