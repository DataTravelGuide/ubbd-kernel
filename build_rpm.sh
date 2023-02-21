#!/bin/bash

set -e

version=`cat VERSION`

./install_dep.sh
make dist
cp ubbd-kernel-${version}.tar.gz ~/rpmbuild/SOURCES/
cp rpm/ubbd-kernel.spec ~/rpmbuild/SPECS/
rpmbuild -ba ~/rpmbuild/SPECS/ubbd-kernel.spec
