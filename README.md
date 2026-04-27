# T99w373-Research


# Foxconn T99W373 PCIe RC, RTL8125 and IPA Ethernet bring-up

This repository documents and packages the work required to use a Foxconn T99W373 modem as a PCIe Root Complex device with an external Realtek RTL8125 Ethernet controller, QCMAP networking, modem-side WebUI, and Qualcomm IPA Ethernet acceleration.

The current state is a working research release, not a polished product. Both known hardware families are now supported:

- **Unlocked / ES devices**: use the preferred path with a modified `boot.img` / DTB so the PCIe RC and RTL8125/IOSS bindings are available from boot.
- **Locked / production devices**: use a stock secure-boot image and a runtime kernel module that patches the live device tree in RAM.

Both paths now have self-installing QCMAP bundles and have passed clean-install smoke tests with download, upload, WebUI, service persistence and reboot validation.

## Status

| Area | Current state |
|---|---|
| Target modem | Foxconn T99W373 / SDX62 / `5.4.210-perf` ARMv7 |
| PCIe RC | Working on ES and production modem |
| Ethernet device | Realtek RTL8125 visible as PCIe device `10ec:8125` |
| Kernel modules | `rmnet_eth`, `r8125`, `ioss`, `r8125_ioss` loaded persistently |
| IPA download | Working, validated with multiple download tests |
| IPA upload | Working in the final configuration through the IPACM ABI bridge |
| QCMAP | Working with WAN on `rmnet_data0` / `rmnet_data1`, LAN on `bridge0` |
| WebUI | Installed on modem-side HTTP server, adapted from the earlier T99W175 work |
| Release quality | Functional research release, needs more testers and long-duration testing |

## Project origin

The idea started from earlier work done on the Foxconn T99W175. That device already had useful community knowledge around modem-side Ethernet, QCMAP, web UI behavior and AT-command handling.

The T99W373 looked close enough to be worth trying, but it had one major limitation: unlike some other modems, there was no known OEM CPE product using this exact module with Ethernet already enabled. That meant there was no donor firmware or donor device tree that we could simply copy from another T99W373-based router.

So the T99W373 work had to start almost from zero:

1. Enable the PCIe Root Complex.
2. Make the modem enumerate an external RTL8125 Ethernet controller.
3. Build compatible kernel modules for the modem kernel.
4. Reconstruct the missing RTL8125/IOSS/IPA bindings.
5. Make Qualcomm IPACM and QCMAP understand the Ethernet path.
6. Keep the result stable across reboot.
7. Package everything into installable bundles for both unlocked and locked devices.

The early plan was intentionally direct: write a custom kernel module, `pcie_enabler`, able to manipulate the live DTS/device tree in RAM and add or modify the pieces required to turn on PCIe RC mode.

That approach was low-level and experimental, but it was the right first tool: it gave immediate feedback without reflashing the modem and allowed many fast iterations while we were still discovering what the T99W373 kernel was willing to accept.

## Hardware and software target

The tested target is a Foxconn T99W373-class SDX62 modem with:

```text
Linux sdxlemur 5.4.210-perf ... armv7l GNU/Linux
```

Important runtime facts:

- The target is **ARMv7 / ARM32**, not ARM64.
- Compatible modules must match:

```text
vermagic: 5.4.210-perf preempt mod_unload ARMv7 p2v8
```

- The external Ethernet controller used during bring-up is Realtek RTL8125:

```text
PCI ID: 10ec:8125
Runtime netdev: eth0
IPA ETH debugfs name: RTK8125B
```

## Kernel source and build environment

Several Qualcomm `msm-5.4` SDX65 snapshots were evaluated. The tree that matched the modem well enough to build usable modules was:

```text
msm-5.4-LE.UM.6.3.6.r1-04700-SDX65.0
```

The build uses the pulled live modem configuration as the base kernel config:

This was one of the key points of the project. Building against a random `msm-5.4` tree was not enough; using the live modem config avoided many subtle mismatches and produced modules that could be loaded on the real target.

Main build assumptions:

```text
ARCH=arm
CROSS_COMPILE=arm-linux-gnueabihf-
LLVM=1
LLVM_IAS=0
```


## The first milestone: making `lspci` see Realtek

The first practical success was getting the modem to enumerate the RTL8125 on PCIe.

The initial `pcie_enabler` module did only the minimum:

1. Find the Qualcomm PCIe Root Complex node.
2. Force its `status` property to `"okay"`.
3. Create the platform device from the live OF node.
4. Let the Qualcomm PCIe driver bring up the RC.

That made it possible to see the Realtek PCIe hardware from the modem side with tools such as `lspci`, and it proved that the hardware path was viable.

The early stable source for this idea is:

```text
pcie_enabler.back
```

Later iterations added runtime DTS injection for:

```text
/soc/r8125_rx@eth0
/soc/r8125_tx@eth0
realtek,rtl8125@0,0
qcom,ioss_channels
qcom,iommu-group
pci-ids = "10ec:8125", "10ec:3000"
```

The later production variant also incorporates the PMDS fix derived from `pcie_enabler_ipaV22`.

## Realtek and IOSS module work

After the PCIe device was visible, the next step was building the Ethernet and offload modules against the compatible kernel tree.

The final IPA path required the vendor RTL8125 stack and Qualcomm IOSS pieces:

```text
r8125_rebuilt.ko
ioss_rebuilt.ko
r8125_ioss_rebuilt.ko
```

The final baseline module order is:

```text
1. rmnet_eth.ko
2. r8125_rebuilt.ko
3. ioss_rebuilt.ko
4. r8125_ioss_rebuilt.ko
```

For locked production units, the runtime PCIe enabler is loaded first:

```text
1. pcie_enabler_non_es_v1.ko kallsyms_lookup_name_addr=<runtime address>
2. rmnet_eth.ko
3. r8125_rebuilt.ko
4. ioss_rebuilt.ko
5. r8125_ioss_rebuilt.ko
6. ip link set eth0 up
```

Known module metadata:

```text
r8125_rebuilt.ko
  description: Realtek RTL8125 2.5Gigabit Ethernet driver
  version: 9.010.01-NAPI-RSS
  vermagic: 5.4.210-perf preempt mod_unload ARMv7 p2v8

ioss_rebuilt.ko
  description: IPA Offload Sub-System v2
  vermagic: 5.4.210-perf preempt mod_unload ARMv7 p2v8

r8125_ioss_rebuilt.ko
  description: Realtek R8125 IOSS Glue Driver
  depends: r8125,ioss
  vermagic: 5.4.210-perf preempt mod_unload ARMv7 p2v8
```

## IPA download: the Quectel `rmnet_eth` breakthrough

The PCIe and Ethernet side was only half of the problem. The T99W373 also needed the Qualcomm IPA Ethernet path to understand the RMNET Ethernet endpoint.

The important breakthrough came from a Quectel RM520 reference. A compatible `rmnet_eth.ko` module was extracted from a Quectel environment with the same practical kernel ABI(WHAT AN IMMENSE POT OF LUCK HERE):

```text
name: rmnet_eth
description: RmNet ETH Driver
depends: rmnet_core
vermagic: 5.4.210-perf preempt mod_unload ARMv7 p2v8
```

Why this mattered:

- The modem negotiates an RMNET Ethernet egress endpoint.
- Without `rmnet_eth`, the kernel does not recognize that endpoint.
- The historical symptom was:

```text
Egress ep type not defined
```

With `rmnet_eth.ko` loaded before QMI/IPACM negotiates the WAN path, the download path starts using IPA hardware.

The key RMNET endpoint detail observed during the work:

```text
EP[0] RMNET_EGRESS_DEFAULT
EP[1] RMNET_EGRESS_ETH_DATA
```

`RMNET_EGRESS_ETH_DATA` is the piece that required `rmnet_eth`.

Download validation was done with real traffic and IPA counters, not just a successful curl:

```sh
curl -4 --interface enx00e04c6802a5 --max-time 120 -L \
  'https://speed.cloudflare.com/__down?bytes=50000000' \
  -o down_50mb.bin
```

The DL PASS criteria were:

- file is complete;
- HTTP 200;
- `sw_tx_delta = 0`;
- IPA/RTK hardware counters move;
- services remain active;
- no reboot during the test.

One validated ES run after clean install:

```text
HTTP 200
file_bytes=50000000
sw_tx_delta=0
hw_tx_delta=4
SW_FILT_delta=1
services_active=yes
no_reboot=yes
result=PASS
```

## IPA upload: the hard part

After download started working, upload was still not correct. This led to several more days of work.

The misleading part was that upload connectivity could work while still not proving the proper hardware path. I had to inspect:

- IPACM logs;
- `WAN_UPSTREAM_ROUTE_ADD`;
- QMAP header handling;
- IPA route/filter ioctls;
- `sw_tx`;
- `INVALID_PIPE`, `HDRI`, `CSUM`;
- RTK IPA ring counters;
- `ipacm` PID stability and restart count.

Earlier experiments tried preload hacks and narrower translations, but those were not stable enough and were disabled before the final baseline.

The final root cause was an ABI mismatch between userspace IPACM and the kernel IPA interface.

The strongest symptom was a numeric pipe field being read as:

```text
0x7670695f
```

which is ASCII for:

```text
"_ipv"
```

In other words, IPACM was reading a string field as an integer because the structure layout did not match the kernel it was talking to. This also explained the recurring `Failed to get QMAP header` class of failures and broken route/filter programming.

## Final IPA UL fix: IPACM ABI bridge

The final fix is an ABI bridge loaded into IPACM through `LD_PRELOAD`:

```text
/usr/lib/libipacm_abi_bridge_final.so
```

The final service configuration enables translation for:

```text
eth0, rmnet_data0, rmnet_data1
```

The bridge translates the relevant IPACM/kernel IPA ABI calls, including:

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

Final runtime evidence includes:

```text
Setting up QMAP ID 1.
TriggerWANUp() if_name:rmnet_data0, ipv4_address=<valid>, mux_id:1
handle_uplink_filter_rule() ... dev eth0 ... committing modem UL rules
AddEntry() Added rule(...) successfully
```

The final validation criteria for UL are not based only on global `hw_tx`, because after reboot that counter is not proportional enough to be the only proof. The final criteria use:

- `sw_tx_delta = 0`;
- `INVALID_PIPE_delta = 0`;
- `HDRI_delta = 0`;
- `CSUM_delta = 0`;
- `ipacm` PID unchanged;
- `NRestarts=0`;
- RTK/IPA counters or eth0 IRQ activity moving under traffic;
- stable QCMAP/WAN/LAN services.

Validated UL examples:

```text
ES bundle clean install, iperf3:
sent_bytes=18481152
sent_bps=9855811
sw_tx_delta=0
rtk_cons_ring_delta=3582
rtk_prod_ring_delta=3582
INVALID_PIPE/HDRI/CSUM=0/0/0
ipacm PID unchanged
result=PASS

Production locked clean install, bind-dev iperf3:
sent_bytes=4063232
sent_bps=2166941
sw_tx_delta=0
INVALID_PIPE/HDRI/CSUM=0/0/0
ipacm PID unchanged
NRestarts=0
result=PASS
```

## ES / unlocked method

For unlocked or ES devices, the preferred method is to use a modified `boot.img` / DTB.

Reason:

- The DTS can describe the PCIe RC and RTL8125/IOSS relationship from boot.
- The kernel does not need to rely on late live-DTS manipulation for the main PCIe bring-up.
- This is cleaner and should be more robust whenever the device allows it.

The ES bundle still installs the modem-side userspace and service stack:

```text
qcmap-modem-bundle-v3-ES.tar
```

Included behavior:

- QCMAP network stack;
- modem-side WebUI;
- `rmnet_eth`, `r8125`, `ioss`, `r8125_ioss`;
- final IPACM ABI bridge;
- iptables dedupe timer;
- conservative IPA/QCMAP healthcheck timer;
- `/usr/bin/ipa-ul-final-status.sh` verification tool;
- LAN policy `192.168.225.0/24`.

The clean-install ES validation showed:

```text
8/8 services active
WebUI HTTP 200
WAN route present
LAN route 192.168.225.0/24 present
DL 50 MB PASS
UL iperf3 PASS
ipacm stable, NRestarts=0
sw_tx=0
IPA error counters zero
```

## Production / locked method

For locked production devices, the project cannot rely on a modified boot image.

The production path therefore uses:

```text
pcie_enabler_non_es_v1.ko
```

This module:

- enables the PCIe Root Complex at runtime;
- injects RTL8125/IOSS bindings into the live device tree;
- applies the PMDS fix derived from `pcie_enabler_ipaV22` to `ipa_smmu_eth` and `ipa_smmu_eth1`;
- avoids the need for a modified `boot.img`;
- accepts `kallsyms_lookup_name_addr` as a module parameter.

Module metadata:

```text
name: pcie_enabler_non_es_v1
description: NON-ES runtime PCIe enabler with RTL8125/IOSS DT injection and V22 PMDS fix
vermagic: 5.4.210-perf preempt mod_unload ARMv7 p2v8
parm: kallsyms_lookup_name_addr
```

Production constraints handled by the bundle:

- stock boot has `kptr_restrict=2`, so the loader temporarily sets `kptr_restrict=0` to resolve `kallsyms_lookup_name`;
- SELinux can block `insmod` from some contexts, so the bundle installs `disable-selinux.service` and uses `setenforce 0` as fallback;
- the module is not signed by the trusted OEM key, so the kernel is expected to be tainted when it loads.

The locked production bundle is:

```text
qcmap-modem-bundle-v3-NON-ES.tar
```

Clean-install production validation showed:

```text
Services: 8/8 active
Modules: pcie_enabler_non_es_v1, rmnet_eth, r8125, ioss, r8125_ioss
WAN route via rmnet_data0
LAN route 192.168.225.0/24 via bridge0
WebUI HTTP 200
NAT MASQUERADE present exactly on rmnet_data0
DL 50 MB PASS
UL bind-dev iperf3 PASS
Second reboot persistence PASS
```

Known production note:

```text
IOSS may log a fallback for runtime channel phandles:
"channel[0] phandle resolved to non-channel node 'r8125_rx', trying fallback"
```

In the current tests this did not block `ipa_eth_client_conn_pipes`, `ipa_eth_client_reg_intf`, DL or UL, but it should be monitored by testers.

Also in production unit it seems impossible to make the ethernet led to work , i really can't figure out why , not that is a big issue but damn.

## WebUI

The WebUI work was adapted from the earlier T99W175 modem UI, then corrected for T99W373 behavior.

Important T99W373 differences found during testing:

- `AT^VERSION?` works better than `AT+CGMR` for firmware/version parsing on this target.
- `AT^BAND_PREF_EXT?` is not supported and returns `ERROR`.
- Large mixed AT batches can hang or return dirty data, so the UI uses smaller independent queries.

The bundle installs the WebUI while preserving the `/WEBSERVER` parent and refreshing only `/WEBSERVER/www`.


The report corpus is intentionally kept because this project was not a single patch. It was a long reverse-engineering process involving kernel module bring-up, live DTS mutation, SMMU/PMDS debugging, IOSS/IPA pipe validation, IPACM ABI translation, QCMAP boot ordering and clean-install packaging.

## Known limitations

This project is not perfect and should not be treated as a finished commercial firmware.

Known limitations:

- The production path relies on live kernel/DTS manipulation.
- The production path requires resolving `kallsyms_lookup_name` at runtime.
- The modules are out-of-tree and may taint the kernel.
- The IPACM final fix is an ABI bridge through `LD_PRELOAD`; the cleaner long-term fix would be rebuilding or patching IPACM against the exact IPA headers used by the target kernel.
- The validation is real, but still short compared to long-term CPE uptime requirements.
- More testing is needed across firmware versions, SIM/APN profiles, radio conditions, reboots, cable changes and sustained traffic.

## Tester request

This release is shared because it finally works end to end, not because it is perfect.

Help is welcome with:

- long-duration uptime tests;
- repeated cold boot and warm reboot tests;
- ES vs production comparison;
- different APNs and carriers;
- sustained DL/UL traffic;
- WebUI regression tests;
- checking whether IPA counters remain clean over time;
- collecting logs when services recover through the healthcheck timer;
- identifying what is still wrong or fragile.

The goal is to turn this from a working lab release into something reproducible and understandable by other T99W373 owners.

## Current verdict

The project has reached a functional milestone:

```text
T99W373 PCIe RC: working
RTL8125 Ethernet: working
QCMAP/WebUI: working
IPA DL: working
IPA UL: working with ABI bridge
ES bundle: clean-install PASS
Production locked bundle: clean-install PASS
```

There is still cleanup to do, but the hard part is no longer theoretical: both device paths can boot into a usable modem-side Ethernet/QCMAP/IPA configuration.


MORE and trust me when i say MORE documentation will come in the next days , have around 300Gb of file test MD and stuff to sort before.

Everything is being put out on the web for free so that people can enjoy and develop around this modem that otherwise would have ended on a landfill as ewaste , no need to thank me now ,i know i am awesome.


## License

This project is licensed under the PolyForm Noncommercial License 1.0.0.

Use, modification and redistribution are allowed only for non-commercial purposes.

Commercial use, resale, paid redistribution, SaaS offering, or profit-driven modification is prohibited without prior written permission from the author(seriously i will personally come and kick your butt).
