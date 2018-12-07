#
#  Configure:
#  source /opt/yocto/poky/1.3.2/environment-setup-core2-poky-linux
#
#  Compile:
#  KERNELDIR=/opt/yocto/poky-danny-8.0.2/build/tmp/work/cedartrail_nopvr-poky-linux/linux-yocto-3.0.32+git1+bf5ee4945ee6d748e6abe16356f2357f76b5e2f0_1+1e79e03d115ed177882ab53909a4f3555e434833-r4.1/linux-cedartrail-nopvr-standard-build make
#
#

ifeq ($(KERNELRELEASE),)

# Assume the source tree is where the running kernel was built
# You should set KERNELDIR in the environment if it's elsewhere
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
# The current directory is passed to sub-makes as argument
	PWD := $(shell pwd)

modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
	cp altera_driver.ko /opt/tftp

modules_install:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules_install

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions modules.order Module.symvers

.PHONY: modules modules_install clean

else
# called from kernel build system: just declare what our modules are
	obj-m := altera_driver.o 
endif
