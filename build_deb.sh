#!/bin/bash

set -e

VER=`cat VERSION`
arch=`dpkg --print-architecture`

./install_dep.sh

git submodule update --init --recursive

source /etc/os-release

sed "s/@ARCH@/${arch}/g" debian/control.in > debian/control
sed "s/stable/${VERSION_CODENAME}/g" debian/changelog.in > debian/changelog
sed -i "s/@VERSION@/${VER}/g" debian/changelog
sed -i "s/@CODENAME@/${VERSION_CODENAME}/g" debian/changelog
sed "s/@VERSION@/${VER}/g" debian/prerm.in > debian/prerm
sed "s/@VERSION@/${VER}/g" debian/prerm.in > debian/prerm
sed "s/@VERSION@/${VER}/g" debian/postinst.in > debian/postinst
sed "s/@VERSION@/${VER}/g" dkms.conf.in > dkms.conf
dpkg-buildpackage -uc -us
