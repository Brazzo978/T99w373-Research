# IPA UL - ABI bridge v2 matrix report (2026-04-25)

## Cartella artefatti

```text
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/abi_bridge_v2_test_2026-04-25_20-54-05
```

## File creati

```text
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/IPA_ABI_LAYOUT_MAP_2026-04-25.md
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/ipacm_abi_bridge_v2.c
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/libipacm_abi_bridge_v2.so
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/UL_ABI_BRIDGE_V2_MATRIX_REPORT_2026-04-25.md
```

## Obiettivo della v2

La v1 aveva dimostrato che tradurre il layout ABI vecchio `02300` nel layout nuovo atteso da `ipacm` corregge campi corrotti come:

```text
dst_pipe=0x7670695f = "_ipv"
```

Pero' la v1 traduceva troppo in blocco. La v2 separa:

- modalita' `log` / `translate`
- allowlist interfacce
- allowlist interfacce traducibili
- TX / RX / EXT separati
- log route/filter senza traduzione
- fail-closed in caso di layout ambiguo

## Variabili supportate dalla v2

```sh
IPA_ABI_BRIDGE_MODE=log|translate
IPA_ABI_BRIDGE_ALLOW_IFACES=eth0,rmnet_data0,rmnet_data1
IPA_ABI_BRIDGE_TRANSLATE_IFACES=eth0
IPA_ABI_BRIDGE_TX=1
IPA_ABI_BRIDGE_RX=1
IPA_ABI_BRIDGE_EXT=1
IPA_ABI_BRIDGE_RT_FLT_LOG=1
IPA_ABI_BRIDGE_FAIL_CLOSED=1
IPA_ABI_BRIDGE_HEXDUMP_ONCE=1
IPA_ABI_BRIDGE_LOG=/usrdata/ipacm_abi_bridge_v2.log
```

## Scoring layout

La v2 calcola due punteggi per ogni risposta `QUERY_INTF_TX_PROPS` / `RX_PROPS`:

- `old_score`: interpreta il buffer come layout `02300`
- `new_score`: interpreta il buffer come layout nuovo `13900`

Traduce solo se:

```text
mode=translate
iface in TRANSLATE_IFACES
old_score valido
new_score non valido
```

Se entrambi risultano validi, non traduce.

## Fase 1 - log-only, restart solo ipacm

Config:

```sh
IPA_ABI_BRIDGE_MODE=log
IPA_ABI_BRIDGE_ALLOW_IFACES=eth0,bridge0,rmnet_data0,rmnet_data1
IPA_ABI_BRIDGE_TRANSLATE_IFACES=
TX=1 RX=1 EXT=1
```

Risultato:

```text
servizi active
LAN ping OK
DL 1024 byte HTTP 200
nessuna traduzione
```

Evidenza log eth0:

```text
QUERY_TX if=eth0 n=2 old_score=10 new_score=6 threshold=8 action=log_only
TX_OLD eth0: dst=109 hdr=eth0_ipv4 / eth0_ipv6
TX_NEW eth0: dst=0x7670695f hdr vuoto
```

Conclusione: log-only e' sicuro e conferma il mismatch.

## Fase 2 - translate solo eth0 TX/RX, restart solo ipacm

Config:

```sh
IPA_ABI_BRIDGE_MODE=translate
IPA_ABI_BRIDGE_ALLOW_IFACES=eth0
IPA_ABI_BRIDGE_TRANSLATE_IFACES=eth0
TX=1 RX=1 EXT=0
```

Risultato:

```text
servizi active
LAN ping OK
DL 1 MB HTTP 200
rollback OK
```

IPACM vede finalmente eth0 coerente:

```text
Tx(0): ip-type: 0, dst_pipe: 109, header: eth0_ipv4
Tx(1): ip-type: 1, dst_pipe: 109, header: eth0_ipv6
Rx(0): ip-type: 0, src_pipe: 108
Rx(1): ip-type: 1, src_pipe: 108
```

Conclusione: traduzione `eth0` e' utile e operativamente sicura con restart solo IPACM.

## Fase 3 - eth0 translate + rmnet log-only, restart solo ipacm

Config:

```sh
IPA_ABI_BRIDGE_MODE=translate
IPA_ABI_BRIDGE_ALLOW_IFACES=eth0,rmnet_data0,rmnet_data1
IPA_ABI_BRIDGE_TRANSLATE_IFACES=eth0
TX=1 RX=1 EXT=1
```

Risultato:

```text
servizi active
LAN ping OK
DL 1024 byte HTTP 200
rmnet non viene reinterrogato con il solo restart ipacm
```

Conclusione: per osservare rmnet serve evento WAN/QCMAP, non basta restart solo `ipacm`.

## Fase 3b - log-only su rmnet, restart QCMAP/netmgr/radio

Config:

```sh
IPA_ABI_BRIDGE_MODE=log
IPA_ABI_BRIDGE_ALLOW_IFACES=eth0,rmnet_data0,rmnet_data1
IPA_ABI_BRIDGE_TRANSLATE_IFACES=
TX=1 RX=1 EXT=1
```

Risultato operativo:

```text
servizi active
route WAN via rmnet_data0
WAN_UPSTREAM_ROUTE_ADD ancora W:0 R:0
ping gateway inizialmente KO durante assestamento
DL 1024 byte HTTP 200 dopo circa 11 secondi
baseline tornata OK dopo attesa/rollback
```

Evidenza rmnet log-only:

```text
rmnet_data0 TX old:
  tx[0] ip=0 dst=35 hdr=dmux_hdr_v4_1
  tx[1] ip=1 dst=35 hdr=dmux_hdr_v4_1
rmnet_data0 TX new:
  dst=0x7264685f = "_hdr"
  alt=0x5f34765f = "_v4_"

rmnet_data1 TX old:
  tx[0] ip=0 dst=35 hdr=dmux_hdr_v4_2
  tx[1] ip=1 dst=35 hdr=dmux_hdr_v4_2
rmnet_data1 TX new:
  dst=0x7264685f = "_hdr"
  alt=0x5f34765f = "_v4_"
```

EXT risulta coerente e non richiede traduzione layout:

```text
rmnet_data0 ext mux_id=1
rmnet_data1 ext mux_id=2
```

Conclusione: rmnet TX e' sicuramente affetto dallo stesso mismatch. EXT invece ha layout uguale tra 02300 e 13900.

## Fase 4 - translate solo rmnet TX, RX/EXT spenti, restart QCMAP/netmgr/radio

Config:

```sh
IPA_ABI_BRIDGE_MODE=translate
IPA_ABI_BRIDGE_ALLOW_IFACES=rmnet_data0,rmnet_data1
IPA_ABI_BRIDGE_TRANSLATE_IFACES=rmnet_data0,rmnet_data1
TX=1 RX=0 EXT=0
```

Risultato tecnico positivo:

```text
rmnet_data1 Tx corretto:
  dst_pipe=35
  hdr=dmux_hdr_v4_2

rmnet_data0 Tx corretto:
  dst_pipe=35
  hdr=dmux_hdr_v4_1
```

IPACM arriva molto piu' avanti nel path WAN:

```text
Default route is added to iface rmnet_data0
Setting up QMAP ID 1
TriggerWANUp() if_name:rmnet_data0, ipv4_address:0xa8deb33 mux_id:1, xlat_mux_id:0
```

Questo e' un miglioramento netto rispetto al problema storico:

```text
TriggerWANUp ipv4_address=0x0
```

Risultato operativo negativo:

```text
ping gateway KO durante test
DL 1024 byte timeout durante test
rollback eseguito
baseline tornata OK dopo assestamento
```

Log chiave sul perche' non basta TX-only:

```text
rmnet_data0 Rx resta letto male perche' RX=0:
  Rx(0): src_pipe: 16384
  Rx(1): src_pipe: 0

IPACM_Lan:
  handle_uplink_filter_rule() No rx properties registered for iface eth0
```

Conclusione: il primo punto pericoloso e' gia' `rmnet TX` nel ciclo QCMAP completo. Corregge WAN/QMAP abbastanza da far partire TriggerWANUp, ma non basta a mantenere LAN/DL stabile perche' RX/eth0/uplink filters restano incoerenti.

## Stato finale dopo i test

Ripristino eseguito:

```sh
rm -f /etc/systemd/system/ipacm.service.d/31-abi-bridge-v2-test.conf
rm -f /usr/bin/ipacm-abi-bridge-v2-test.sh
rm -f /usr/lib/libipacm_abi_bridge_v2.so
systemctl daemon-reload
systemctl restart ipacm.service QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service
```

Verifica finale dopo assestamento:

```text
servizi active x5
nessun LD_PRELOAD attivo
route WAN via rmnet_data1
ping 192.168.225.1 OK
DL 1024 byte HTTP 200
```

## Conclusione aggiornata

La causa ABI e' confermata oltre ogni dubbio:

- `eth0` TX/RX vecchio layout: confermato
- `rmnet_data0` TX vecchio layout: confermato
- `rmnet_data1` TX vecchio layout: confermato
- EXT/mux: layout uguale, dati coerenti

La parte sicura oggi:

```text
eth0 TX/RX translate
```

La parte pericolosa oggi:

```text
rmnet TX translate durante ciclo QCMAP completo
```

Il punto successivo non deve essere EXT: EXT sembra gia' sano. Il prossimo nodo e' capire la combinazione minima stabile tra:

- eth0 TX/RX translate
- rmnet TX translate
- rmnet RX translate
- ordine eventi QCMAP
- route/filter ioctl in uscita

## Raccomandazione pratica

Non proseguire con TX+RX+EXT globale.

Il prossimo test sensato e' una Fase 5 controllata:

```text
eth0 TX/RX translate
rmnet_dataX TX/RX translate
EXT log-only
route/filter log-only
```

Ma solo con watchdog automatico piu' aggressivo, perche' Fase 4 ha gia' causato timeout DL durante il test.
