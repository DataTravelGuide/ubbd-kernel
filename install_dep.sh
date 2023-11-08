#!/bin/bash

if [ "$UBBD_SKIP_INSTALL_DEP" == "yes" ]; then
	exit 0
fi

source /etc/os-release
case "$ID" in
debian|ubuntu|devuan|elementary|softiron)
	echo "ubuntu"
	apt install -y linux-headers-$(uname -r) debhelper dpkg-dev dkms
        ;;
rocky|centos|fedora|rhel|ol|virtuozzo)
	echo "centos"
	yum install -y kernel-devel rpm-build dkms
        ;;
*)
        echo "$ID is unknown, dependencies will have to be installed manually."
        exit 1
        ;;
esac
