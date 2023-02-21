#!/bin/bash

set -e

KERNEL_VERSION=`uname -r`

./install_dep.sh

sed "s/@KERNEL_VERSION@/${KERNEL_VERSION}/g" debian/ubbd-kernel.install.in > debian/ubbd-kernel.install
git submodule update --init --recursive
dpkg-buildpackage -uc -us
