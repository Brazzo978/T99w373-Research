# IPA UL - Test report ABI bridge preload (2026-04-25)

## Artefatti

Cartella principale test:

```text
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/ul_abi_bridge_test_2026-04-25_20-31-51
```

File sorgente/preload creati:

```text
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/ipacm_abi_bridge_preload.c
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/libipacm_abi_bridge_v1.so
```

Probe creati/aggiornati:

```text
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/query_iface_props_probe.c
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/query_iface_props_probe_02300.static
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/query_iface_props_probe_13900.static
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/query_iface_raw_probe.c
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/query_iface_raw_probe_13900.static
```

## Stato baseline prima del test

Dopo reboot manuale:

```text
servizi inizialmente in assestamento, poi active x5
moduli presenti: rmnet_eth, r8125, ioss, r8125_ioss
route WAN via rmnet_data0
nessun LD_PRELOAD attivo
IPACM_cfg.xml reale: eth0 Category ODU
```

DL e LAN post assestamento:

```text
ping 192.168.225.1: OK
curl DL 1024 byte via enx00e04c6802a5: HTTP 200
```

## Test 1 - Probe ABI senza modifiche

### Header 13900

Risultato non coerente:

```text
eth0 tx[0] dst=1987078495 hdr=''
rmnet_data0 tx[0] dst=1919182943 alt=1597273695 hdr='1'
```

Interpretazione:

```text
1987078495 = 0x7670695f = "_ipv"
1919182943 = 0x7264685f = "_hdr"
1597273695 = 0x5f34765f = "_v4_"
```

Questi non sono pipe, sono frammenti ASCII di nomi header letti all'offset sbagliato.

### Header 02300

Risultato coerente:

```text
eth0:
  tx[0] ip=0 dst=109 hdr='eth0_ipv4' l2=1
  tx[1] ip=1 dst=109 hdr='eth0_ipv6' l2=1
  rx[0] ip=0 src=108 l2=1
  rx[1] ip=1 src=108 l2=1

rmnet_data0:
  tx[0] ip=0 dst=35 hdr='dmux_hdr_v4_1'
  tx[1] ip=1 dst=35 hdr='dmux_hdr_v4_1'
  rx[0] ip=0 src=34 attrib=0x4000
  rx[1] ip=1 src=34 attrib=0x4000
  ext props=9 mux_id=1
```

Esito: PASS diagnostico. Il layout kernel runtime e' `02300`.

## Test 2 - ABI bridge solo con restart `ipacm`

Install temporanea:

```sh
LD_PRELOAD=/usr/lib/libipacm_abi_bridge_v1.so
ExecStart=/usr/bin/ipacm-abi-bridge-test.sh
```

Log ABI bridge:

```text
TX_XLAT[eth0]0x00000002
RX_XLAT[eth0]0x00000002
```

Log IPACM dopo traduzione:

```text
Tx(0): ip-type: 0, dst_pipe: 109, header: eth0_ipv4
Tx(1): ip-type: 1, dst_pipe: 109, header: eth0_ipv6
Rx(0): ip-type: 0, src_pipe: 108
Rx(1): ip-type: 1, src_pipe: 108
```

Dopo traffico host reale, IPACM crea header validi:

```text
ODU v4 full header name:35_IPACM_ODU_v4 header handle:(0x23)
ODU v6 full header name:35_IPACM_ODU_v6 header handle:(0x24)
eth-client(0) v4 full header name:35_IPACM_ETH_v4_0 header handle:(0x25)
eth-client(0) v6 full header name:35_IPACM_ETH_v6_0 header handle:(0x26)
client(0): v4 header handle:(0x25)
```

DL check:

```text
ping gateway: OK
curl DL 1 MB: HTTP 200
```

Upload 10 MB:

| test | http | size_upload | time | speed_upload | hw_tx_delta | sw_tx_delta | SW_FILT_delta | WAN_UPSTREAM_ROUTE_ADD | esito |
|---|---:|---:|---:|---:|---:|---:|---:|---|---|
| ul_run_1_after_abi_bridge | 200 | 10485760 | 9.428240s | 1112165 B/s | +7918 | 0 | +7918 | W:0 R:0 | PARZIALE |

Interpretazione: traffico IP passa e DL resta OK, ma non e' ancora UL HW 100% perche' `WAN_UPSTREAM_ROUTE_ADD` resta zero e `SW_FILT` cresce in modo dominante.

## Test 3 - Restart QCMAP/netmgr/qcmap-radio-on con ABI bridge attivo

Scopo: far rigenerare anche il lato WAN/QMAP con traduzione ABI attiva.

Log ABI bridge:

```text
TX_XLAT[rmnet_data0]0x00000002
RX_XLAT[rmnet_data0]0x00000002
TX_XLAT[rmnet_data1]0x00000002
RX_XLAT[rmnet_data1]0x00000002
TX_XLAT[rmnet_data1]0x00000002
RX_XLAT[rmnet_data1]0x00000002
```

Log IPACM WAN corretto:

```text
Tx(0): ip-type: 0, dst_pipe: 35, header: dmux_hdr_v4_2
Tx(1): ip-type: 1, dst_pipe: 35, header: dmux_hdr_v4_2
Rx(0): ip-type: 0, src_pipe: 34
Rx(1): ip-type: 1, src_pipe: 34
Setting up QMAP ID 2
TriggerWANUp() if_name:rmnet_data1, ipv4_address:0xa8deb33 mux_id:2, xlat_mux_id:0
```

Questa e' una correzione reale rispetto al vecchio:

```text
TriggerWANUp() ipv4_address=0x0
```

Regressione rilevata:

```text
ping 192.168.225.1: 100% packet loss
curl DL 1 MB: timeout
```

Esito: FAIL operativo. Il test porta informazioni ottime, ma non e' deployabile perche' rompe temporaneamente la LAN/navigazione.

## Ripristino finale

Azioni eseguite:

```sh
rm -f /etc/systemd/system/ipacm.service.d/31-abi-bridge-test.conf
rm -f /usr/bin/ipacm-abi-bridge-test.sh
rm -f /usr/lib/libipacm_abi_bridge_v1.so
systemctl daemon-reload
systemctl restart ipacm.service QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service
```

Verifica finale:

```text
servizi active x5
nessun LD_PRELOAD in ambiente ipacm
route WAN via rmnet_data1
ping 192.168.225.1 OK
curl DL 1024 byte HTTP 200
```

## Decisione

Il fix UL non va ancora applicato in modo permanente.

La direzione corretta pero' e' stata identificata:

1. non toccare Realtek/IOSS/rmnet_eth;
2. correggere compatibilita' ABI tra kernel IPA layout `02300` e userspace IPACM layout nuovo;
3. tenere la traduzione separata per LAN e WAN;
4. capire perche' il ciclo WAN/QCMAP con campi finalmente corretti causa regressione LAN;
5. solo dopo riprovare `WAN_UPSTREAM_ROUTE_ADD`/UL HW.

## Prossimo passo tecnico consigliato

Preparare una `v2` ancora piu' controllata con feature flags runtime, ad esempio:

```text
IPA_ABI_BRIDGE_ETH=1
IPA_ABI_BRIDGE_RMNET=0/1
IPA_ABI_BRIDGE_LOG_ONLY=1
```

In questo modo si puo' validare separatamente:

- solo `eth0` client/header path, che oggi e' risultato sicuro;
- solo `rmnet_dataX` WAN path, che oggi corregge `TriggerWANUp` ma puo' rompere LAN;
- eventuale correlazione con `IPA_HANDLE_WAN_UP` e installazione filtri UL.

