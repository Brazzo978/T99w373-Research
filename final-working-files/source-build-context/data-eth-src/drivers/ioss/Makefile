
ifeq ($(building_out_of_srctree),1)
ccflags-y += -I$(src)/../ioss/include/
endif

ccflags-y += -I$(srctree)/$(src)/include
ccflags-y += -I$(src)/include

obj-$(CONFIG_IOSS) += ioss.o

ioss-y := \
	ioss_main.o \
	ioss_bus.o \
	ioss_pci.o \
	ioss_net.o \
	ioss_debugfs.o \
	ioss_of.o \
	ioss_ipa.o \
	ioss_ipa_hal.o \
	ioss_mem.o \
	ioss_utils.o
