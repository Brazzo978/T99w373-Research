
ifeq ($(building_out_of_srctree),1)
ccflags-y += -I$(src)/../ioss/include/
endif

ccflags-y += -I$(srctree)/drivers/net/ethernet/aquantia/atlantic-fwd/
ccflags-y += -I$(srctree)/techpack/data-eth/drivers/ioss/include/

obj-$(CONFIG_AQFWD_IOSS) += aqc-ioss.o

aqc-ioss-objs := \
		aqc_ioss.o \
		aqc_regs.o

