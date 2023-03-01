#!/bin/bash

set -e

VER=`cat VERSION`
KERNEL_VERSION=`uname -r`
ARCH=`dpkg --print-architecture`

./install_dep.sh

sed "s/@KERNEL_VERSION@/${KERNEL_VERSION}/g" debian/ubbd-kernel.install.in > debian/ubbd-kernel.install
sed "s/@KERNEL_VERSION@/${KERNEL_VERSION}/g" debian/changelog.in > debian/changelog
sed -i "s/@VERSION@/${VER}/g" debian/changelog
sed "s/@ARCH@/${ARCH}/g" debian/control.in > debian/control
git submodule update --init --recursive
dpkg-buildpackage -uc -us
