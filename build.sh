rm -rf compat.h

make compat.h

UBBD_KMODS_DIR=`pwd` make -C /lib/modules/`uname -r`/build M=`pwd` modules V=0
