#!/bin/bash

source /etc/os-release
case "$ID" in
debian|ubuntu|devuan|elementary|softiron)
	echo "ubuntu"
	apt install -y linux-headers-$(uname -r) debhelper dpkg-dev
        ;;
rocky|centos|fedora|rhel|ol|virtuozzo)
	echo "centos"
	yum install -y kernel-devel rpm-build
        ;;
*)
        echo "$ID is unknown, dependencies will have to be installed manually."
        exit 1
        ;;
esac
