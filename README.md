**Linux Kernel Module for UBBD (Userspace Backend Block Device)**

1. **To build and install ubbd-kernel**:

        a) build and install from git:  
            $ git clone https://github.com/DataTravelGuide/ubbd-kernel  
            $ ./build_and_install.sh
		
        b) install from archive:  
            $ bash -c "$(wget https://raw.githubusercontent.com/DataTravelGuide/ubbd/master/install-ubbd.sh -O -)"

2. **check ubbd module**  

        $ lsmod|grep ubbd

3. **build package**  

        a) for debian:  
            $ ./build_deb.sh
		
        b) for rpm:  
            $ ./build_rpm.sh
		
More information: https://github.com/DataTravelGuide/ubbd
