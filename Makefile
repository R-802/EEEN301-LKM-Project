obj-m += lkm.o

KDIR    := /lib/modules/$(shell uname -r)/build
PRU_PKG := /usr/lib/ti/pru-software-support-package-v6.0
PRU_CMD := $(PRU_PKG)/examples/am335x/PRU_Halt/AM335x_PRU.cmd
PRU_LIB := /usr/share/ti/cgt-pru/lib/rtspruv3_le.lib

all:
	make -C $(KDIR) M=$(shell pwd) modules
	$(CC) app.c -o app

test: all
	./app

pru:
	clpru -v3 -O2 --display_error_number --endian=little --hardware_mac=on \
	      -I/usr/share/ti/cgt-pru/include \
	      -I$(PRU_PKG)/include \
	      -I$(PRU_PKG)/include/am335x \
	      -fe pru.object \
	      pru.c
	clpru -v3 -z $(PRU_CMD) \
	      -o pru.out \
	      pru.object \
	      $(PRU_LIB)

clean:
	make -C $(KDIR) M=$(shell pwd) clean
	rm -f app pru.out pru.object
	rm -f lkm.o lkm.ko lkm.mod lkm.mod.c
	rm -f Module.symvers modules.order .lkm.o.cmd .lkm.ko.cmd 2>/dev/null || true
