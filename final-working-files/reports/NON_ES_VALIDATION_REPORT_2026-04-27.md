# NON-ES validation report - 2026-04-27

## Scope

Porting della configurazione IPA UL/DL finale su modem NON-ES con secure boot stock, senza boot.img modificata.

## Nuovo componente

Modulo creato:

```text
/home/manu/Scrivania/linaro 5.4/load_ipa_NON_ES/latest/src_pcie_non_es_v1/pcie_enabler_non_es_v1.ko
```

Installato nel bundle come:

```text
/moduli/pcie_enabler_non_es_v1.ko
```

Funzioni:

- abilita runtime il PCIe Root Complex `/soc/qcom,pcie@1c00000`;
- inietta runtime i binding RTL8125/IOSS;
- applica il fix PMDS derivato da `pcie_enabler_ipaV22` su `ipa_smmu_eth` e `ipa_smmu_eth1`;
- evita la necessita di boot.img/DTB modificata.

## Bundle

```text
/home/manu/Scrivania/qcmap/dist/qcmap-modem-bundle-v3-NON-ES/qcmap-modem-bundle-v3-NON-ES.tar
```

## Load order NON-ES

```text
1. pcie_enabler_non_es_v1.ko kallsyms_lookup_name_addr=<runtime>
2. rmnet_eth.ko
3. r8125_rebuilt.ko
4. ioss_rebuilt.ko
5. r8125_ioss_rebuilt.ko
6. ip link set eth0 up
```

## Vincoli gestiti

- Stock boot ha `kptr_restrict=2`: il loader imposta `kptr_restrict=0` e legge `kallsyms_lookup_name`.
- SELinux stock blocca insmod da alcuni contesti: il bundle usa `disable-selinux.service` e il loader fa `setenforce 0` come fallback.
- Il modulo out-of-tree tainta il kernel per firma mancante, atteso su questa build.

## Test manuale modulo in RAM

Risultato:

```text
insmod_rc=0
PCIe RC created
Realtek PCIe device: 10ec:8125
PMDS fixed for ipa_smmu_eth
PMDS fixed for ipa_smmu_eth1
```

Stack completo caricato manualmente:

```text
pcie_enabler_non_es_v1
rmnet_eth
r8125
ioss
r8125_ioss
```

`eth0` portato UP e IPA ETH presente:

```text
RTK8125B PROD pipe_id=8 ch_id=18
RTK8125B CONS pipe_id=26 ch_id=21
```

## Bundle install + reboot

Post reboot dopo installazione bundle:

```text
pcie-enabler.service: active
ipacm.service: active
QCMAP_ConnectionManagerd.service: active
netmgrd.service: active
qcmap-radio-on.service: active
ipa-iptables-dedupe.timer: active
ipa-stack-healthcheck.timer: active
qcmap_httpd.service: active
```

Moduli:

```text
pcie_enabler_non_es_v1
rmnet_eth
r8125
ioss
r8125_ioss
```

Route:

```text
default via 10.166.85.200 dev rmnet_data0
192.168.225.0/24 dev bridge0 src 192.168.225.1
```

IPACM:

```text
MainPID=873
NRestarts=0
LD_PRELOAD=/usr/lib/libipacm_abi_bridge_final.so
```

## Traffic tests

DL artifact:

```text
/home/manu/Scrivania/linaro 5.4/load_ipa_NON_ES/non_es_bundle_dl_2026-04-27_19-50-57
```

DL result:

```text
file_bytes=50000000
http_code=200
hw_tx_delta=4
sw_tx_delta=0
SW_FILT_delta=1
esito=PASS
```

UL artifact:

```text
/home/manu/Scrivania/linaro 5.4/load_ipa_NON_ES/ul_v5_active_iperf3_2026-04-27_19-51-09
```

UL result:

```text
sent_bytes=12976128
sent_bps=6919976
sw_tx_delta=0
hw_tx_delta=7
rtk_cons_ring_delta=4246
rtk_prod_ring_delta=4246
INVALID_PIPE_delta=0
HDRI_delta=0
CSUM_delta=0
pid_before=873
pid_after=873
nrestarts_after=0
esito=PASS
```

## Second reboot persistence

Post second reboot:

```text
pcie-enabler.service Result=success ExecMainStatus=0
all 8 services/timers active
ipacm MainPID=812
ipacm NRestarts=0
route 192.168.225.0/24 present
IPA ETH RTK8125B present
INVALID_PIPE/HDRI/CSUM=0
```

## Problemi/limitazioni aperte

- IOSS logga: `channel[0] phandle resolved to non-channel node 'r8125_rx', trying fallback` e poi usa `built-in RTL8125 IOSS fallback config`. Nei test non blocca IPA ETH, DL o UL, ma resta da monitorare.
- La soluzione richiede `kallsyms_lookup_name` runtime; attualmente funziona dopo `kptr_restrict=0`.
- Il modulo non e firmato con chiave trusted del kernel stock; viene caricato perche il kernel consente moduli out-of-tree ma tainta il kernel.

## Verdetto

PASS iniziale.

La variante NON-ES porta il modem stock secure-boot a configurazione QCMAP + IPA UL/DL funzionante senza boot.img modificata. Servono ancora test piu lunghi/multipli prima di dichiararla equivalente alla baseline ES, ma il percorso tecnico e validato.
