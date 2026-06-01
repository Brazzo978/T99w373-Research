# T99W373 QCMAP bundle v1.0.0

Superseded by v1.0.1 / bundle v9. Use v1.0.1 for new installs because it fixes
rootfs growth from repeated iptables backup snapshots.

First public release of the self-installing T99W373 locked / production / NON-ES
bundle.

Release asset:

```text
qcmap-modem-bundle-v8-NON-ES-rx1024.tar
sha256: 0241e94f9c83dbc635eb286dd52ca8ae49e1eed9eec231db31260c78aaf0e26d
```

## What this bundle does

- Installs a permanent QCMAP setup for the Foxconn T99W373 modem.
- Keeps the modem on stock locked / NON-ES boot firmware; no `boot.img` is
  included or flashed by this release.
- Enables the PCIe / Realtek RTL8125 Ethernet path at runtime with
  `pcie_enabler_non_es_v1.ko`.
- Installs the modem Ethernet module stack:
  `rmnet_eth.ko`, `r8125_stack.ko`, `ioss_rebuilt.ko`,
  `r8125_ioss_rebuilt.ko`.
- Configures QCMAP for WAN over RMNET and LAN over `bridge0`.
- Sets the default LAN to `192.168.225.1/24` with DHCP and NAT.
- Installs the final IPA UL/DL userspace fix:
  `libipacm_abi_bridge_final.so`, a patched `ipacm.service`,
  `ipa-iptables-dedupe.timer`, `ipa-stack-healthcheck.timer`,
  and the validation helper `/usr/bin/ipa-ul-final-status.sh`.
- Installs the T99W373 SimpleAdmin WebUI, version `Simple T99373-1.0.2B`.
- Adds WebUI control for subnet/DHCP settings, custom TTL, automatic reboot,
  connection watchdog and Tailscale.
- Keeps Tailscale download-based: the modem downloads the current Tailscale build
  only when online. The service waits for RMNET WAN and a valid clock before
  starting, avoiding the boot-time navigation failure seen during testing.
- Adds optional Dropbear SSH server installation.
- Adds optional `btop` installation.
- Adds installer logging, step tracking and heartbeat messages for long install
  phases.
- Uses `/foxusr` for staging when available, reducing pressure on the small
  `/tmp` filesystem during clean installs.
- Reboots automatically after installation by default.

The bundle does not configure the APN.

## Install summary

```sh
adb push qcmap-modem-bundle-v8-NON-ES-rx1024.tar /foxusr/qcmap-modem-bundle-v8.tar
adb shell 'rm -rf /foxusr/qcmap-install && mkdir -p /foxusr/qcmap-install && tar -C /foxusr/qcmap-install -xf /foxusr/qcmap-modem-bundle-v8.tar && cd /foxusr/qcmap-install/qcmap-modem-bundle-v8-NON-ES-rx1024 && ROOT_PASSWORD=123 ENABLE_WEB_CLIENT=1 sh ./install-full-stack-on-modem.sh'
```

The installer asks whether to install SSH server and `btop`. For scripted use:

```sh
INSTALL_SSH_SERVER=1 INSTALL_BTOP=1 ROOT_PASSWORD=123 sh ./install-full-stack-on-modem.sh
```

## Expected result

After reboot:

- WebUI: `http://192.168.225.1`
- SSH, if enabled: `root@192.168.225.1`
- WAN route through `rmnet_data0` or another QCMAP-selected RMNET interface
- LAN bridge on `bridge0`
- QCMAP, IPACM, WebUI, watchdog and timers active

## Validation status

This release was tested on a clean modem install path with:

- QCMAP service recovery and reboot persistence
- modem WebUI access
- Settings page subnet/DHCP/lease behavior
- TTL runtime
- automatic reboot scheduler
- connection watchdog with long T99W373 boot grace
- Tailscale install/startup behavior
- WAN navigation after reboot with Tailscale enabled

More long-duration testing and wider hardware/firmware reports are still useful.
