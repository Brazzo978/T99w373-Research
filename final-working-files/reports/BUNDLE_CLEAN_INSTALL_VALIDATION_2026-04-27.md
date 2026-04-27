# Bundle clean install validation - 2026-04-27

## Bundle tested

```text
/home/manu/Scrivania/qcmap/dist/qcmap-modem-bundle-v3-ES/qcmap-modem-bundle-v3-ES.tar
```

## Clean install result

Install command completed from a clean modem and rebooted automatically.

During clean-install validation a boot-order packaging issue was found and fixed:

- Symptom: `ipacm.service` could fail during early boot before traffic tests.
- Root cause: the stock `ipacm.service` used `/usrdata/ipacm_log.txt` from `StandardOutput` and also removed that file in `ExecStartPre`; during early boot this path can be unavailable or not safely writable.
- Final fix added to bundle: install a safe final `/lib/systemd/system/ipacm.service` with ABI bridge environment built in, non-fatal `/usrdata` log cleanup, and `StandardOutput=null`.
- The old ABI bridge drop-in is no longer required for boot; the preload environment is integrated directly into the final `ipacm.service`.

## Final subnet policy

```text
LAN: 192.168.225.0/24
modem: 192.168.225.1
DHCP: 192.168.225.20-192.168.225.170
mobileap SubNetMask: 255.255.255.0
IPACM SubnetAddress: 192.168.225.0
IPACM SubnetMask: 255.255.255.0
```

## Post-fix service status after normal reboot

After normal `adb reboot` and waiting for QCMAP/radio retry loop:

```text
pcie-enabler.service: active
ipacm.service: active
QCMAP_ConnectionManagerd.service: active
netmgrd.service: active
qcmap-radio-on.service: active
ipa-iptables-dedupe.timer: active
ipa-stack-healthcheck.timer: active
qcmap_httpd.service: active
ipacm MainPID: 883
ipacm NRestarts: 0
DropInPaths: empty
StandardOutput: null
LD_PRELOAD=/usr/lib/libipacm_abi_bridge_final.so
```

Route:

```text
default via 10.140.181.161 dev rmnet_data0
192.168.225.0/24 dev bridge0 src 192.168.225.1
```

NAT:

```text
1x -A POSTROUTING -o rmnet_data0 -j MASQUERADE --random
```

## WebUI and LAN smoke test

Host -> modem ping:

```text
3 packets transmitted, 3 received, 0% packet loss
```

WebUI:

```text
HTTP/1.0 200 OK
Content-Length: 108145
```

## Watchdog validation

The bundle includes a conservative watchdog:

```text
/usr/bin/ipa-stack-healthcheck.sh
/etc/systemd/system/ipa-stack-healthcheck.service
/etc/systemd/system/ipa-stack-healthcheck.timer
```

Policy:

```text
OnBootSec=90s
OnUnitActiveSec=60s
Only restart failed/inactive units
Do not restart healthy services
Run IPA/QCMAP iptables dedupe idempotently
Log: /tmp/ipa-stack-healthcheck.log
```

Live tests before repacking:

| test | action | result |
|---|---|---|
| healthy stack | ran `/usr/bin/ipa-stack-healthcheck.sh` with all services active | PASS: no service restart, `ipacm` PID unchanged |
| WebUI recovery | stopped `qcmap_httpd.service`, then ran healthcheck | PASS: `qcmap_httpd.service` restored to active |
| IPACM recovery | stopped `ipacm.service`, then ran healthcheck | PASS: `ipacm` restored to active, QCMAP/netmgr/radio stayed active, route `/24` preserved |
| post-recovery traffic | WebUI HEAD + 1MB download | PASS: HTTP 200, `sw_tx=0`, IPA errors zero |

Evidence after IPACM recovery:

```text
old ipacm PID: 883
new ipacm PID: 2917
NRestarts: 0
services: active active active active active active active active
route: 192.168.225.0/24 dev bridge0 src 192.168.225.1
```

## DL 50 MB after bundle install

Artifact directory:

```text
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/bundle_clean_v3es_dl_2026-04-27_18-48-09
```

Result:

```text
HTTP 200
file_bytes=50000000
time=3.025541s
sw_tx delta=0
hw_tx delta=4
SW_FILT delta=1
services_active=yes
no_reboot=yes
esito=PASS
```

## UL iperf3 after bundle install and reboot

Artifact directory:

```text
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/ul_v5_active_iperf3_2026-04-27_18-48-18
```

Result:

| run | rc | sent_bytes | sent_bps | received_bytes | received_bps | sw_tx_delta | hw_tx_delta | rtk_cons_ring_delta | rtk_prod_ring_delta | SW_FILT_delta | NAT_delta | INVALID_PIPE_delta | HDRI_delta | CSUM_delta | pid_before | pid_after | nrestarts_after | esito |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 1 | 0 | 18481152 | 9855811 | 17339696 | 8793661 | 0 | 7 | 3582 | 3582 | 2 | 5 | 0 | 0 | 0 | 883 | 883 | 0 | PASS |

## Final counters after DL+UL smoke

```text
sw_tx=0
hw_tx=15
INVALID_PIPE=0
HDRI=0
CSUM=0
IPA_CLIENT_RTK_ETHERNET_CONS_RingUtilCount=17302
IPA_CLIENT_RTK_ETHERNET_PROD_RingUtilCount=17302
```

## Final verdict

PASS.

The regenerated bundle installs the complete QCMAP + IPA UL/DL final configuration from clean state. After reboot, services recover automatically, WebUI responds, WAN comes up, LAN is `/24`, DL works, UL works, `ipacm` remains stable, `sw_tx` stays zero, hardware counters increase under traffic, and IPA error counters remain zero.
