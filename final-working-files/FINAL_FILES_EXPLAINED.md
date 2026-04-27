# Final working files explained

This folder contains the final files used by the working T99W373 PCIe RC + RTL8125 + IPA Ethernet setup.

The material is intentionally separated from the old research workspace so it can be reviewed, published and tested without carrying the whole local tree.

## Layout

```text
final-working-files/
  es-unlocked/
  production-locked/
  ipa-final/
  pcie-enabler-sources/
  source-build-context/
  intermediate-steps/
  reports/
```

## Exact final-file manifest

This section maps each final file to its runtime purpose. The bundle sometimes renames files during installation, so the repository filename and the modem install path are both listed where they differ.

### ES / unlocked file manifest

| Repo file | Installed/runtime path | What it does |
|---|---|---|
| `es-unlocked/boot373_custom_dtb11_v2.img` | booted with `fastboot boot` or used as ES boot image base | Provides the corrected DTB at boot time. It makes the PCIe RC / RTL8125 / IOSS shape available before Linux userspace starts, avoiding the production-style live-DT patch for the main hardware description. |
| `es-unlocked/rmnet_eth.ko` | `/moduli/rmnet_eth.ko` | Quectel-derived RMNET Ethernet driver. It registers support for the RMNET ETH egress endpoint required for IPA download offload. It must load before QMI/IPACM builds the WAN path. |
| `es-unlocked/r8125_stack.ko` | `/moduli/r8125_rebuilt.ko` | Realtek RTL8125 driver. It binds to PCI ID `10ec:8125`, creates `eth0`, and provides the Ethernet device used by IOSS. |
| `es-unlocked/ioss_rebuilt.ko` | `/moduli/ioss_rebuilt.ko` | Qualcomm IOSS core. It owns the IPA Ethernet offload registration path and calls into the IPA ETH client code. |
| `es-unlocked/r8125_ioss_rebuilt.ko` | `/moduli/r8125_ioss_rebuilt.ko` | Glue driver between RTL8125 and IOSS. It connects Realtek rings/events to the IOSS channel model. |
| `es-unlocked/pcie_enabler_ipaV22.ko` | optional diagnostic/safety module | Historical PMDS-fix module. Not the preferred ES bring-up path when the patched boot image is used, but kept because its logic led to the final production PMDS fix. |
| `es-unlocked/from-modem-live/load-pcie-enabler.sh` | `/usr/bin/load-pcie-enabler.sh` | Loads `rmnet_eth`, `r8125`, `ioss`, `r8125_ioss` in the validated order and then brings `eth0` up. |
| `es-unlocked/from-modem-live/pcie-enabler.service` | `/etc/systemd/system/pcie-enabler.service` or equivalent unit location | Early oneshot systemd unit. Runs before QCMAP/radio services so the Ethernet/IPA stack exists before networking is negotiated. |

### Production / locked file manifest

| Repo file | Installed/runtime path | What it does |
|---|---|---|
| `production-locked/pcie_enabler_non_es_v1.ko` | `/moduli/pcie_enabler_non_es_v1.ko` | Final stock-boot PCIe enabler. It enables PCIe RC at runtime, injects RTL8125/IOSS DT nodes/properties, and applies the V22-derived PMDS fix for `ipa_smmu_eth` and `ipa_smmu_eth1`. |
| `production-locked/rmnet_eth.ko` | `/moduli/rmnet_eth.ko` | Same RMNET ETH prerequisite as ES. Required before IPACM/QMI WAN negotiation for IPA Ethernet download. |
| `production-locked/r8125_stack.ko` | `/moduli/r8125_rebuilt.ko` | Same RTL8125 driver as ES. Creates `eth0` from the Realtek PCIe endpoint. |
| `production-locked/ioss_rebuilt.ko` | `/moduli/ioss_rebuilt.ko` | Same IOSS core as ES. Registers the Ethernet offload path with IPA. |
| `production-locked/r8125_ioss_rebuilt.ko` | `/moduli/r8125_ioss_rebuilt.ko` | Same Realtek/IOSS glue as ES. Completes the RTK8125B IPA ETH channel/event path. |
| `production-locked/from-modem-live/load-pcie-enabler.sh` | `/usr/bin/load-pcie-enabler.sh` | Production loader. Sets permissive SELinux as fallback, lowers `kptr_restrict`, resolves `kallsyms_lookup_name`, loads `pcie_enabler_non_es_v1`, then loads the IPA/RTL stack. |
| `production-locked/from-modem-live/pcie-enabler.service` | `/etc/systemd/system/pcie-enabler.service` or equivalent unit location | Early oneshot systemd unit. It runs before QCMAP, radio and `ipacm.service` so production live-DT injection and module loading happen before IPACM starts. |

### Common IPA/IPACM final manifest

| Repo file | Installed/runtime path | What it does |
|---|---|---|
| `ipa-final/ipa_ul_final/libipacm_abi_bridge_final.so` | `/usr/lib/libipacm_abi_bridge_final.so` | Final UL fix. Loaded into IPACM with `LD_PRELOAD`; translates the userspace/kernel IPA ABI for `eth0`, `rmnet_data0`, and `rmnet_data1`. |
| `ipa-final/ipa_ul_final/ipacm.service.final` | `/lib/systemd/system/ipacm.service` | Final IPACM service. Integrates ABI bridge environment directly and avoids the early-boot `/usrdata` logging race found during clean-install validation. |
| `ipa-final/ipa_ul_final/31-abi-bridge-final.conf` | legacy/drop-in reference | Previous drop-in form of the final ABI bridge environment. Kept as reference, but the clean-install bundle now prefers the full final `ipacm.service`. |
| `ipa-final/ipa_ul_final/ipa-iptables-dedupe.sh` | `/usr/bin/ipa-iptables-dedupe.sh` | Removes duplicate QCMAP NAT/filter rules and keeps one correct MASQUERADE rule on the active WAN interface. |
| `ipa-final/ipa_ul_final/ipa-iptables-dedupe.service` | `/etc/systemd/system/ipa-iptables-dedupe.service` | One-shot unit for the dedupe helper. |
| `ipa-final/ipa_ul_final/ipa-iptables-dedupe.timer` | `/etc/systemd/system/ipa-iptables-dedupe.timer` | Periodic timer for idempotent iptables cleanup. |
| `ipa-final/ipa_ul_final/ipa-stack-healthcheck.sh` | `/usr/bin/ipa-stack-healthcheck.sh` | Conservative recovery helper. Checks QCMAP/IPACM/WebUI/IPA-related services and restarts only failed or inactive units. |
| `ipa-final/ipa_ul_final/ipa-stack-healthcheck.service` | `/etc/systemd/system/ipa-stack-healthcheck.service` | One-shot unit for the healthcheck helper. |
| `ipa-final/ipa_ul_final/ipa-stack-healthcheck.timer` | `/etc/systemd/system/ipa-stack-healthcheck.timer` | Periodic timer for conservative stack recovery. |
| `ipa-final/ipa_ul_final/ipa-ul-final-status.sh` | `/usr/bin/ipa-ul-final-status.sh` | User-facing status command. Prints services, IPACM preload state, NAT state, IPA errors and RTK/IPA counter presence. |
| `ipa-final/ipacm_abi_bridge_v5.c` | source reference for final bridge generation | Source snapshot for the last ABI bridge iteration before the final `.so`. Useful for review and future cleanup. |
| `ipa-final/install_ipa_ul_final.sh` | installer/helper reference | Standalone installer used during final UL work; useful for seeing exactly how the final IPACM/healthcheck/dedupe pieces were staged. |

## Two supported device paths

There are two working paths because ES/unlocked and production/locked devices have different constraints.

### ES / unlocked

Folder:

```text
es-unlocked/
```

This path is for devices where a modified `boot.img` / DTB can be used. This is the cleaner method because the PCIe Root Complex and RTL8125/IOSS bindings can exist from boot instead of being injected later into the live device tree.

Important files:

```text
boot373_custom_dtb11_v2.img
pcie_enabler_ipaV22.ko
rmnet_eth.ko
r8125_stack.ko
ioss_rebuilt.ko
r8125_ioss_rebuilt.ko
from-modem-live/load-pcie-enabler.sh
from-modem-live/pcie-enabler.service
```

Runtime service:

```text
pcie-enabler.service
```

The ES service expects these installed paths:

```text
/moduli/rmnet_eth.ko
/moduli/r8125_rebuilt.ko
/moduli/ioss_rebuilt.ko
/moduli/r8125_ioss_rebuilt.ko
```

Load order:

```text
1. rmnet_eth
2. r8125
3. ioss
4. r8125_ioss
5. ip link set eth0 up
```

The service runs early:

```text
After=local-fs.target disable-selinux.service
Before=qcmap-radio-on.service QCMAP_ConnectionManagerd.service
```

Why `pcie_enabler_ipaV22.ko` is still present:

- On the ES path, the preferred fix is the modified boot image / DTB.
- `pcie_enabler_ipaV22.ko` remains useful as a historical safety/debug artifact because it contains the PMDS correction logic that informed the later production module.

### Production / locked

Folder:

```text
production-locked/
```

This path is for locked devices where using a modified `boot.img` is not practical. It uses a runtime kernel module to enable PCIe RC and inject the missing RTL8125/IOSS bindings into the live device tree.

Important files:

```text
pcie_enabler_non_es_v1.ko
rmnet_eth.ko
r8125_stack.ko
ioss_rebuilt.ko
r8125_ioss_rebuilt.ko
from-modem-live/load-pcie-enabler.sh
from-modem-live/pcie-enabler.service
```

Runtime service:

```text
pcie-enabler.service
```

The production service expects:

```text
/moduli/pcie_enabler_non_es_v1.ko
/moduli/rmnet_eth.ko
/moduli/r8125_rebuilt.ko
/moduli/ioss_rebuilt.ko
/moduli/r8125_ioss_rebuilt.ko
```

Load order:

```text
1. pcie_enabler_non_es_v1 kallsyms_lookup_name_addr=<runtime address>
2. rmnet_eth
3. r8125
4. ioss
5. r8125_ioss
6. ip link set eth0 up
```

The production loader also handles stock-device restrictions:

```text
setenforce 0
echo 0 > /proc/sys/kernel/kptr_restrict
resolve kallsyms_lookup_name from /proc/kallsyms
```

The module metadata is:

```text
name: pcie_enabler_non_es_v1
description: NON-ES runtime PCIe enabler with RTL8125/IOSS DT injection and V22 PMDS fix
vermagic: 5.4.210-perf preempt mod_unload ARMv7 p2v8
parameter: kallsyms_lookup_name_addr
```

## IPA final files

Folder:

```text
ipa-final/
```

This folder contains the final IPACM / IPA userspace fix and the helper services used by both ES and production bundles.

Important files:

```text
ipa_ul_final/libipacm_abi_bridge_final.so
ipa_ul_final/ipacm.service.final
ipa_ul_final/31-abi-bridge-final.conf
ipa_ul_final/ipa-iptables-dedupe.sh
ipa_ul_final/ipa-iptables-dedupe.service
ipa_ul_final/ipa-iptables-dedupe.timer
ipa_ul_final/ipa-stack-healthcheck.sh
ipa_ul_final/ipa-stack-healthcheck.service
ipa_ul_final/ipa-stack-healthcheck.timer
ipa_ul_final/ipa-ul-final-status.sh
ipacm_abi_bridge_v5.c
install_ipa_ul_final.sh
```

### `libipacm_abi_bridge_final.so`

Final upload fix.

It is loaded into `ipacm` with `LD_PRELOAD` and translates the ABI between the userspace IPACM binary and the kernel IPA interface used by this firmware.

The root cause was not the Realtek driver itself. The problem was that IPACM was reading IPA structures with the wrong layout. A strong symptom was a numeric field such as `dst_pipe` being read as:

```text
0x7670695f
```

which is ASCII for:

```text
"_ipv"
```

That means a string field was being interpreted as an integer because userspace and kernel disagreed on the structure layout.

The final bridge handles the relevant query, route and filter calls, including:

```text
QUERY_INTF_TX_PROPS
QUERY_INTF_RX_PROPS
QUERY_INTF_EXT_PROPS
ADD_RT
ADD_FLT
ADD_FLT_V2
ADD_FLT_AFTER
GENERATE_FLT_EQ
```

The final allowed interfaces are:

```text
eth0
rmnet_data0
rmnet_data1
```

### `ipacm.service.final`

Final replacement service file for IPACM.

The clean-install validation found that using a drop-in only was not enough on every boot. The final bundle installs a safe `ipacm.service` with:

- ABI bridge environment integrated directly;
- `LD_PRELOAD=/usr/lib/libipacm_abi_bridge_final.so`;
- safe `/usrdata` log handling;
- `StandardOutput=null`;
- stable behavior during early boot.

### `ipa-iptables-dedupe.*`

QCMAP can duplicate NAT / iptables rules. The dedupe helper keeps the active WAN path clean, especially around reboot and service recovery.

Expected final NAT state:

```text
one MASQUERADE rule on the active rmnet_dataX interface
```

### `ipa-stack-healthcheck.*`

Conservative watchdog for the modem-side stack.

Policy:

- run after boot and periodically;
- only restart services that are `failed` or `inactive`;
- do not restart healthy services;
- run iptables dedupe idempotently;
- help recover WebUI/IPACM/QCMAP without disturbing a healthy stack.

### `ipa-ul-final-status.sh`

Operational status tool.

After install and reboot:

```sh
adb shell '/usr/bin/ipa-ul-final-status.sh'
```

Expected:

```text
pcie-enabler.service active
ipacm.service active
QCMAP_ConnectionManagerd.service active
netmgrd.service active
qcmap-radio-on.service active
ipa-iptables-dedupe.timer active
ipa-stack-healthcheck.timer active
qcmap_httpd.service active
LD_PRELOAD=/usr/lib/libipacm_abi_bridge_final.so
ipacm NRestarts=0
sw_tx=0
INVALID_PIPE=0
HDRI=0
CSUM=0
```

## Kernel module roles

### `rmnet_eth.ko`

Origin: Quectel RM520-compatible environment.

Role:

- enables the RMNET Ethernet egress endpoint;
- must be loaded before the QMI/IPACM WAN path is negotiated;
- was the key piece that made IPA download hardware offload work.

Historical failure when missing:

```text
Egress ep type not defined
```

### `r8125_stack.ko`

Realtek RTL8125 driver module.

In the modem install it is used as:

```text
/moduli/r8125_rebuilt.ko
```

Runtime module name:

```text
r8125
```

Role:

- drives the RTL8125 Ethernet controller;
- creates `eth0`;
- provides the hardware endpoint used by IOSS/IPA.

### `ioss_rebuilt.ko`

Qualcomm IPA Offload Sub-System module.

Runtime module name:

```text
ioss
```

Role:

- owns the IPA offload side of the Ethernet path;
- registers Ethernet pipes;
- exposes diagnostics and preflight parameters used during reverse engineering.

### `r8125_ioss_rebuilt.ko`

Glue module between Realtek RTL8125 and IOSS.

Runtime module name:

```text
r8125_ioss
```

Role:

- connects the Realtek driver to IOSS;
- requests/open/enables the channels/events needed by the offload path;
- completes the RTK8125B IPA ETH pipeline.

## Source folder

Folder:

```text
pcie-enabler-sources/
```

Contains the important source snapshots:

```text
pcie_enabler.back
pcie_enabler.c
pcie_enabler_ipaV12.c
pcie_enabler_ipaV22.c
pcie_enabler_non_es_v1.c
```

These are not all historical attempts. They are the useful reference points:

- minimal RC enable;
- full live-DT experiment;
- V12-style IOSS binding phase;
- V22 PMDS fix phase;
- final production locked module.

## Buildable source context

Folder:

```text
source-build-context/
```

This folder contains the source/build context needed to study and rebuild the final modules:

```text
source-build-context/kernel-module-build/
source-build-context/data-eth-src/
source-build-context/pcie-non-es-v1/
```

Use this folder when the goal is not just to inspect the final binaries, but to rebuild or improve:

- `pcie_enabler_non_es_v1.ko`;
- `ioss_rebuilt.ko`;
- `r8125_stack.ko`;
- `r8125_ioss_rebuilt.ko`;
- related standalone pcie-enabler modules.

Start with:

```text
source-build-context/README.md
```

## Reports folder

Folder:

```text
reports/
```

Contains the final evidence documents copied from the lab workspace:

```text
DL_STABLE_FINAL_2026-04-25.md
IPA_UL_STABLE_FINAL_2026-04-26.md
IPA_UL_VALIDATION_REPORT_2026-04-26.md
BUNDLE_CLEAN_INSTALL_VALIDATION_2026-04-27.md
NON_ES_VALIDATION_REPORT_2026-04-27.md
CLEAN_INSTALL_VALIDATION_SUMMARY.md
```

These reports are the proof trail for:

- DL hardware path;
- UL ABI bridge fix;
- ES clean install;
- production locked clean install;
- reboot persistence;
- WebUI smoke tests;
- traffic tests.

## Intermediate folder

Folder:

```text
intermediate-steps/
```

This is not used by the final install path. It exists to show the past steps: `pcie_enabler` iterations, IPACM/ABI experiments, diagnostic IOSS/RTK module builds and the reports that explain why each direction changed.

Start with:

```text
intermediate-steps/README.md
```
