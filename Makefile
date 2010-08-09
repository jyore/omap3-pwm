# cross-compile module makefile

ifneq ($(KERNELRELEASE),)
    obj-m := pwm.o pwmsp.o pwmsp_lib.o
else
    SUBDIRS := $(shell pwd)

default:
ifeq ($(strip $(KERNELDIR)),)
	$(error "KERNELDIR is undefined!")
else
	$(MAKE) -C $(KERNELDIR) M=$(SUBDIRS) modules 
endif


clean:
	rm -rf *~ *.ko *.o *.mod.c modules.order Module.symvers .pwm* .tmp_versions

endif


