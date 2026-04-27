# DL Stable Final - 2026-04-25

## Stato finale

Questa e' la baseline operativa stabile per IPA DL.

Obiettivo raggiunto: DL stabile e verificato al 100% secondo i criteri concordati.

UL HW resta non risolto. Non reintrodurre il preload ipacm o gli hack UL se non dentro un test isolato, reversibile e tracciato.

## Configurazione definitiva servizi

### Stack moduli

Il boot stack e' gestito da `pcie-enabler.service`, che esegue:

`/usr/bin/load-pcie-enabler.sh`

Ordine moduli definitivo:

1. `/moduli/rmnet_eth.ko`
2. `/moduli/r8125_rebuilt.ko`
3. `/moduli/ioss_rebuilt.ko`
4. `/moduli/r8125_ioss_rebuilt.ko`

Lo script porta anche `eth0` UP se presente, senza fallire se manca carrier.

### pcie-enabler.service

Caratteristiche importanti:

- `Type=oneshot`
- `RemainAfterExit=yes`
- `Before=qcmap-radio-on.service QCMAP_ConnectionManagerd.service`
- `ConditionPathExists` sui quattro moduli richiesti
- drop-in SELinux: `After=disable-selinux.service`, `Requires=disable-selinux.service`

### Dipendenze QCMAP

Drop-in persistenti presenti:

```text
/etc/systemd/system/qcmap-radio-on.service.d/10-pcie-enabler.conf
/etc/systemd/system/QCMAP_ConnectionManagerd.service.d/10-pcie-enabler.conf
```

Entrambi contengono:

```ini
[Unit]
Requires=pcie-enabler.service
After=pcie-enabler.service
```

### ipacm standard

`ipacm.service` deve partire standard con `/usr/bin/ipacm`.

Preload/hack UL disabilitati:

```text
/etc/systemd/system/ipacm.service.d/30-preload-fix.conf.disabled_2026-04-25
/usr/bin/ipacm-preload.sh.disabled_2026-04-25
/usr/lib/libipacm_fix.so.disabled_2026-04-25
```

## Check post reboot

Dopo reboot, attendere ADB e lasciare assestare `qcmap-radio-on` se necessario.

Comandi rapidi:

```sh
adb wait-for-device
adb shell 'systemctl is-active pcie-enabler.service ipacm.service QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service'
adb shell 'cat /proc/modules | grep -E "^(rmnet_eth|r8125|ioss|r8125_ioss) "'
adb shell 'ip route'
adb shell 'cat /sys/kernel/debug/ipa/stats | grep -E "sw_tx=|hw_tx=|lan_rx_excp\\[6"'
adb shell 'cat /sys/kernel/debug/ipa/msg | grep -E "WAN_UPSTREAM_ROUTE_ADD|WAN_UPSTREAM_ROUTE_DEL"'
```

PASS post reboot:

- tutti e 5 i servizi risultano `active`
- moduli presenti: `rmnet_eth`, `r8125`, `ioss`, `r8125_ioss`
- route WAN presente via `rmnet_data0` o `rmnet_data1`
- route LAN presente su `bridge0` `192.168.224.0/22`
- `sw_tx=0` prima dei test DL

Nota pratica: se l'interfaccia host e' DOWN, il modem puo' mostrare `bridge0 linkdown` e la route WAN/LAN puo' non essere utile per il test. Portare su il link host:

```sh
sudo ip link set enx00e04c6802a5 up
ip -br addr show enx00e04c6802a5
ethtool enx00e04c6802a5
```

Atteso: IP `192.168.225.x/22`, link detected `yes`, 1000Mb/s full duplex.

## Comando test DL manuale

Comando base concordato:

```sh
curl -4 --interface enx00e04c6802a5 --max-time 120 -L 'https://speed.cloudflare.com/__down?bytes=50000000' -o down_50mb.bin
```

Per validare IPA, salvare sempre before/after:

```sh
adb shell 'cat /sys/kernel/debug/ipa/stats' > ipa_stats_before.txt
adb shell 'cat /sys/kernel/debug/ipa/msg' > ipa_msg_before.txt
adb shell 'systemctl is-active pcie-enabler.service ipacm.service QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service' > services_before.txt

curl -4 --interface enx00e04c6802a5 --max-time 120 -L 'https://speed.cloudflare.com/__down?bytes=50000000' -o down_50mb.bin

adb shell 'cat /sys/kernel/debug/ipa/stats' > ipa_stats_after.txt
adb shell 'cat /sys/kernel/debug/ipa/msg' > ipa_msg_after.txt
adb shell 'systemctl is-active pcie-enabler.service ipacm.service QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service' > services_after.txt
```

## Script riutilizzabile

Script creato:

`/home/manu/Scrivania/linaro 5.4/load_ipa_ES/test_ipa_dl_stability.sh`

Uso standard:

```sh
"/home/manu/Scrivania/linaro 5.4/load_ipa_ES/test_ipa_dl_stability.sh"
```

Variabili utili:

```sh
IFACE=enx00e04c6802a5 RUNS=3 BYTES=50000000 MAX_TIME=120 \
  "/home/manu/Scrivania/linaro 5.4/load_ipa_ES/test_ipa_dl_stability.sh"
```

Output:

- folder timestamped `dl_validation_YYYY-MM-DD_HH-MM-SS/`
- `summary.csv`
- `summary.txt`
- `run_N/result_summary.txt`
- snapshot before/after per stats IPA, msg IPA, servizi, moduli, route, dmesg e host iface

## Criteri PASS/FAIL DL

PASS DL se tutte le condizioni sono vere:

- file scaricato completo: `50000000` byte
- `hw_tx_delta > 0` significativo
- `sw_tx_delta = 0`
- `curl_exit = 0`
- HTTP `200`
- servizi ancora `active`
- nessun reboot anomalo durante la run

FAIL DL se una di queste condizioni accade:

- file incompleto o curl timeout
- `hw_tx_delta = 0`
- `sw_tx_delta > 0`
- servizi non attivi dopo la run
- reboot/crash durante la run

Non blocca il PASS DL:

- crescita di `lan_rx_excp[6:IPAHAL_PKT_STATUS_EXCEPTION_SW_FILT]`, perche' rappresenta componente UL/ACK software attesa nello stato attuale

## Risultato validazione del 2026-04-25

Report:

`/home/manu/Scrivania/linaro 5.4/load_ipa_ES/DL_VALIDATION_REPORT_2026-04-25.md`

Artefatti DL:

`/home/manu/Scrivania/linaro 5.4/load_ipa_ES/dl_validation_2026-04-25_19-41-44/`

Artefatti reboot:

`/home/manu/Scrivania/linaro 5.4/load_ipa_ES/reboot_validation_2026-04-25_19-33-24/`

Sintesi run:

| run_id | file_bytes | hw_tx_delta | sw_tx_delta | SW_FILT_delta | esito |
|---:|---:|---:|---:|---:|---|
| 1 | 50000000 | 4508 | 0 | 4512 | PASS |
| 2 | 50000000 | 4779 | 0 | 4779 | PASS |
| 3 | 50000000 | 4862 | 0 | 4862 | PASS |

## Troubleshooting rapido

### Host interface DOWN

Sintomi:

- `ip -br addr show enx00e04c6802a5` mostra `DOWN`
- `ethtool enx00e04c6802a5` mostra `Link detected: no`
- modem route `bridge0 linkdown`

Fix reversibile:

```sh
sudo ip link set enx00e04c6802a5 up
```

Poi ricontrollare link e IP.

### Servizi non subito active

Aspettare il retry loop di `qcmap-radio-on`, poi rivalutare:

```sh
sleep 20
adb shell 'systemctl is-active pcie-enabler.service ipacm.service QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service'
```

Se persistono problemi, salvare:

```sh
adb shell 'systemctl status --no-pager pcie-enabler.service ipacm.service QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service'
adb shell 'journalctl -b --no-pager -u pcie-enabler.service -u ipacm.service -u QCMAP_ConnectionManagerd.service -u netmgrd.service -u qcmap-radio-on.service | tail -n 220'
```

### Moduli mancanti

Controllare ordine e presenza file:

```sh
adb shell 'cat /proc/modules | grep -E "^(rmnet_eth|r8125|ioss|r8125_ioss) "'
adb shell 'ls -l /moduli/rmnet_eth.ko /moduli/r8125_rebuilt.ko /moduli/ioss_rebuilt.ko /moduli/r8125_ioss_rebuilt.ko'
```

### DL non offloadato

Se `sw_tx_delta > 0` o `hw_tx_delta = 0`:

- salvare folder completo dello script
- non riavviare subito, prima catturare `ipa/stats`, `ipa/msg`, route e dmesg
- verificare che `ipacm` sia standard, senza preload
- verificare che i drop-in QCMAP richiedano `pcie-enabler.service`

### UL HW

Stato attuale: UL HW non risolto.

Indizi storici ancora validi:

- `Failed to get QMAP header`
- `TriggerWANUp` con `ipv4_address=0x0`
- `WAN_UPSTREAM_ROUTE_ADD` nativo non affidabile per UL
- `SW_FILT` cresce sotto traffico host

Qualsiasi nuovo tentativo UL deve essere isolato e reversibile. Se non arriva a UL HW 100%, dichiarare esplicitamente: UL non risolto.
