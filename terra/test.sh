#!/bin/bash
set -e
echo
echo "*** Running test suite with fork mode..."
echo
~/3rdparty/terra/terra -mg test.t

echo
echo "*** Running test suite without fork mode (CK_FORK=no)..."
echo
CK_FORK=no ~/3rdparty/terra/terra -mg test.t

echo
echo "*** Running test suite with valgrind..."
echo
CK_FORK=no valgrind --leak-check=full --log-file=valgrind.log ./run_tests
./valgrind-parse.sh valgrind.log

echo
echo "*** Running standalone error test without debug (-g)..."
echo
~/3rdparty/terra/terra tests/errors_standalone.t

echo
echo "*** Running standalone error test without debug (-g) with valgrind..."
echo
valgrind --leak-check=full --log-file=valgrind.log ./errors_standalone
./valgrind-parse.sh valgrind.log


echo
echo "*** Running standalone error test with debug (-g)..."
echo
~/3rdparty/terra/terra -g tests/errors_standalone.t

echo
echo "*** Running standalone error test with debug (-g) with valgrind..."
echo
valgrind --leak-check=full --log-file=valgrind.log ./errors_standalone
./valgrind-parse.sh valgrind.log
