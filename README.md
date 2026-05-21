# T99W373 QCMAP modem bundle

This repository publishes the first tested self-installing bundle for the Foxconn
T99W373 modem in locked / production / NON-ES mode.

The release artifact is a single tar archive:

```text
qcmap-modem-bundle-v8-NON-ES-rx1024.tar
```

It is meant to be copied to the modem with ADB and installed from the modem shell.
The installer is permanent: it writes services, modules, QCMAP configuration,
the modem WebUI and runtime helpers to the modem filesystem, then reboots.

## Supported target

| Item | Status |
|---|---|
| Modem | Foxconn T99W373 / SDX62 |
| Firmware kernel | `5.4.210-perf` ARMv7 |
| Device family | Locked / production / NON-ES |
| Ethernet controller | Realtek RTL8125 |
| Bundle version | `v8 NON-ES RX1024` |
| WebUI version | `Simple T99373-1.0.2B` |
| LAN default | `192.168.225.1/24` |

This release does not require a custom `boot.img`.

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

For non-interactive installs, pass the choices through the environment:

```sh
INSTALL_SSH_SERVER=1 INSTALL_BTOP=1 ROOT_PASSWORD=123 sh ./install-full-stack-on-modem.sh
```

Use `0` instead of `1` to skip one of the optional components.

The modem reboots automatically when the installer completes.

## After install

Default access:

| Service | Address |
|---|---|
| WebUI | `http://192.168.225.1` |
| SSH, if enabled | `root@192.168.225.1` |
| Default root password | `123`, unless changed with `ROOT_PASSWORD` |

Useful validation commands:

```sh
adb shell 'systemctl is-active qcmap-radio-on qcmap_httpd ipacm ipa-stack-healthcheck.timer'
adb shell '/usr/bin/ipa-ul-final-status.sh'
adb shell 'ip -4 route; ip addr show bridge0; ip addr show rmnet_data0'
```

## What the bundle installs

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
  The Tailscale binary is not bundled; the helper downloads the current build
  when the modem is online. Startup waits for RMNET WAN and a valid clock before
  launching `tailscaled`, so it does not break modem navigation during boot.
- Optional Dropbear SSH server.
- Optional `btop`.
- Persistent installer logging and heartbeat output so long install steps are
  visible instead of looking frozen.

The bundle does not set the APN. Keep the modem APN configured separately.

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
