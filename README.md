# T99W373 Pcie-RC modem bundle

This repository publishes the first tested self-installing bundle for the Foxconn
T99W373 modem.

The release artifact is a single tar archive:

```text
qcmap-modem-bundle-v8-NON-ES-rx1024.tar
```


## Supported target

| Item | Status |
|---|---|
| Modem | Foxconn T99W373 / SDX62 |
| Firmware |Tricky part|
|kernel | `5.4.210-perf` ARMv7 |
| Ethernet controller | Realtek RTL8125 |
| WebUI version | `Simple T99373-1.0.2B` |


## Install

Download the release asset from GitHub, then push it to the modem:

```sh
adb push qcmap-modem-bundle-v8-NON-ES-rx1024.tar /foxusr/qcmap-modem-bundle-v8.tar
```

Run the installer:

```sh
adb shell 'rm -rf /foxusr/qcmap-install && mkdir -p /foxusr/qcmap-install && tar -C /foxusr/qcmap-install -xf /foxusr/qcmap-modem-bundle-v8.tar && cd /foxusr/qcmap-install/qcmap-modem-bundle-v8-NON-ES-rx1024 && ROOT_PASSWORD=123 ENABLE_WEB_CLIENT=1 sh ./install-full-stack-on-modem.sh'
```

During an interactive install, the script asks:

```text
Do you want to install the SSH server? [Y/n]
Do you want to install btop? [Y/n]
```

For non-interactive installs, you can pass the choices through the environment:

```sh
INSTALL_SSH_SERVER=1 INSTALL_BTOP=1 ROOT_PASSWORD=123 sh ./install-full-stack-on-modem.sh
```

Use `0` instead of `1` to skip one of the optional components.

The modem reboots automatically when the installer completes, i reccommend to use interactive install , installation can take VERY long time , ideally should be done before 10min.

## After install

Default access:

| Service | Address |
|---|---|
| WebUI | `http://192.168.225.1` |
| SSH, if enabled | `root@192.168.225.1` |
| Default root password | `123`, unless changed with `ROOT_PASSWORD` |


```

## What doesthe bundle installs

- QCMAP runtime configured for WAN over RMNET and LAN on `bridge0`.
- LAN defaults to `192.168.225.1/24` with DHCP and NAT.
- Runtime PCIe enablement for locked T99W373 devices through
  `pcie_enabler_non_es_v1.ko`.
- Realtek / IPA Ethernet module stack:
  `rmnet_eth.ko`, `r8125_stack.ko`, `ioss_rebuilt.ko`,
  `r8125_ioss_rebuilt.ko`.
- Final IPA UL/DL userspace fix:
  `libipacm_abi_bridge_final.so`, patched `ipacm.service`,
  `ipa-iptables-dedupe.timer`, `ipa-stack-healthcheck.timer`,
  and `/usr/bin/ipa-ul-final-status.sh`.
- T99W373 SimpleAdmin WebUI on the modem HTTP server.
- WebUI Settings support for subnet, DHCP range and lease configuration.
- Custom TTL runtime and WebUI control.
- Automatic reboot scheduler.
- Connection watchdog with long boot grace for slow T99W373 WAN startup.
- Tailscale WebUI helper.
- Optional Dropbear SSH server(PLEASE INSTALL IT).
- Optional `btop`(could not find any htop build working).


The bundle does not set the APN. Please if not automatically inserted by the mbn config put it yourself, even if the gui isnt responsive push it anyway.

## Research documentation

The long technical history, PCIe/IPA bring-up notes, module lineage and
intermediate experiments are kept in:

- [`RESEARCH.md`](RESEARCH.md)
- [`final-working-files/FINAL_FILES_EXPLAINED.md`](final-working-files/FINAL_FILES_EXPLAINED.md)
- [`final-working-files/VERSION_EVOLUTION.md`](final-working-files/VERSION_EVOLUTION.md)
- [`final-working-files/intermediate-steps/README.md`](final-working-files/intermediate-steps/README.md)
- [`final-working-files/source-build-context/README.md`](final-working-files/source-build-context/README.md)

Start from the release bundle if you want to install the working setup. Read the
research notes if you want to understand how the PCIe, Realtek, QCMAP and IPA
pieces were discovered.
