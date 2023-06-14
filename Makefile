UBBD_KERNEL_DIR := $(shell pwd)
VERSION ?= $(shell cat VERSION)
UBBD_KERNEL_VERSION ?= ubbd-kernel-dkms-$(VERSION)
KERNEL_VERSION ?= $(shell uname -r)
DIST_FILES := ubbd-headers Makefile src VERSION install_dep.sh debian rpm dkms.conf.in

mod:
	cd src; UBBD_HEADER_DIR="$(UBBD_KERNEL_DIR)/ubbd-headers" make mod

install:
	cd src/; make install

files_install:
	mkdir -p $(DESTDIR)/lib/modules/$(KERNEL_VERSION)/extra/
	install src/ubbd.ko $(DESTDIR)/lib/modules/$(KERNEL_VERSION)/extra/ubbd.ko

uninstall:
	cd src/; make uninstall
clean:
	cd src/; make clean

dist:
	git submodule update --init --recursive
	sed "s/@VERSION@/$(VERSION)/g" rpm/ubbd-kernel.spec.in > rpm/ubbd-kernel.spec
	sed -i 's/@KVER@/$(KERNEL_VERSION)/g' rpm/ubbd-kernel.spec
	cd /tmp && mkdir -p $(UBBD_KERNEL_VERSION) && \
	for u in $(DIST_FILES); do cp -rf $(UBBD_KERNEL_DIR)/$$u $(UBBD_KERNEL_VERSION); done && \
	tar --format=posix -chf - $(UBBD_KERNEL_VERSION) | gzip -c > $(UBBD_KERNEL_DIR)/$(UBBD_KERNEL_VERSION).tar.gz && \
	rm -rf $(UBBD_KERNEL_VERSION)
