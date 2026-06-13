obj-m += ultrasonic_lkm.o

KDIR    := /lib/modules/$(shell uname -r)/build
PRU_PKG := /usr/lib/ti/pru-software-support-package-v6.0
PRU_CMD := $(PRU_PKG)/examples/am335x/PRU_Halt/AM335x_PRU.cmd
PRU_LIB := /usr/share/ti/cgt-pru/lib/rtspruv3_le.lib

all:
	make -C $(KDIR) M=$(shell pwd) modules
	$(CC) ultrasonic_app.c -o ultrasonic_app

test: all
	./ultrasonic_app

pru:
	clpru -v3 -O2 --display_error_number --endian=little --hardware_mac=on \
	      -I/usr/share/ti/cgt-pru/include \
	      -I$(PRU_PKG)/include \
	      -I$(PRU_PKG)/include/am335x \
	      -fe ultrasonic_pru.object \
	      ultrasonic_pru.c
	clpru -v3 -z $(PRU_CMD) \
	      -o ultrasonic_pru.out \
	      ultrasonic_pru.object \
	      $(PRU_LIB)

clean:
	make -C $(KDIR) M=$(shell pwd) clean
	rm -f ultrasonic_app ultrasonic_pru.out ultrasonic_pru.object
	rm -f ultrasonic_lkm.o ultrasonic_lkm.ko ultrasonic_lkm.mod ultrasonic_lkm.mod.c
	rm -f Module.symvers modules.order .ultrasonic_lkm.o.cmd .ultrasonic_lkm.ko.cmd 2>/dev/null || true
