#!/bin/bash

os=$(uname -s)
err=
if [[ "$os" == "Linux" ]] ; then
    sudo apt-get install cmake libev-dev check cvs libtool autoconf
    err=$?
elif [[ "$os" == "Darwin" ]] ; then
    brew install cmake libev check cvs libtool autoconf
    err=$?
else
    echo "Unrecognized OS: $os" >&2
    err=1
fi

exit $err
