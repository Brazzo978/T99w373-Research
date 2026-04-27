obj-m += pcie_enabler.o
obj-m += pcie_enabler_ipaV11.o
obj-m += pcie_enabler_ipaV12.o
obj-m += pcie_enabler_ipaV16.o
obj-m += pcie_enabler_ipaV17.o
obj-m += pcie_enabler_ipaV18.o
obj-m += pcie_enabler_ipaV19.o
obj-m += pcie_enabler_ipaV20.o
obj-m += pcie_enabler_ipaV21.o
obj-m += pcie_enabler_ipaV22.o
obj-m += pcie_enabler_stepdiag.o
obj-m += pcie_enabler_v2.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
