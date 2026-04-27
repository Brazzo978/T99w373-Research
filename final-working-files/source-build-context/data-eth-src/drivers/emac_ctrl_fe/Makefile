ccflags-y := -I$(srctree)/include/ -I$(srctree)/include/linux/
ccflags-y += -I$(srctree)/techpack/data-eth/drivers/include/linux/

#CONFIG_EMAC_CTRL_FE=y
obj-$(CONFIG_QTI_QUIN_GVM) += emac-ctrl-fe-virtio.o
emac-ctrl-fe-virtio-objs := emac_ctrl_fe_virtio.o