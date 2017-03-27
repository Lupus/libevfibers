#!/bin/bash
echo
echo "*** Running Lua test suite..."
echo
#gdb --args ~/3rdparty/terra/terra -g ./lua_tests.t
~/3rdparty/terra/terra -g ./lua_tests.t
