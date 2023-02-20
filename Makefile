UBBD_KERNEL_DIR := $(shell pwd)
VERSION ?= $(shell cat VERSION)
UBBD_KERNEL_VERSION ?= ubbd-kernel-$(VERSION)
KERNEL_VERSION := $(shell uname -r)

mod:
	cd src; UBBD_HEADER_DIR="$(UBBD_KERNEL_DIR)/ubbd-headers" make mod

install:
	cd src/; make install

uninstall:
	cd src/; make uninstall
clean:
	cd src/; make clean

dist:
	git submodule update --init --recursive
	sed "s/@VERSION@/$(VERSION)/g" rpm/ubbd-kernel.spec.in > rpm/ubbd-kernel.spec
	sed -i 's/@KVER@/$(KERNEL_VERSION)/g' rpm/ubbd-kernel.spec
	cd /tmp && mkdir -p $(UBBD_KERNEL_VERSION) && \
	cp -rf $(UBBD_KERNEL_DIR)/{ubbd-headers,Makefile,src} $(UBBD_KERNEL_VERSION) && \
	tar --format=posix -chf - $(UBBD_KERNEL_VERSION) | gzip -c > $(UBBD_KERNEL_DIR)/$(UBBD_KERNEL_VERSION).tar.gz && \
	rm -rf $(UBBD_KERNEL_VERSION)
