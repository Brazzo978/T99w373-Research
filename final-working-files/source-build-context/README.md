# Source and build context

This folder contains source code and build context for the files published in `final-working-files/`.

It is included so people can study, rebuild and improve the final pieces instead of only downloading opaque binaries.

## Layout

```text
source-build-context/
  kernel-module-build/
  data-eth-src/
  pcie-non-es-v1/
```

## `kernel-module-build/`

Build scripts and target config used during the T99W373 module work.

Important files:

| File | Purpose |
|---|---|
| `Dockerfile` | Ubuntu 20.04 cross-build environment with ARM toolchains and kernel build dependencies. |
| `config.txt` | Kernel config captured from the live modem. This is the config used to make the module build match the T99W373 runtime kernel. |
| `build_module.sh` | Generic out-of-tree module builder. Used for standalone modules such as `pcie_enabler`. |
| `build_data_eth_stack.sh` | Composite builder for `ioss`, `r8125`, and `r8125_ioss`. |
| `build_vendor_r8125.sh` | Builder for the standalone vendor RTL8125 module. |
| `build_r8169.sh` | Early builder for in-tree `r8169` and `realtek` modules. |
| `Makefile` | Local out-of-tree module Makefile used by pcie-enabler builds. |
| `arm-smmu.h` | Local header used during some pcie-enabler/SMMU related iterations. |

Known target facts:

```text
runtime kernel: 5.4.210-perf
runtime arch: armv7l
module vermagic: 5.4.210-perf preempt mod_unload ARMv7 p2v8
ARCH=arm
CROSS_COMPILE=arm-linux-gnueabihf-
LLVM=1
LLVM_IAS=0
```

The best-matching kernel tree used in the lab was:

```text
msm-5.4-LE.UM.6.3.6.r1-04700-SDX65.0
```

Use a path without spaces for kernel builds:

```text
/tmp/msm-5.4-04700-buildsrc
```

## `data-eth-src/`

Source tree for the final data-eth stack:

```text
ioss_rebuilt.ko
r8125_stack.ko
r8125_ioss_rebuilt.ko
```

Important subfolders:

| Folder | Builds |
|---|---|
| `drivers/ioss/` | `ioss.ko`, published as `ioss_rebuilt.ko` |
| `drivers/r8125/` | `r8125.ko`, published as `r8125_stack.ko` or installed as `/moduli/r8125_rebuilt.ko` |
| `drivers/r8125_ioss/` | `r8125_ioss.ko`, published as `r8125_ioss_rebuilt.ko` |

Build command used in the lab:

```sh
./build_data_eth_stack.sh \
  /tmp/msm-5.4-04700-buildsrc \
  '/home/manu/Scrivania/linaro 5.4' \
  '/home/manu/Scrivania/linaro 5.4/data-eth-src'
```

For a clean repo checkout, adapt the two workspace paths to your local clone.

## `pcie-non-es-v1/`

Complete source/build folder for the final production locked PCIe enabler:

```text
pcie_enabler_non_es_v1.c
Makefile
arm-smmu.h
pcie_enabler_non_es_v1.ko
```

Runtime module:

```text
production-locked/pcie_enabler_non_es_v1.ko
```

What it does:

- enables the PCIe Root Complex at runtime;
- injects RTL8125/IOSS device-tree bindings;
- applies the V22-derived PMDS fix for `ipa_smmu_eth` and `ipa_smmu_eth1`;
- accepts `kallsyms_lookup_name_addr=<addr>`.

This is the module used for locked / production devices where a modified boot image is not used.

