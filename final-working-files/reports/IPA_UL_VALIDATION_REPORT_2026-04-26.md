# IPA UL validation report - 2026-04-26

## Ambiente test

- Host path: `/home/manu/Scrivania/linaro 5.4`
- Modem via ADB: `4f2ad17a`
- Host interface: `enx00e04c6802a5`
- Host IP: `192.168.225.54/22`
- Gateway modem: `192.168.225.1`
- WAN post reboot: `rmnet_data0`, mux `1`
- Server iperf3: `5.182.48.12:5201`
- Patch attiva: `/usr/lib/libipacm_abi_bridge_final.so`

## Stato servizi post reboot

```text
pcie-enabler.service: active
ipacm.service: active
QCMAP_ConnectionManagerd.service: active
netmgrd.service: active
qcmap-radio-on.service: active
ipa-iptables-dedupe.timer: active
ipacm MainPID: 1146
ipacm NRestarts: 0
```

## Evidenza ABI bridge

```text
LD_PRELOAD=/usr/lib/libipacm_abi_bridge_final.so
IPA_ABI_BRIDGE_MODE=translate
IPA_ABI_BRIDGE_ALLOW_IFACES=eth0,rmnet_data0,rmnet_data1
IPA_ABI_BRIDGE_RT_FLT_TRANSLATE=1
```

Log ABI finale:

```text
QUERY_TX if=rmnet_data0 action=translate_old_to_new hdr=dmux_hdr_v4_1 dst=0x23
QUERY_RX if=rmnet_data0 action=translate_old_to_new src=0x22
QUERY_EXT if=rmnet_data0 mux=1
ADD_RT translated ret=0x00000000
ADD_FLT translated ret=0x00000000
ADD_FLT_V2 translated ret=0x00000000
GENERATE_FLT_EQ translated ret=0x00000000
```

## Evidenza IPACM WAN/UL

```text
Setting up QMAP ID 1.
TriggerWANUp() if_name:rmnet_data0, ipv4_address:0xac79a2b mux_id:1, xlat_mux_id:0
handle_uplink_filter_rule() flt_index: src pipe: 8, num of rules: 4, ebd pipe: 2, mux id: 1
handle_uplink_filter_rule() this is the first PDN for dev eth0, commiting modem UL rules, mux 0
AddEntry() Added rule(...) successfully handle (...)
```

## UL run-by-run

Artefatti:

```text
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/ul_v5_active_iperf3_2026-04-26_15-02-13/
```

| run_id | sent_bytes | sent_bps | sw_tx_delta | hw_tx_delta | rtk_cons_ring_delta | rtk_prod_ring_delta | SW_FILT_delta | NAT_delta | INVALID_PIPE_delta | HDRI_delta | CSUM_delta | pid_before | pid_after | nrestarts_after | esito |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 1 | 9306112 | 3722329 | 0 | 7 | 5301 | 5301 | 2 | 5 | 0 | 0 | 0 | 1146 | 1146 | 0 | PASS |
| 2 | 7995392 | 3198005 | 0 | 5 | 5451 | 5451 | 5 | 3 | 0 | 0 | 0 | 1146 | 1146 | 0 | PASS |
| 3 | 7864320 | 3145530 | 0 | 6 | 4806 | 4806 | 2 | 4 | 0 | 0 | 0 | 1146 | 1146 | 0 | PASS |

## UL counter-scope test

Artefatti:

```text
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/ul_counter_scope_2026-04-26_15-00-20/
```

Risultato:

```text
sent_bytes=8781824
sw_tx_delta=0
hw_tx_delta=5
RTK CONS RingUtilCount delta=5625
RTK PROD RingUtilCount delta=5625
INVALID_PIPE/HDRI/CSUM delta=0
ipacm rc=0, no restart
```

Interpretazione: il traffico UL passa nel datapath RTK/IPA; `hw_tx` globale non e proporzionale post reboot, mentre i ring RTK crescono coerentemente.

## DL post reboot

Artefatti:

```text
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/dl_counter_scope_2026-04-26_15-04-17/
```

Risultato:

```text
HTTP 200
file_bytes=50000000
time=3.274383s
ipacm MainPID=1146
ipacm NRestarts=0
sw_tx_delta=0
INVALID_PIPE/HDRI/CSUM delta=0
```

## Stato finale dopo test

```text
SERVICES: all active
ipacm MainPID=1146
ipacm NRestarts=0
LD_PRELOAD=/usr/lib/libipacm_abi_bridge_final.so
NAT: 1 MASQUERADE on rmnet_data0
sw_tx=0
INVALID_PIPE=0
HDRI=0
CSUM=0
IPA_CLIENT_RTK_ETHERNET_CONS_RingUtilCount present and increasing under traffic
IPA_CLIENT_RTK_ETHERNET_PROD_RingUtilCount present and increasing under traffic
```

## Esito

PASS operativo post reboot.

La patch finale mantiene navigazione, DL e UL attivi senza crash nei test ripetuti. Il criterio di validazione UL definitivo deve includere i counter RTK/IPA, non solo `hw_tx` globale.
