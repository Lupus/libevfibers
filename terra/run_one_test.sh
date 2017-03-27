#!/bin/bash
echo
echo "*** Running test suite without fork mode (CK_FORK=no)..."
echo
#CK_FORK=no gdb --args ~/3rdparty/terra/terra -mg test.t
CK_FORK=no ~/3rdparty/terra/terra -g test.t

if [ -z "$1" ] ; then
	echo
	echo "*** Running test suite with valgrind..."
	echo
	CK_FORK=no valgrind --leak-check=full --log-file=valgrind.log ./run_tests
	./valgrind-parse.sh valgrind.log
fi
