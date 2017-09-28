XENO_CONFIG=/usr/xenomai/bin/xeno-config
prefix := $(shell $(XENO_CONFIG) --prefix)
ifeq ($(prefix),)
$(error Please add <xenomai-install-path>/bin to your PATH variable)
endif
CC=gcc -no-pie -fno-pie
PWD:= $(shell pwd)
KDIR := /lib/modules/$(shell uname -r)/build
STD_CFLAGS  := $(shell $(XENO_CONFIG) --skin=cobalt --skin=rtdm --cflags) -I. -g
STD_LDFLAGS := $(shell $(XENO_CONFIG) --skin=cobalt --skin=rtdm --ldflags) -g 
EXTRA_CFLAGS += $(shell $(XENO_CONFIG) --skin=cobalt --skin=rtdm --cflags)

EXTRA_CFLAGS += $(CFLAGS)

obj-m := hello.o hello_rt.o 

all: hello_rt.ko
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

hello_rt.ko: hello_rt.c

install:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules_install
	depmod -a

clean:
	rm -f *~ Module.markers Module.symvers modules.order
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) clean
