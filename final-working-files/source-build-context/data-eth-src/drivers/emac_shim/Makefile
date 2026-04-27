ccflags-y := -I$(srctree)/include/ -I$(srctree)/include/linux/
ccflags-y += -I$(srctree)/include/net/
ccflags-y += -I$(srctree)/techpack/data-eth/drivers/include/linux/

obj-$(CONFIG_EMAC_SHIM) += stmmac.o
stmmac-objs:= stmmac_thin.o stmmac_ethtool_thin.o \
	      dwmac4_descs.o dwmac4_thin.o hwif_thin.o \
	      stmmac_platform_thin.o dwmac-qcom-ethqos-thin.o
