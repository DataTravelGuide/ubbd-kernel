UBBD_KERNEL_DIR := $(shell pwd)

mod:
	cd src; UBBD_HEADER_DIR="$(UBBD_KERNEL_DIR)/ubbd-headers" make mod

install:
	cd src/; make install

uninstall:
	cd src/; make uninstall
clean:
	cd src/; make clean
