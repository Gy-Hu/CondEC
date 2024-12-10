#!/bin/bash

# aiger
VEXPPARSER_VERSION=9b243f2d93ed9d797b8064d54c863863980c1b2b

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

if [ ! -d "$DIR/aiger" ]; then
    git clone https://github.com/arminbiere/aiger
    cd aiger
    git checkout -f $VEXPPARSER_VERSION
    ./configure.sh && make
else
    echo "$DIR/aiger already exists. If you want to rebuild, please remove it manually."
fi

if [ -f $DIR/aiger/aiger.o ] ; then \
    echo "It appears vexpparser was successfully built in $DIR/aiger."
    echo "You may now build aiger with: ./configure.sh && make"
else
    echo "Building aiger failed."
    echo "You might be missing some dependencies."
    echo "Please see their github page for installation instructions: https://github.com/arminbiere/aiger"
    exit 1
fi
