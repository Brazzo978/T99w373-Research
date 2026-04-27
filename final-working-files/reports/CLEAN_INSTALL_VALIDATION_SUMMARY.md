# NON-ES Locked Clean Install Validation

Date: 2026-04-27
Bundle: /home/manu/Scrivania/qcmap/dist/qcmap-modem-bundle-v3-NON-ES/qcmap-modem-bundle-v3-NON-ES.tar
SHA256: 1c96b31177291cdf3140c9f06d15ca131164bde8f4fc68eccfb93e119d15f3d6
Run folder: /home/manu/Scrivania/linaro 5.4/load_ipa_NON_ES/clean_install_validation_2026-04-27_20-18-46

## Install

- Installed from clean modem via bundle tar only.
- Installer completed and triggered reboot.
- Post-install boot grace: 90s.

## Post-Install Status

- Services: 8/8 active.
- pcie-enabler: active/exited, Result=success.
- ipacm: active/running, NRestarts=0.
- Modules loaded: pcie_enabler_non_es_v1, rmnet_eth, r8125, ioss, r8125_ioss.
- WAN route present via rmnet_data0.
- LAN route present: 192.168.225.0/24 via bridge0.
- WebUI HTTP: 200.
- NAT MASQUERADE present exactly on rmnet_data0.

## DL Test

Artifact: /home/manu/Scrivania/linaro 5.4/load_ipa_NON_ES/clean_install_validation_2026-04-27_20-18-46/dl_50mb/result_summary.txt

file_bytes=50000000
curl_exit=0
http_code=200
hw_tx_delta=4
sw_tx_delta=0
SW_FILT_delta=1
INVALID_PIPE_delta=0
HDRI_delta=0
CSUM_delta=0
services_active=yes
esito=PASS

## UL Test

Artifact: /home/manu/Scrivania/linaro 5.4/load_ipa_NON_ES/clean_install_validation_2026-04-27_20-18-46/ul_iperf_binddev_15s/result_summary.txt

Important host-side note: iperf/nc without SO_BINDTODEVICE followed the host default route via enp4s0. Valid UL test used iperf3 --bind-dev enx00e04c6802a5.

host_ip=192.168.225.54
iperf_rc=0
wall_duration_s=15.843
iperf_duration_s=15.000801
sent_bytes=4063232
sent_bps=2166941
hw_tx_delta=9
sw_tx_delta=0
SW_FILT_delta=2
INVALID_PIPE_delta=0
HDRI_delta=0
CSUM_delta=0
rtk_cons_ring_delta=0
rtk_prod_ring_delta=0
pid_before=874
pid_after=874
nrestarts_before=0
nrestarts_after=0
irq_deltas=115:eth0-0:1:0.07/s,131:eth0-16:13:0.87/s
esito=PASS

## Second Reboot Persistence

Artifact: /home/manu/Scrivania/linaro 5.4/load_ipa_NON_ES/clean_install_validation_2026-04-27_20-18-46/second_reboot_status.txt

- ADB returned after reboot.
- Post-reboot grace: 90s.
- Services: 8/8 active.
- ipacm: NRestarts=0.
- Modules loaded automatically.
- IPA ETH RTK pipes present.
- IPA error counters INVALID_PIPE/HDRI/CSUM all zero.

## Verdict

PASS: the NON-ES locked bundle installs from clean state and brings the modem to working IPA DL/UL state with persistent boot wiring.
