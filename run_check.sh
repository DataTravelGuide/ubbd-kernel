#!/bin/bash
set -e

ubbd_path=`pwd`

git submodule update --init --recursive

# build
make mod

# install
make install
