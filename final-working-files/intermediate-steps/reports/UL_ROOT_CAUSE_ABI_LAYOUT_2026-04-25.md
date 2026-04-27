# IPA UL - Causa radice identificata: mismatch ABI `ipa_ioc_*` (2026-04-25)

## Sintesi breve

Il problema IPA UL non e' nei moduli Realtek/IOSS/rmnet_eth che hanno gia' portato il DL hardware stabile.
La causa tecnica principale trovata oggi e' un mismatch ABI/layout tra:

- il kernel/driver IPA runtime, che restituisce le strutture `QUERY_INTF_TX_PROPS`/`RX_PROPS` con layout vecchio `02300`;
- il binario userspace `ipacm`, che interpreta quelle stesse strutture con layout piu' nuovo `04700/09300/13900`.

Effetto pratico: `ipacm` legge byte del nome header come se fossero campi numerici `dst_pipe`/`src_pipe`.
Per questo compaiono valori assurdi, header vuoti, `wrong tx property`, `COPY_HDR` falliti e setup UL incompleto.

## Prova piu' forte

Con probe compilato usando header `13900`, `eth0` viene letto cosi':

```text
IF name=eth0 query_ret=0 tx=2 rx=2 ext=0 excp=33
  TX ret=0 n=2
    tx[0] ip=0 dst=1987078495 alt=52 l2=0 hdr=''
    tx[1] ip=0 dst=0 alt=0 l2=0 hdr=''
  RX ret=0 n=2
    rx[0] ip=0 src=0 l2=0
    rx[1] ip=0 src=0 l2=0
```

Il valore `1987078495` in esadecimale e':

```text
1987078495 = 0x7670695f = bytes little-endian "_ipv"
```

Quindi il campo `dst_pipe` letto da userspace contiene in realta' una porzione della stringa `eth0_ipv4`.
Questo non e' un valore pipe valido: e' disallineamento di struttura.

## Conferma con raw dump

Il dump raw di `QUERY_INTF_TX_PROPS eth0` mostra:

```text
absolute offset 192: 6d 00 00 00          -> dst_pipe reale = 109
absolute offset 200: 65 74 68 30 5f 69... -> "eth0_ipv4"
absolute offset 232: 01 00 00 00          -> l2 = ETHII
```

Questi offset corrispondono al layout `02300`:

```text
struct ipa_ioc_tx_intf_prop 02300:
  dst_pipe offset relativo 156
  hdr_name offset relativo 164
  hdr_l2_type offset relativo 196
  sizeof(tx_prop) 200
```

Il layout `13900` invece si aspetta:

```text
struct ipa_ioc_tx_intf_prop 13900:
  dst_pipe offset relativo 168
  hdr_name offset relativo 176
  hdr_l2_type offset relativo 208
  sizeof(tx_prop) 212
```

Quindi un binario compilato per `13900` legge il buffer `02300` spostato di 12 byte.

## Probe con header corretto `02300`

Compilando lo stesso probe con header `02300`, i dati diventano coerenti:

```text
IF name=eth0 query_ret=0 tx=2 rx=2 ext=0 excp=33
  TX ret=0 n=2
    tx[0] ip=0 dst=109 alt=0 l2=1 hdr='eth0_ipv4'
    tx[1] ip=1 dst=109 alt=0 l2=1 hdr='eth0_ipv6'
  RX ret=0 n=2
    rx[0] ip=0 src=108 l2=1
    rx[1] ip=1 src=108 l2=1

IF name=rmnet_data0 query_ret=0 tx=2 rx=2 ext=9 excp=35
  TX ret=0 n=2
    tx[0] ip=0 dst=35 alt=0 l2=0 hdr='dmux_hdr_v4_1'
    tx[1] ip=1 dst=35 alt=0 l2=0 hdr='dmux_hdr_v4_1'
  RX ret=0 n=2
    rx[0] ip=0 src=34 l2=0 attrib=0x4000
    rx[1] ip=1 src=34 l2=0 attrib=0x4000
```

Questo dimostra che il kernel non sta necessariamente esponendo dati privi di senso: li espone in un layout diverso da quello che `ipacm` usa.

## Perche' questo spiega i log IPACM storici

Prima della traduzione ABI, `ipacm` loggava:

```text
handle_odu_hdr_init() Got partial v4-header name from 0 tx props
handle_odu_hdr_init() header name:  in tx:0
IPA_IOC_COPY_HDR ioctl failed
handle_eth_hdr_init() header name:  in tx:0
eth client not found/attached
Tx(1): wrong tx property: dst_pipe: 0
Failed to get QMAP header
TriggerWANUp() ipv4_address=0x0
```

Questi sintomi sono coerenti con una lettura sbagliata di:

- `dst_pipe`
- `src_pipe`
- `hdr_name`
- `hdr_l2_type`
- proprieta' WAN/QMAP su `rmnet_dataX`

## Cosa e' stato risolto dalla patch diagnostica

E' stata creata una LD_PRELOAD minimale:

```text
load_ipa_ES/ipacm_abi_bridge_preload.c
load_ipa_ES/libipacm_abi_bridge_v1.so
```

Scopo: intercettare solo `ioctl(/dev/ipa, IPA_IOC_QUERY_INTF_TX_PROPS/RX_PROPS)` e tradurre in-place il layout `02300` nel layout nuovo atteso da `ipacm`.

Questa patch NON fa:

- force di `WAN_UPSTREAM_ROUTE_ADD`
- fake `COPY_HDR`
- fake `ADD_HDR`
- rewrite filtri/route IPA
- modifiche ai moduli
- modifiche persistenti a `IPACM_cfg.xml`

Con la patch attiva solo su restart `ipacm`, IPACM legge finalmente `eth0` bene:

```text
Tx(0): ip-type: 0, dst_pipe: 109, alt_dst_pipe: 0, header: eth0_ipv4
Tx(1): ip-type: 1, dst_pipe: 109, alt_dst_pipe: 0, header: eth0_ipv6
Rx(0): ip-type: 0, src_pipe: 108
Rx(1): ip-type: 1, src_pipe: 108
```

E riesce a costruire header client validi:

```text
ODU v4 full header name:35_IPACM_ODU_v4 header handle:(0x23)
ODU v6 full header name:35_IPACM_ODU_v6 header handle:(0x24)
eth-client(0) v4 full header name:35_IPACM_ETH_v4_0 header handle:(0x25)
eth-client(0) v6 full header name:35_IPACM_ETH_v6_0 header handle:(0x26)
client(0): v4 header handle:(0x25)
```

## Cosa resta non risolto

La patch ABI bridge risolve una parte reale del problema, ma non basta ancora per dichiarare UL hardware al 100%.

Durante upload 10 MB con patch attiva dopo solo restart `ipacm`:

```text
HTTP upload: OK, 10 MB completi
hw_tx_delta: +7918
sw_tx_delta: +0
SW_FILT_delta: +7918
WAN_UPSTREAM_ROUTE_ADD: W:0 R:0
```

Il traffico IP passa, DL resta funzionante, ma `WAN_UPSTREAM_ROUTE_ADD` resta zero e `SW_FILT` cresce in parallelo.
Questo non soddisfa ancora il criterio UL HW 100%.

## Tentativo WAN/QCMAP completo e limite operativo

Riavviando anche `QCMAP_ConnectionManagerd`, `netmgrd` e `qcmap-radio-on` con ABI bridge attivo, IPACM legge correttamente anche `rmnet_data1`:

```text
Tx(0): ip-type: 0, dst_pipe: 35, alt_dst_pipe: 0, header: dmux_hdr_v4_2
Tx(1): ip-type: 1, dst_pipe: 35, alt_dst_pipe: 0, header: dmux_hdr_v4_2
Rx(0): ip-type: 0, src_pipe: 34
Rx(1): ip-type: 1, src_pipe: 34
Setting up QMAP ID 2
TriggerWANUp() if_name:rmnet_data1, ipv4_address:0xa8deb33 mux_id:2
```

Questo elimina il vecchio sintomo `ipv4_address=0x0`.
Pero' subito dopo la LAN host->modem si e' bloccata:

```text
ping 192.168.225.1: 100% packet loss
DL curl via enx00e04c6802a5: timeout
```

Rimuovendo il preload e riavviando i servizi, la baseline e' tornata sana:

```text
nessun LD_PRELOAD attivo
servizi active
ping gateway OK
DL 1024 byte HTTP 200
```

Quindi la patch e' diagnostica/parziale, non ancora accettabile come fix definitivo.

## Conclusione tecnica

Il motivo per cui IPA UL non funziona e' ora molto piu' chiaro:

1. Il path driver/moduli per DL e' sano.
2. Il kernel IPA espone proprieta' interfaccia in layout `02300`.
3. Il binario `ipacm` interpreta quelle proprieta' con layout nuovo.
4. Questo corrompe `dst_pipe/src_pipe/hdr_name` lato userspace.
5. Corrompendo le proprieta', IPACM non costruisce correttamente header client e regole UL.
6. Una traduzione ABI mirata corregge i campi e sblocca la creazione header ODU/ETH.
7. Applicarla anche al ciclo WAN/QCMAP porta IPACM fino a `TriggerWANUp` valido, ma causa regressione LAN, quindi non e' ancora deployabile.

## Stato finale lasciato sul modem

Al termine del test:

- drop-in preload rimosso
- wrapper `/usr/bin/ipacm-abi-bridge-test.sh` rimosso
- `/usr/lib/libipacm_abi_bridge_v1.so` rimosso dal modem
- `ipacm` avviato standard
- nessun `LD_PRELOAD` attivo
- servizi chiave `active`
- DL piccolo verificato OK

