XENO_CONFIG=/usr/xenomai/bin/xeno-config

XENOMAI_SKIN=native
prefix := $(shell $(XENO_CONFIG) --prefix)
ifeq ($(prefix),)
$(error Please add <xenomai-install-path>/bin to your PATH variable)
endif
CC=gcc -no-pie -fno-pie
PWD:= $(shell pwd)
KDIR := /lib/modules/$(shell uname -r)/build

STD_CFLAGS  := $(shell $(XENO_CONFIG) --skin=$(XENOMAI_SKIN) --skin=rtdm --cflags) -I. -g -DXENOMAI_SKIN_$(XENOMAI_SKIN)
STD_LDFLAGS := $(shell $(XENO_CONFIG) --skin=$(XENOMAI_SKIN) --skin=rtdm --ldflags) -g 
EXTRA_CFLAGS += $(shell $(XENO_CONFIG) --skin=$(XENOMAI_SKIN) --skin=rtdm --cflags)
EXTRA_CFLAGS += $(CFLAGS) 

obj-m := hello_rt.o 

all: hello_rt.ko gpio-irq-test
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules Q=

hello_rt.ko: hello_rt.c

test: gpio-irq-test
gpio-irq-test: gpio-irq-test.c pru_irq_test_bin.h
	echo 22 > /sys/class/gpio/export || true
	echo out > /sys/class/gpio/gpio22/direction || true
	$(CC) -o $@ $< $(STD_CFLAGS) $(STD_LDFLAGS) -I/root/Bela/include /root/Bela/lib/libprussdrv.a

install:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules_install
	depmod -a

clean:
	rm -f *~ Module.markers Module.symvers modules.order
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) clean

run: all
	rmmod hello_rt 2> /dev/null || true
	insmod ./hello_rt.ko
test: run
	./gpio-irq-test

pru_irq_test_bin.h: pru_irq_test.p
	pasm -V2 pru_irq_test.p > /dev/null

