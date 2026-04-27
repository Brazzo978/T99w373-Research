# IPA UL stable final - 2026-04-26

## Scopo

Portare l'upload IPA da fallback software / path instabile a datapath hardware stabile, senza rompere:

- navigazione LAN via `eth0`/`bridge0`
- download IPA gia funzionante
- boot persistente dei moduli PCIe/Realtek/IOSS
- servizi modem/QCMAP/netmgrd

## Stato finale attuale

Configurazione finale attiva e verificata post reboot:

- `ipacm.service` parte con ABI bridge finale via `LD_PRELOAD`.
- `pcie-enabler.service`, `ipacm.service`, `QCMAP_ConnectionManagerd.service`, `netmgrd.service`, `qcmap-radio-on.service` sono `active`.
- `ipacm` e stabile: PID post reboot `1146`, `NRestarts=0` durante tutti i test.
- route WAN attiva su `rmnet_data0`.
- LAN host attiva su `bridge0`/`eth0`, host `192.168.225.54`, gateway `192.168.225.1`.
- NAT deduplicato: una sola regola `MASQUERADE` su `rmnet_data0`.
- UL iperf3 pubblico funzionante e ripetibile.
- DL 50 MB funzionante post reboot.

## Componente finale installato

File persistenti sul modem:

```text
/usr/lib/libipacm_abi_bridge_final.so
/etc/systemd/system/ipacm.service.d/31-abi-bridge-final.conf
/usr/bin/ipa-iptables-dedupe.sh
/etc/systemd/system/ipa-iptables-dedupe.service
/etc/systemd/system/ipa-iptables-dedupe.timer
/usr/bin/ipa-ul-final-status.sh
```

Backup installazione finale:

```text
/root/ipa_ul_final_backups/20260426_144720
```

Drop-in finale `ipacm`:

```ini
[Service]
Environment=IPA_ABI_BRIDGE_MODE=translate
Environment=IPA_ABI_BRIDGE_ALLOW_IFACES=eth0,rmnet_data0,rmnet_data1
Environment=IPA_ABI_BRIDGE_TRANSLATE_IFACES=eth0,rmnet_data0,rmnet_data1
Environment=IPA_ABI_BRIDGE_TX=1
Environment=IPA_ABI_BRIDGE_RX=1
Environment=IPA_ABI_BRIDGE_EXT=1
Environment=IPA_ABI_BRIDGE_RT_FLT_LOG=1
Environment=IPA_ABI_BRIDGE_RT_FLT_TRANSLATE=1
Environment=IPA_ABI_BRIDGE_FAIL_CLOSED=1
Environment=IPA_ABI_BRIDGE_HEXDUMP_ONCE=0
Environment=IPA_ABI_BRIDGE_LOG=/tmp/ipacm_abi_bridge_final.log
Environment=LD_PRELOAD=/usr/lib/libipacm_abi_bridge_final.so
```

## Root cause risolta

Il problema UL non era nei moduli Realtek/IOSS/rmnet_eth. Quei moduli avevano gia prodotto DL stabile.

La causa tecnica era un mismatch ABI tra userspace IPACM e kernel IPA:

- IPACM leggeva strutture IPA con layout sbagliato.
- Sintomo forte: campo numerico `dst_pipe` letto come `0x7670695f`, ASCII `_ipv`, cioe una stringa letta come intero.
- Il log `Failed to get QMAP header` era conseguenza dello stesso problema: IPACM leggeva `hdr_name`/pipe/attributi a offset errati.
- Il path route/filter V2 richiedeva traduzione ABI, altrimenti `ADD_FLT_V2` falliva o programmava regole non coerenti.

Il bridge finale traduce:

- `QUERY_INTF_TX_PROPS`
- `QUERY_INTF_RX_PROPS`
- proprieta `EXT` rmnet/eth
- route/filter ioctl, inclusi `ADD_RT`, `ADD_FLT`, `ADD_FLT_V2`, `ADD_FLT_AFTER`, `GENERATE_FLT_EQ`

## Evidenza IPACM post reboot

Dal log live post reboot:

```text
Setting up QMAP ID 1.
TriggerWANUp() if_name:rmnet_data0, ipv4_address=0xac79a2b mux_id:1, xlat_mux_id:0
handle_uplink_filter_rule() flt_index: src pipe: 8, num of rules: 4, ebd pipe: 2, mux id: 1
handle_uplink_filter_rule() this is the first PDN for dev eth0, commiting modem UL rules, mux 0
AddEntry() Added rule(...) successfully handle (...)
```

Questo dimostra che dopo reboot il path WAN/UL e arrivato fino a:

- QMAP ID corretto su `rmnet_data0`
- `TriggerWANUp` con IPv4 non nullo
- regole UL per `eth0`
- entry NAT hardware per i flussi testati

## Nota importante sui counter

Dopo reboot il vecchio counter globale `hw_tx` non cresce piu in modo proporzionale al traffico, anche quando IPACM installa correttamente QMAP/NAT/UL rules.

Per validare UL adesso usare insieme:

- `sw_tx_delta = 0`
- `INVALID_PIPE/HDRI/CSUM delta = 0`
- `ipacm` PID stabile e `NRestarts=0`
- log IPACM con `Setting up QMAP ID`, `TriggerWANUp`, `handle_uplink_filter_rule`, `Added rule successfully`
- crescita dei counter RTK/IPA:
  - `IPA_CLIENT_RTK_ETHERNET_CONS_RingUtilCount`
  - `IPA_CLIENT_RTK_ETHERNET_PROD_RingUtilCount`

Durante UL 20s i ring RTK sono cresciuti di circa `+4800..+5450` per run, mentre `sw_tx` e rimasto a zero.

## Check rapido operativo

```bash
adb shell '/usr/bin/ipa-ul-final-status.sh'
```

Cose attese:

- tutti i servizi `active`
- `LD_PRELOAD=/usr/lib/libipacm_abi_bridge_final.so`
- `NRestarts=0`
- una sola regola `MASQUERADE` per WAN attiva
- `sw_tx=0`
- errori `INVALID_PIPE`, `HDRI`, `CSUM` a zero
- counter `IPA_ETH_RTK` presenti

## Test UL ripetibile

```bash
cd '/home/manu/Scrivania/linaro 5.4'
SSHPASS='<password server>' RESET_REMOTE_IPERF=1 SERVER=5.182.48.12 PORT=5201 RUNS=3 DURATION=20 ./load_ipa_ES/test_ipa_ul_active_iperf3.sh
```

Criterio PASS UL pratico:

- `rc=0`
- `sent_bytes > 0`
- `sw_tx_delta = 0`
- `rtk_cons_ring_delta > 0`
- `rtk_prod_ring_delta > 0`
- `INVALID_PIPE_delta = 0`
- `HDRI_delta = 0`
- `CSUM_delta = 0`
- `pid_before = pid_after`
- `nrestarts_after = 0`

## Test DL ripetibile

```bash
curl -4 --interface enx00e04c6802a5 --max-time 120 -L \
  'https://speed.cloudflare.com/__down?bytes=50000000' \
  -o /tmp/dl_50mb.bin
```

Criterio PASS DL:

- file da `50000000` byte
- HTTP 200
- navigazione non si interrompe
- `ipacm` non crasha
- errori IPA `INVALID_PIPE/HDRI/CSUM` a zero

## Problemi risolti durante stabilizzazione

- Crash/instabilita con traduzioni ABI troppo larghe: risolto con allowlist `eth0,rmnet_data0,rmnet_data1` e fail-closed.
- `Failed to get QMAP header`: risolto traducendo correttamente TX/RX props e header QMAP.
- `ADD_FLT_V2 ret=-1`: risolto traducendo layout route/filter V2.
- Duplicazione iptables QCMAP: risolta con `ipa-iptables-dedupe.timer`.
- `nf_nat_setup_info` warning da duplicati NAT: mitigato deduplicando regole `MASQUERADE`/DROP ripetute.
- Post reboot su `rmnet_data0` invece che `rmnet_data1`: gestito dalla allowlist e dalla traduzione EXT/mux per entrambe.

## Limitazioni note

- La soluzione finale operativa e un ABI bridge via `LD_PRELOAD`. Per questo firmware e stabile nei test, ma la soluzione sorgente ideale resta ricompilare IPACM contro gli header IPA esatti del kernel target o integrare la compat ABI direttamente in IPACM.
- Il counter globale `hw_tx` non e piu un indicatore sufficiente post reboot; usare i counter RTK/IPA e i log IPACM come criterio principale.
- Se si stacca fisicamente `eth0`, il client sparisce e il traffico si ferma: e comportamento atteso, non prova di crash IPA.

## File sorgenti/locali importanti

```text
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/ipacm_abi_bridge_v5.c
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/libipacm_abi_bridge_v5.so
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/final_ul_stage/libipacm_abi_bridge_final.so
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/install_ipa_ul_final.sh
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/test_ipa_ul_active_iperf3.sh
```

## Stato finale sintetico

La navigazione e attiva, DL e attivo, UL e stabile nei test ripetuti. Il fix finale e caricato in modo persistente e sopravvive al reboot.
