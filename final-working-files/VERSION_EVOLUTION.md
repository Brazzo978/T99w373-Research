# Version evolution and technical changes

This document explains what changed from version to version during the T99W373 PCIe RC + RTL8125 + IPA bring-up.

It is not a full lab notebook. It is a curated explanation of the important versions and why the final setup looks the way it does.

## Phase 0: T99W175 inspiration, but no T99W373 donor

The project was inspired by earlier T99W175 work around modem-side Ethernet, QCMAP and WebUI behavior.

The important difference was that the T99W373 did not have a known OEM CPE donor with Ethernet already enabled. That meant there was no ready-made T99W373 DTB, IPACM config or firmware image to copy.

Everything below had to be discovered on the actual modem:

- PCIe Root Complex enablement;
- RTL8125 enumeration;
- compatible kernel tree;
- module build flags;
- IOSS binding structure;
- RMNET ETH requirement;
- IPACM ABI mismatch;
- final boot order.

## Phase 1: minimal `pcie_enabler`

Reference source:

```text
pcie-enabler-sources/pcie_enabler.back
```

Goal:

```text
turn on PCIe RC enough to enumerate the RTL8125
```

Behavior:

1. Find a Qualcomm/Synopsys PCIe node in the live device tree.
2. Allocate `status = "okay"`.
3. Call `of_update_property()`.
4. Call `of_platform_device_create()`.

Why it mattered:

- This was the first proof that PCIe RC mode could be enabled from the modem.
- It made the Realtek device visible with PCI tools.
- It proved the external Ethernet hardware path was viable.

Limit:

- It did not solve the IPA/IOSS binding problem.
- It only woke the PCIe side.

## Phase 2: build environment and kernel compatibility

The module work only became repeatable after finding a close enough Qualcomm kernel tree and using the live modem config.

Working kernel tree:

```text
msm-5.4-LE.UM.6.3.6.r1-04700-SDX65.0
```

Target facts:

```text
ARCH=arm
runtime arch=armv7l
vermagic=5.4.210-perf preempt mod_unload ARMv7 p2v8
```

Important lesson:

```text
Do not assume ARM64 on this target.
```

Another important practical fix:

```text
Build from a path without spaces, for example /tmp/msm-5.4-04700-buildsrc.
```

This phase produced loadable modules and allowed meaningful A/B tests on real hardware.

## Phase 3: Realtek module attempts

Early modules:

```text
r8169.ko
realtek.ko
```

Purpose:

- validate that the kernel tree and config could produce modules accepted by the modem;
- verify that Realtek PCI IDs were seen correctly;
- test basic PCIe/NIC behavior.

Final direction:

```text
r8125_stack.ko
ioss_rebuilt.ko
r8125_ioss_rebuilt.ko
```

Why:

- the in-tree Realtek path was useful for proving basic Ethernet;
- the IPA offload path required the vendor RTL8125 and Qualcomm IOSS stack.

## Phase 4: V2 to V6 live-DT experiments

Reference family:

```text
pcie_enabler_ipaV2.c
pcie_enabler_ipaV5.c
pcie_enabler_ipaV6.c
pcie_enabler_ipaV6.1.c
pcie_enabler_ipaV6.2.c
pcie_enabler_ipaV6.3.c
```

Goal:

```text
create the missing RTL8125/IOSS device-tree pieces at runtime
```

What changed:

- attempts to create a child RTL8125 node under the PCIe root port;
- attempts to add channel nodes;
- attempts to attach `pdev->dev.of_node`;
- attempts to make sysfs expose the `of_node` link cleanly.

Important observed issue:

```text
pci 0000:01:00.0: Error -2 creating of_node link
```

Meaning:

- the kernel could see a runtime OF pointer internally;
- sysfs/device-tree linkage was still incomplete or inconsistent;
- out-of-tree live OF manipulation was more fragile than a real DTB change.

## Phase 5: V12-style full binding attempts

Reference source:

```text
pcie-enabler-sources/pcie_enabler_ipaV12.c
```

Goal:

```text
make the runtime RTL8125 node look enough like a real IOSS Ethernet device
```

Added concepts:

- RTL8125 endpoint node;
- IOSS channels;
- IOMMU group properties;
- `pci-ids`;
- early attempts around IOMMU map / SID behavior.

Important debugging result:

- Some reboot observations were later traced to test-loop teardown, especially `rmmod r8125`, not only to the enable path itself.
- `rmmod r8125` could trigger an Oops in the driver removal path.

Practical lesson:

```text
For this target, test load-once-per-boot when validating PCIe/RTL8125 stability.
Do not rely on repeated rmmod/insmod loops as a clean test method.
```

## Phase 6: IPA/IOSS crash isolation

Several versions and module variants were used to isolate crashes around:

```text
ioss_ipa_register
ipa_eth_client_conn_pipes
ipa3_eth_connect
ipa3_smmu_map_eth_pipes
av8l_fast_map
```

Diagnostics added or used in `ioss_rebuilt.ko` included:

```text
preflight_only
preflight_strict
iova_ceiling
fix_high_iova
skip_ipa_register
skip_conn_pipes
disable_tcm
```

Key result:

- the crash path was below simple PCIe bring-up;
- more `pcie_enabler` changes alone were not enough;
- IOSS/IPA pipe mapping and SMMU/PMDS state had to be understood.

## Phase 7: V22 PMDS fix

Reference source:

```text
pcie-enabler-sources/pcie_enabler_ipaV22.c
```

Goal:

```text
fix broken/unsafe PMDS state for IPA Ethernet SMMU nodes
```

Important nodes:

```text
ipa_smmu_eth
ipa_smmu_eth1
```

Why this mattered:

- earlier fixes touched only part of the IPA SMMU side;
- `ipa_smmu_eth1` also needed correction;
- V22 aligned the PMDS/base/end state with the usable IPA SMMU setup.

This became one of the ingredients of the final production locked module.

## Phase 8: ES final path via modified boot image

Final ES artifact:

```text
es-unlocked/boot373_custom_dtb11_v2.img
```

Why this is preferred on unlocked devices:

- the DTB can describe PCIe RC, RTL8125 and IOSS from boot;
- the kernel starts with a coherent view of the hardware;
- less live device-tree mutation is needed;
- it is cleaner than patching RAM after boot.

Final ES loader:

```text
es-unlocked/from-modem-live/load-pcie-enabler.sh
```

Final ES load order:

```text
rmnet_eth
r8125
ioss
r8125_ioss
eth0 up
```

## Phase 9: Quectel `rmnet_eth` and IPA download

Final file:

```text
rmnet_eth.ko
```

Origin:

```text
Quectel RM520-compatible environment
```

Problem solved:

```text
Egress ep type not defined
```

Why it was required:

- the modem exposes an RMNET Ethernet egress endpoint;
- IPA download hardware offload needs that endpoint type known before QMI/IPACM builds the path;
- loading `rmnet_eth` first allows the DL path to enter hardware.

Validation:

```text
50 MB download
HTTP 200
file complete
sw_tx_delta=0
services active
no reboot
```

## Phase 10: Upload was still wrong

After DL was working, upload connectivity could work but did not yet prove a correct hardware path.

Symptoms investigated:

```text
Failed to get QMAP header
WAN_UPSTREAM_ROUTE_ADD not behaving as expected
ADD_FLT_V2 failures
invalid or missing route/filter programming
IPACM reading wrong pipe/header fields
```

Old preload hacks and forced route/filter experiments helped identify the problem but were not acceptable as the final baseline.

## Phase 11: IPACM ABI bridge versions

Reference source:

```text
ipa-final/ipacm_abi_bridge_v5.c
```

Important history:

- early preload tests proved the bug was in userspace/kernel ABI interpretation;
- V2/V3 mapped more of the query and route/filter structures;
- V4/V5 narrowed translation and stability behavior;
- the final `.so` uses an allowlist and translates only the relevant interfaces and ioctls.

The root cause was ABI layout mismatch. The strongest clue was:

```text
0x7670695f
```

being read as a numeric field even though it is ASCII:

```text
"_ipv"
```

That means IPACM was reading a string as an integer due to wrong offsets.

Final file:

```text
ipa-final/ipa_ul_final/libipacm_abi_bridge_final.so
```

Final service:

```text
ipa-final/ipa_ul_final/ipacm.service.final
```

Interfaces translated:

```text
eth0
rmnet_data0
rmnet_data1
```

Result:

```text
UL path stable
ipacm NRestarts=0
sw_tx_delta=0
INVALID_PIPE/HDRI/CSUM=0
traffic tests PASS
```

## Phase 12: production locked final module

Final source:

```text
pcie-enabler-sources/pcie_enabler_non_es_v1.c
```

Final module:

```text
production-locked/pcie_enabler_non_es_v1.ko
```

Why it exists:

- production devices cannot rely on a modified boot image;
- the same PCIe/RTL8125/IOSS setup has to be created at runtime;
- secure boot stock path still needs PCIe RC and IPA ETH enabled.

What it combines:

```text
PCIe RC enable
RTL8125/IOSS runtime DT injection
V22 PMDS fix for ipa_smmu_eth and ipa_smmu_eth1
kallsyms_lookup_name_addr runtime parameter
```

Production loader behavior:

```text
set SELinux permissive as fallback
set kptr_restrict=0
resolve kallsyms_lookup_name
load pcie_enabler_non_es_v1
load rmnet_eth
load r8125
load ioss
load r8125_ioss
bring eth0 up
```

Validation:

```text
clean install PASS
second reboot persistence PASS
WebUI HTTP 200
DL 50 MB PASS
UL bind-dev iperf3 PASS
```

## Phase 13: packaging and boot ordering

The final bundles are not just module copies. They also fix service order.

Important unit ordering:

```text
pcie-enabler.service
Before=qcmap-radio-on.service QCMAP_ConnectionManagerd.service
```

On production locked:

```text
Before=qcmap-radio-on.service QCMAP_ConnectionManagerd.service ipacm.service
```

Why:

- QCMAP and IPACM must not start before the PCIe/IPA Ethernet stack is ready;
- `rmnet_eth` must be present before the modem negotiates the Ethernet egress endpoint;
- IPACM must start with the ABI bridge active.

Other final packaging fixes:

- safe final `ipacm.service`;
- NAT/iptables dedupe timer;
- conservative healthcheck timer;
- WebUI installed without deleting the `/WEBSERVER` parent;
- LAN normalized to `192.168.225.0/24`.

## Final state

Current working result:

```text
ES / unlocked:
  modified boot image + final IPA stack
  clean install PASS

Production / locked:
  stock boot + pcie_enabler_non_es_v1 runtime patch
  clean install PASS

Common final IPA:
  rmnet_eth for DL
  libipacm_abi_bridge_final.so for UL ABI translation
  ipacm.service.final for stable boot
  healthcheck and iptables dedupe for service recovery
```

This does not mean the project is finished. It means the hard path is now functional and reproducible enough for testers to help find the next layer of bugs.

