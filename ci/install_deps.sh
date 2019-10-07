#!/bin/bash

os=$(uname -s)
err=
if [[ "$os" == "Linux" ]] ; then
    sudo apt-get update -qq
    sudo apt-get install -y cmake libev-dev check cvs libtool autoconf
    err=$?
elif [[ "$os" == "Darwin" ]] ; then
    brew install cmake libev check cvs libtool autoconf
    # Ugly, but brew return 1 if package already installed
    #err=$?
    err=0
else
    echo "Unrecognized OS: $os" >&2
    err=1
fi

exit $err
