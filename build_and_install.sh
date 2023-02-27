#!/usr/bin/bash

set -e

ubbd_path=`pwd`

# install requirments
./install_dep.sh
git submodule update --init --recursive

# build
make mod

# install
make install

# post install
depmod -a
modprobe ubbd
