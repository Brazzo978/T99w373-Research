# T99W373 QCMAP bundle v1.0.1

Maintenance release for the self-installing T99W373 locked / production /
NON-ES bundle.

Release asset:

```text
qcmap-modem-bundle-v9-NON-ES-rx1024.tar
sha256: 7d044e7ea6d92c203fb526707b9de9eeb2553873381c337b18e9376cf225a5fc
```

## Why this release exists

The v8 bundle itself could bring up PCIe, Realtek, IOSS and IPA correctly, but
one maintenance script wrote a new `iptables-save` backup under
`/root/ipa_stabilize_backups` every timer run.

On these modems `/root` lives on the very small rootfs. After enough runs, rootfs
could reach 100% usage. Once that happened, QCMAP runtime files under
`/etc/data` could be left truncated, causing:

- `QCMAP_ConnectionManagerd` exceptions;
- `netmgrd` failures;
- no `rmnet_data0` default route;
- LAN/WebUI reachable but WAN navigation broken.

PCIe was not the failing layer in that incident.

## Fixes

- `ipa-iptables-dedupe.sh` now writes backups to
  `/tmp/ipa_stabilize_backups`.
- Only the newest four dedupe backup snapshots are kept.
- The main installer now runs a small-filesystem cleanup step before reboot.
- Cleanup removes old v8 `/root/ipa_stabilize_backups` residue.
- Cleanup clears volatile dedupe backups in `/tmp`.
- Cleanup removes old install staging folders, including `/foxusr/qcmap-stage2`
  and old extracted v6/v7/v8 bundle trees, unless a path contains the currently
  executing payload.
- Cleanup removes old `/data/coredump/core.*` files by default.
- Cleanup trims temporary custom logs.
- Cleanup logs before/after `df` snapshots.

## Cleanup controls

Defaults:

```sh
CLEAN_INSTALL_STAGING=1
CLEAN_COREDUMPS=1
```

Disable them only when debugging:

```sh
CLEAN_INSTALL_STAGING=0 CLEAN_COREDUMPS=0 sh ./install-full-stack-on-modem.sh
```

## Still included from v1.0.0

- Permanent QCMAP setup for Foxconn T99W373 locked / NON-ES modems.
- Stock boot image path; no `boot.img` is flashed by this release.
- Runtime PCIe enablement with `pcie_enabler_non_es_v1.ko`.
- Realtek / IPA Ethernet stack:
  `rmnet_eth.ko`, `r8125_stack.ko`, `ioss_rebuilt.ko`,
  `r8125_ioss_rebuilt.ko`.
- Final IPA UL/DL userspace fix:
  `libipacm_abi_bridge_final.so`, patched `ipacm.service`,
  `ipa-iptables-dedupe.timer`, `ipa-stack-healthcheck.timer`,
  and `/usr/bin/ipa-ul-final-status.sh`.
- T99W373 SimpleAdmin WebUI, version `Simple T99373-1.0.2B`.
- WebUI control for subnet/DHCP settings, custom TTL, automatic reboot,
  connection watchdog and Tailscale.
- Optional Dropbear SSH server installation.
- Optional `btop` installation.

The bundle does not configure the APN.

## Install summary

```sh
adb push qcmap-modem-bundle-v9-NON-ES-rx1024.tar /foxusr/qcmap-modem-bundle-v9.tar
adb shell 'rm -rf /foxusr/qcmap-install && mkdir -p /foxusr/qcmap-install && tar -C /foxusr/qcmap-install -xf /foxusr/qcmap-modem-bundle-v9.tar && cd /foxusr/qcmap-install/qcmap-modem-bundle-v9-NON-ES-rx1024 && ROOT_PASSWORD=123 ENABLE_WEB_CLIENT=1 sh ./install-full-stack-on-modem.sh'
```

## Live validation

The hotfix was tested on a modem that had hit the rootfs-full failure:

- PCIe and Realtek/IOSS were confirmed healthy.
- Corrupted `/etc/data` XML files were restored from `/systemrw/data`.
- QCMAP, `netmgrd`, `dnsmasq`, WebUI, Tailscale and watchdog recovered.
- Default route returned through `rmnet_data0`.
- Ping and HTTP navigation worked.
- After a timer cycle, rootfs usage stayed stable and no `/root` backup
  directory was recreated.
