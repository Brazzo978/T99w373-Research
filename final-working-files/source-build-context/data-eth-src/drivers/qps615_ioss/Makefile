ifeq ($(building_out_of_srctree),1)
ccflags-y += -I$(src)/../ioss/include/
endif

ccflags-y += -I$(srctree)/techpack/data-eth/drivers/qps615/src/
ccflags-y += -I$(srctree)/techpack/data-eth/drivers/ioss/include/

obj-$(CONFIG_QPS615_IOSS) += qps615-ioss.o
qps615-ioss-y += qps615_ioss.o

