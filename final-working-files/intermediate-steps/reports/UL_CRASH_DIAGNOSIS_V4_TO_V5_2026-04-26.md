# Diagnosi crash/loss LAN IPA UL v4 -> v5 - 2026-04-26

## Sintesi breve

La patch ABI v4 ha dimostrato che l'upload puo' entrare nel datapath hardware IPA/NAT:

- `sw_tx` resta `0` durante gli upload.
- `hw_tx` cresce in modo coerente col traffico.
- `lan_rx_excp[NAT]` cresce quasi 1:1 con `hw_tx`.
- `INVALID_PIPE`, `HDRI`, `CSUM` restano a `0`.
- `ipacm.service`, quando gestito da systemd runtime, resta `active/running` con `NRestarts=0`.

Pero' la v4 non e' ancora stabile sotto stress lungo: dopo il run da 50 MB la LAN e ADB sono rimasti non raggiungibili. La causa piu' probabile non e' un crash userspace di IPACM, ma una programmazione IPA route/filter ancora parzialmente incoerente, che genera warning kernel nel path `ioss -> IPA LAN RX -> bridge/netfilter/NAT`.

## Stato modem dopo il blocco

Dopo il run lungo:

```text
adb devices -l
4f2ad17a offline

ping 192.168.225.1
100% packet loss

ip neigh show dev enx00e04c6802a5
192.168.225.1 INCOMPLETE/FAILED
```

Quindi il piano LAN non risponde piu'. Serve reboot manuale per continuare i test live.

## Run v4 systemd eseguiti

| run | tipo | upload HTTP | bytes inviati | sw_tx delta | hw_tx delta | NAT delta | SW_FILT delta | ipacm | esito |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | --- | --- |
| `ul_v4_systemd_2026-04-26_11-46-45` | 10 MB | 200 | 10485760 | 0 | 7918 | 7917 | 1 | active, stesso PID, 0 restart | PASS breve |
| `ul_v4_systemd_2026-04-26_11-49-58` | 5 MB + eth0 down/up | 200 | 5242880 | 0 | 3964 | 3963 | 1 | active, stesso PID, 0 restart | PASS hotplug breve |
| `ul_v4_systemd_2026-04-26_11-53-59` | 50 MB | 100 / reset peer | 37552128 | 0 | 27355 | 27348 | 13 | active, stesso PID, 0 restart | FAIL stabilita' lunga |

Nota sul run da 50 MB: il server/connessione si e' fermato a circa 35.8 MiB con:

```text
curl: (56) Recv failure: Connection reset by peer
http=100
up=37552128
```

I counter IPA sono comunque saliti in hardware, ma il run non e' un PASS pieno per stabilita'.

## Evidenza: non e' crash IPACM userspace

Nel run lungo, dopo upload:

```text
ipacm_active=active
sub=running
pid=19745
nrestarts=0
Environment=... LD_PRELOAD=/tmp/libipacm_abi_bridge_v4.so
```

Quindi `ipacm` non e' morto. Il problema e' sotto o attorno alla programmazione kernel IPA/netfilter, non un semplice crash processo.

## Evidenza kernel principale

Nel `dmesg_after_upload.txt` del run lungo compare ripetutamente:

```text
WARNING: CPU: 0 ... at net/netfilter/nf_nat_core.c:614 nf_nat_setup_info+0x8d4/0x8dc
Workqueue: ipawq33 ipa3_wq_handle_rx
nf_nat_setup_info
nf_nat_masquerade_ipv4
masquerade_tg
ipt_do_table
nf_nat_inet_fn
nf_hook_slow
ip_output
ip_forward
ip_sabotage_in
br_nf_pre_routing
br_handle_frame
netif_rx_ni
ioss_ipa_notify_cb [ioss]
ipa3_lan_rx_cb
ipa3_lan_rx_pyld_hdlr
ipa3_wq_rx_common
ipa3_handle_rx
```

Nel sorgente kernel 5.4, la riga `nf_nat_core.c:614` e':

```c
if (WARN_ON(nf_nat_initialized(ct, maniptype)))
    return NF_DROP;
```

Interpretazione: netfilter sta provando ad applicare NAT/MASQUERADE a una conntrack entry che risulta gia' NAT-initialized per quel `maniptype`. In parole semplici: lo stesso flusso/pacchetto arriva al path NAT software in uno stato gia' manipolato/programmato, oppure viene visto due volte in modo incoerente.

## Evidenza IPA route/filter incoerente

Nei dmesg dei run v4 compaiono anche:

```text
ipa __ipa_add_hdr:600 IPACM Trying to add duplicate hdr 35_IPACM_ODU_v4
ipa __ipa_rt_validate_hndls:1001 rt rule does not point to valid hdr
ipa ipa3_add_rt_rule_usr:1342 failed to add rt rule 0
```

Questo e' molto importante: v4 corregge abbastanza ABI da far partire datapath e NAT hardware, ma alcune route/header/filter IPA sono ancora programmate male o duplicate.

## Evidenza ABI mancante in v4: ioctl V2

Nel log ABI v4 compare ripetutamente:

```text
[ABI] IOC nr=0x00000047 ret=0xffffffff ADD_FLT_V2 ip=0x00000000 ep=0x0000006c global=0x00000000 n=0x00000001 sz=0x00000184
[ABI] IOC nr=0x00000047 ret=0xffffffff ADD_FLT_V2 ip=0x00000001 ep=0x0000006c global=0x00000000 n=0x00000001 sz=0x00000184
```

`nr=0x47` e' `IPA_IOCTL_ADD_FLT_RULE_V2`.

Questo e' il pezzo che v4 lasciava pass-through. Il kernel 02300 conosce l'ioctl V2, ma le rule interne devono avere layout/size vecchi. IPACM invece passa rule-size nuovo (`0x184`) e il kernel risponde `-1` (`EINVAL`).

Quindi la spiegazione piu' probabile e':

1. v4 traduce `QUERY_TX/RX`, `ADD_RT`, `ADD_FLT`, `ADD_*_AFTER`, `GENERATE_FLT_EQ`.
2. v4 NON traduce `ADD_FLT_V2` / possibili `*_V2`.
3. Alcune regole V2 falliscono con `EINVAL`.
4. IPACM/QCMAP continua comunque e crea stato parziale: header duplicati, route verso header non valido, filtro V2 mancante.
5. Il traffico breve funziona perche' il path principale e' sufficiente.
6. Sotto traffico lungo/teardown, bridge/netfilter/NAT vede flussi gia' NAT-initialized o duplicati e scatta `nf_nat_setup_info()` warning.
7. Dopo stress, LAN/ADB possono restare non raggiungibili.

## Modifica preparata: v5

Creati:

- `load_ipa_ES/ipacm_abi_bridge_v5.c`
- `load_ipa_ES/libipacm_abi_bridge_v5.so`
- `load_ipa_ES/test_ipa_ul_v5_systemd.sh`

v5 aggiunge traduzione per:

- `IPA_IOCTL_ADD_RT_RULE_V2`
- `IPA_IOCTL_ADD_FLT_RULE_V2`
- `IPA_IOCTL_ADD_RT_RULE_AFTER_V2`
- `IPA_IOCTL_ADD_FLT_RULE_AFTER_V2`

La differenza chiave e':

- prende il puntatore `rules` contenuto nella struct V2 userspace nuova;
- costruisce un buffer temporaneo con rule layout vecchio 02300;
- imposta `rule_add_size` / `flt_rule_size` a `OLD_RT_ADD_SIZE` / `OLD_FLT_ADD_SIZE`;
- invoca il kernel con header V2 ma payload compat 02300;
- ricopia `handle` e `status` dal buffer vecchio al buffer nuovo di IPACM.

## Criterio per validare v5

Dopo reboot manuale:

1. Verificare baseline standard senza preload.
2. Eseguire `test_ipa_ul_v5_systemd.sh` con `UPLOAD_MB=10`.
3. Controllare che nei log ABI non compaia piu':

```text
ADD_FLT_V2 ... ret=0xffffffff
```

4. Controllare dmesg per assenza o forte riduzione di:

```text
rt rule does not point to valid hdr
failed to add rt rule
nf_nat_setup_info
```

5. Solo se 10 MB passa, provare hotplug breve.
6. Solo se hotplug passa, provare upload lungo ma con soglie progressive: 20 MB, poi 50 MB.

## Stato attuale

La v4 e' promettente ma non produzione.

La v5 e' pronta da testare, ma il modem e' offline/non raggiungibile dopo il blocco del run lungo. Serve reboot manuale prima di procedere.
