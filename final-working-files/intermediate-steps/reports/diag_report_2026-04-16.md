# Diagnostica IPA ETH Offload — SSR post-reg_intf
**Run:** case_stop_on_problem_2026-04-16_20-14-03  
**Data raccolta:** 2026-04-16  
**Target:** SDX65, kernel msm-5.4, ioss + r8125_ioss, pcie_enabler_ipaV20

---

## Sommario (5-10 righe)

1. **SSR non catturato nel kmsg**: il file termina a t=32.941 s con `Mapped TX-1 event`; il device crasha ~0.8 s dopo (ADB cade tra t=33.75 e t=34 s). Il segnale di SSR non è mai arrivato nel buffer kmsg.
2. **ipa_fws in stato OFFLINING** (rilevato nel boot successivo al crash via ADB): `subsys1/ipa_fws state = OFFLINING`. Anomalia: la FWS IPA non è ONLINE dopo la recovery.
3. **NTN protocol missing**: IPA uC log riporta `NTN protocol missing 0x1` al momento del load del uC (t≈28.6 s del boot corrente). Nessuna entry IPA relativa alla registrazione NTN/ETH avvenuta a t=32.9 s nel run che crasha.
4. **Nessun handshake QMI esplicito post-reg_intf in ioss**: il driver ioss non esegue alcuna call QMI dopo `ipa_eth_client_reg_intf`. L'handshake QMI con il modem è interamente delegato a ipa-wan (al boot, tramite `QMI_IPA_INIT_MODEM_DRIVER_REQ_V01`).
5. **RX ring size = 1024** (iniettato da pcie_enabler_ipaV20.c riga 301). Il fallback in ioss_of.c usa 1023. Il run corrente usa 1024. Preflight conferma `buffs=1024` per entrambe le direzioni senza errori.
6. **IOMMU debugfs assente**: `/sys/kernel/debug/iommu/` non trovato; impossibile verificare fault SMMU via filesystem. GSI ch_dump/ev_dump/stats tutti denied.
7. **IPA log dal boot corrente** (non dal run che crasha): la IPC ring è in-memory e viene persa al crash. Il log ADB riflette il boot post-SSR (uptime ≈ 63 s al momento della raccolta).
8. **Remoteproc debugfs assente**: `/sys/kernel/debug/remoteproc/` non trovato; crash_reason non leggibile.

---

## Task 1 — Finestra kmsg attorno all'evento SSR

### Fonte
`diag_runs/case_stop_on_problem_2026-04-16_20-14-03.kmsg` (147 righe)

### Risultato: SSR NON catturato

Il primo segnale di SSR atteso (pattern: subsys/ssr/remoteproc/Q6/modem.*crash/smp2p/glink.*close)
**non compare nel file**. Il file termina a `t=32.941 s` con l'ultima riga di setup IOSS.

Correlazione con il .log:
```
[20:14:40] UPTIME=33.75   ← device ancora vivo
[20:14:41] UPTIME=ERR     ← ADB cade (SSR onset tra t=33.75 e t=34 s kernel uptime)
[20:14:45–20:15:04] DEV=NONE  ← recovery in corso (~23 s)
[20:15:05] UPTIME=17.94   ← nuovo boot confermato
```

### Intera finestra pre-SSR (t=23.980 s → t=32.941 s)

| Timestamp | Evento |
|-----------|--------|
| 23.980 | HwRadioState=1 |
| 27.401 | pcie_enabler_ipaV20 insmod — firma mancante (tainting kernel) |
| 27.444 | RC enabled, RTL8125 node + channel phandles iniettati |
| 27.498–27.570 | msm_pcie_probe RC0, link initialized |
| 27.675–27.950 | PCI host bridge su bus 0000:00, RTL8125 [10ec:8125] rilevato a 0000:01:00.0 |
| 28.064 | **modem: Brought out of reset** |
| 28.064 | subsys_powerup: pil_boot successful, waiting for error ready |
| 28.105 | subsys_err_ready_intr_handler: error monitoring UP |
| 28.127 | modem: Power/Clock ready |
| 28.141 | `addr of ctxt->list: b7ef8338` (×2, poi b7d89538/b7d891b8) |
| 28.174–28.191 | qcom_smd_qrtr_probe, Modem QMI Readiness TX/RX cmd:0x2 |
| 28.213 | IPA received MPSS AFTER_POWERUP |
| 28.226 | sysmon-qmi: Connection established (SSCTL) |
| 28.720 | pcie_enabler_v20: RTL8125 trovato a 0000:01:00.0 |
| 28.738–28.768 | **PMDS fix**: ipa_smmu_eth efc7519f→4c765610, ipa_smmu_eth1 8aff02dd→4c765610 |
| 28.836 | r8125: Adding to iommu group 10 |
| 28.880 | **Sending QMI_IPA_INIT_MODEM_DRIVER_REQ_V01** |
| 28.943 | WDI protocol missing 0x1, NTN protocol missing 0x1 |
| 28.952 | **QMI_IPA_INIT_MODEM_DRIVER_REQ_V01 response received** |
| 29.030–29.110 | IOSS init: version 0x1000000, IPA ready, PCI started, r8125_ioss registered, eth0 added |
| 32.560 | **r8125: eth0: link up** |
| 32.586 | **IPA register start: inst=0 ctype=3** |
| 32.586–32.718 | IOVA remapping (8 pipe buffers TX+RX sopra ceiling 0x9fffffff → rimappati a PA) |
| 32.824–32.836 | **Preflight summary: TX trsz=32768 buffs=1024 rc=0; RX trsz=32768 buffs=1024 rc=0** |
| 32.848 | Preflight pipe count: tx=1 rx=1 rc=0 |
| 32.861–32.873 | **ipa_eth_client_conn_pipes** called + done |
| 32.877–32.893 | ioss_ipa_vote_bw: voted 1000 Mbps |
| 32.902–32.909 | **ipa_eth_client_reg_intf** called + done |
| 32.917 | **Mapped RX-1 event 0xfedff920 → 0x03e18920** |
| 32.941 | **Mapped TX-1 event 0xfffcfaa0 → 0x03e18aa0** ← ultimo log |
| ~33.75 | **SSR ONSET** (dedotto dal .log; non catturato nel kmsg) |

### Grep filtrato sui pattern diagnostici (finestra intera)

```
Pattern: subsys, ssr, remoteproc, smmu, iommu, fault, qmi, ipa, gsi, q6, smp2p, glink, pcie.*link, pcie.*down
```

| t (s) | Match |
|-------|-------|
| 28.064 | `subsys-pil-tz 4080000.qcom,mss: modem: Brought out of reset` |
| 28.064 | `subsys-pil-tz: subsys_powerup(): pil_boot is successful from modem` |
| 28.105 | `subsys-pil-tz: subsys_err_ready_intr_handler(): services are up from modem` |
| 28.127 | `subsys-pil-tz 4080000.qcom,mss: modem: Power/Clock ready` |
| 28.178 | `qcom_smd_qrtr_probe:SMD QRTR driver probed` |
| 28.179 | `qrtr: Modem QMI Readiness RX cmd:0x2 node[0x3]` |
| 28.187 | `qrtr: Modem QMI Readiness TX cmd:0x2 node[0x2]` |
| 28.213 | `ipa-wan ipa3_lcl_mdm_ssr_notifier_cb:4035 IPA received MPSS AFTER_POWERUP` |
| 28.226 | `sysmon-qmi: ssctl_new_server: Connection established (modem SSCTL)` |
| 28.738 | `[pcie_enabler_v20] PMDS fix apply on :ipa_smmu_eth: efc7519f -> 4c765610` ← SMMU |
| 28.768 | `[pcie_enabler_v20] PMDS fix apply on :ipa_smmu_eth1: 8aff02dd -> 4c765610` ← SMMU |
| 28.880 | `Sending QMI_IPA_INIT_MODEM_DRIVER_REQ_V01` ← QMI |
| 28.943 | `ipa ipa3_uc_wdi_event_log_info_handler:371 WDI protocol missing 0x1` ← IPA |
| 28.943 | `ipa ipa3_uc_ntn_event_log_info_handler:19 NTN protocol missing 0x1` ← IPA/NTN |
| 28.952 | `QMI_IPA_INIT_MODEM_DRIVER_REQ_V01 response received` ← QMI |
| 32.560–32.941 | (righe IPA register/preflight/conn_pipes/reg_intf/mapped — vedi tabella sopra) |

**Nessuna riga con pattern:** ssr, remoteproc, fault, iommu, gsi, q6 (maiusc), smp2p, glink, pcie.*down.

---

## Task 2 — Crash reason del modem

### Path verificati via ADB (device attivo post-SSR)

| Path | Esito |
|------|-------|
| `/sys/kernel/debug/remoteproc/` | **Non trovato** (debugfs non monta remoteproc su questo target) |
| `/sys/bus/msm_subsys/devices/subsys0/name` | `modem` |
| `/sys/bus/msm_subsys/devices/subsys0/state` | `ONLINE` |
| `/sys/bus/msm_subsys/devices/subsys1/name` | `ipa_fws` |
| `/sys/bus/msm_subsys/devices/subsys1/state` | **`OFFLINING`** ← anomalia |
| `/sys/kernel/debug/qcom_socinfo/` | Non trovato |
| `/data/vendor/ramdump/` | Non accessibile (permessi) |
| `/data/ramdump/` | Non accessibile (permessi) |

**Anomalia segnalata**: `ipa_fws` è nello stato `OFFLINING` invece di `ONLINE` nel boot corrente.  
`crash_reason` non disponibile: il debugfs di remoteproc non è presente su questo target.

---

## Task 3 — Log IPC interni IPA

> **Nota**: il log IPC è in-memory; viene perso al crash. I dati seguenti sono dal boot corrente (uptime ≈ 63 s al momento della raccolta), **non dal run che ha crashato**.

### `/sys/kernel/debug/ipc_logging/ipa/log` (500 righe, ultime del boot corrente)

Log disponibile (16 s → 63 s). Selezione eventi rilevanti:

| t (s) | Evento |
|-------|--------|
| 16.10 | `ipa3_xdci_start:942 client (ep:0) success` |
| 16.10 | `ipa3_uc_state_check:654 uC is not loaded` (×2) |
| 16.10 | `ipa3_uc_send_cmd_64b_param:962 uC send command aborted` (×2) |
| 16.10 | `ipa3_uc_debug_stats_alloc:1603 fail to alloc offload stats` (×2) |
| 16.14 | `ipa_usb_connect_teth_prot:1411 connecting protocol = teth_bridge` |
| 16.15 | `IPA_USB_DEVICE_READY notified` |
| **19.03** | **`ipa-wan: IPA received MPSS BEFORE_POWERUP`** ← SSR modem a t=19 s boot corrente |
| 19.03–19.06 | BEFORE_POWERUP + handling complete + notification 7 |
| 27.25 | `notification 4` (unsupported) |
| 27.81 | `notification 8` (unsupported) |
| **27.84** | **`IPA received MPSS AFTER_POWERUP`** ← recovery completata |
| 28.61 | `Q6 QMI service available now` → `ipa3_qmi_init_modem_send_sync_msg` inviata |
| 28.63 | `is_ssr_bootup 0` nel QMI init msg |
| **28.63** | `ipa3_uc_event_handler: uC evt opcode=2` (log info offset=0x151080) |
| **28.63** | **`ipa3_uc_wdi_event_log_info_handler:371 WDI protocol missing 0x1`** |
| **28.63** | **`ipa3_uc_ntn_event_log_info_handler:19 NTN protocol missing 0x1`** ← NTN mancante |
| 28.63 | `ipa3_uc_response_hdlr:842 uC rsp opcode=1` → `IPA uC loaded` |
| 28.64 | `QMI_IPA_INIT_MODEM_DRIVER_CMPLT_REQ_V01 received` → risposto |
| 28.64 | `handle_ipa_config_req: IPA CONFIG Request received` → risposto |
| **28.64** | **`ipa3_q6_handshake_complete:5659 Q6 handshake complete`** |
| 62.11–63.64 | WWAN rmnet_data0–5 registrazione canali (6 canali aggiunti) |

**Assente nel log corrente**: nessuna entry relativa a `ipa_eth_client_conn_pipes` / `reg_intf` / `NTN conn` dal run corrente (la registrazione ETH non è ancora avvenuta in questo boot al momento della raccolta).

### `/sys/kernel/debug/ipa_low/log`
Non trovato.

### `/sys/kernel/debug/ipa/stats`
```
sw_tx=0, hw_tx=0, tx_non_linear=0, tx_compl=0
wan_rx=0, stat_compl=0
act_clnt=1, con_clnt_bmap=0x20814205
lan_rx_excp[0:NONE]=1, tutti gli altri =0
pipe_setup_fail_cnt=0
```

### `/sys/kernel/debug/ipa/hw_stats`
`[permission denied]`

### `/sys/kernel/debug/ipa/ip4_flt`, `ip4_flt_hw`, `ip4_nat`, `ip4_rt`, `ip4_rt_hw`
Tutti vuoti (nessun contenuto — tabelle non popolate).

### `/sys/kernel/debug/gsi/*`
| File | Esito |
|------|-------|
| `gsi_fw_version` | `hw=13, flavor=0, fw=67` |
| `gsi_hw_profiling_stats` | `bp_count=0, mcs_busy=0, total_cycle_count=0, utilization=0%` |
| `ch_dump`, `ev_dump`, `ipc_low`, `stats`, `max_elem_dp_stats`, `print_dp_stats`, `rst_stats`, `enable_dp_stats` | **`[permission denied]`** su tutti |

---

## Task 4 — Flusso post-reg_intf nel codice

### 4.1 `ipa_eth_client_reg_intf` in drivers/ioss/

```
drivers/ioss/ioss_ipa.c:671  ioss_dev_cfg(idev, "Calling ipa_eth_client_reg_intf");
drivers/ioss/ioss_ipa.c:672  rc = ipa_eth_client_reg_intf(ii);
drivers/ioss/ioss_ipa.c:677  ioss_dev_cfg(idev, "ipa_eth_client_reg_intf done");
```

### 4.2 Funzione intera che contiene la chiamata (`ioss_ipa_register`, riga 530)

La funzione `ioss_ipa_register()` (ioss_ipa.c:530–719) termina a riga 687 con `return 0` **immediatamente dopo** `reg_intf done`. Non ci sono ulteriori chiamate IPA ETH dopo reg_intf nella stessa funzione.

**Flusso completo interno a `ioss_ipa_register`:**
1. Preflight pipe info fill (×N canali)
2. `ioss_ipa_preflight_client()` → validazione preflight
3. `ipa_eth_client_conn_pipes(ec)` ← pipeline
4. `ioss_ipa_vote_bw()` → `ipa_eth_client_set_perf_profile()` ← BW vote
5. Loop: recupero `db_pa`/`db_val` da ogni pipe
6. `ipa_eth_client_reg_intf(ii)` ← **ultima call IPA**
7. `return 0`

### 4.3 Chiamante: `ioss_net_start_interface` (ioss_net.c:600–659)

Dopo il ritorno di `ioss_ipa_register()` (riga 620), la sequenza è:

```c
// ioss_net.c:620
rc = ioss_ipa_register(iface);        // ← conn_pipes + reg_intf al suo interno
if (rc) goto err_ipa_register;

rc = ioss_net_setup_events(iface);    // riga 626 ← DMA map IPA DB → periferica
if (rc) goto err_setup_events;        //   genera "Mapped RX/TX event" nel kmsg

rc = ioss_net_enable_channels(iface); // riga 632 ← rtl8125_enable_ring()
if (rc) goto err_enable_channels;

rc = ioss_pci_disable_pc(idev);       // riga 638 ← disabilita PCI power collapse
if (rc) goto err_disable_pc;

iface->state = IOSS_IF_ST_ONLINE;     // riga 644
```

Il crash avviene **durante o dopo** `ioss_net_setup_events` (i "Mapped event" sono stati emessi) e **prima o durante** `ioss_net_enable_channels` o `ioss_pci_disable_pc`.

### 4.4 Tutte le API `ipa_eth_client_*` in ioss, in ordine di chiamata

| Ordine | Funzione | File:riga |
|--------|----------|-----------|
| 1 | `ipa_eth_client_conn_pipes(ec)` | ioss_ipa.c:632 |
| 2 | `ipa_eth_client_set_perf_profile(ec, &profile)` | ioss_ipa.c:493 (in vote_bw) |
| 3 | `ipa_eth_client_reg_intf(ii)` | ioss_ipa.c:672 |
| — | `ipa_eth_client_conn_evt` / `disconn_evt` | ioss_ipa.c:515-516 (IPA_ETH_API_VER >= 2, path condizionale) |
| — | `ipa_eth_client_disconn_pipes(ec)` | ioss_ipa.c:695 (error path) |
| — | `ipa_eth_client_disconn_pipes(ec)` | ioss_ipa.c:743 (unregister) |
| — | `ipa_eth_client_unreg_intf(ii)` | ioss_ipa.c:750 (unregister) |

### 4.5 Riferimenti QMI in drivers/ioss/

**Nessun match trovato.** Il driver ioss non contiene riferimenti QMI. L'handshake QMI modem↔IPA è completamente gestito da `ipa-wan` (kernel space, `ipa3_q6_handshake_complete` a t≈28.6 s, ben prima della registrazione ETH a t=32.9 s).

### 4.6 `perf_profile` / `set_state` / `enable_offload` / `start_offload`

| Pattern | Match |
|---------|-------|
| `perf_profile` | ioss_ipa.c:482 (`struct ipa_eth_perf_profile profile`) e :493 (`ipa_eth_client_set_perf_profile`) — chiamato dentro `ioss_ipa_vote_bw`, prima di `reg_intf` |
| `set_state` | **Nessun match** |
| `enable_offload` | **Nessun match** |
| `start_offload` | **Nessun match** |

Nessuno step di abilitazione esplicita dell'offload (tipo `start_offload` / `set_state`) trovato in ioss. La pipeline è considerata attiva dopo `reg_intf`.

---

## Task 5 — SMMU / IOMMU domain

### 5.1 `/sys/kernel/debug/iommu/`
**Non trovato** sul device (né debugfs IOMMU né fault file disponibili).

### 5.2 SMMU / IOMMU / IOVA in `ioss_ipa.c` (primi 50 match)

Il file contiene una gestione estesa del problema IOVA ceiling:

| Riga | Contenuto |
|------|-----------|
| 32–45 | Commento: "IPA SMMU ETH CB practical iova ceiling ≈ 0x9fffffff. R8125 rings possono esporre DMA a 0xffXXXXXX; mapparli nell'ETH CB causa fault in `av8l_fast_map()`" |
| 37–38 | `ioss_fix_high_iova = true` (default ON) |
| 42–43 | `ioss_iova_ceiling = 0x9fffffff` |
| 66–69 | `ioss_buff_iova_align = 0x800` |
| 127–162 | Preflight: check per iova > ceiling, iova non allineato, iova che si sovrappone al transfer ring |
| 239–284 | `ioss_ipa_fill_pipe_info`: remap descriptor IOVA se > ceiling → usa PA |
| 300–370 | Buffer list: remap IOVA se > ceiling; detect PA=kva (PAGE_OFFSET guard) → fix con `virt_to_phys` |
| 278–280 | Abort se desc_iova ancora > ceiling dopo remap |

**Nel kmsg del run crashato**: tutti e 8 i buffer pipe (4 TX + 4 RX) avevano IOVA sopra ceiling e sono stati rimappati con successo (messaggi `ERR: High DMA IOVA ... remap IPA IOVA to ...`). Il preflight ha riportato `rc=0, warns=0` per entrambe le direzioni.

### 5.3 IOMMU / SMMU in DT e `pcie_enabler_ipaV20.c`

Il file `pcie_enabler_ipaV20.c` manipola direttamente i page table SMMU:

| Riga | Contenuto |
|------|-----------|
| 15–20 | `#include <linux/iommu.h>`, `#include "arm-smmu.h"` |
| 40–47 | `struct smmu_dev_search` — cerca device per suffix nel platform bus |
| 60–78 | `find_ipa_smmu_dev(suffix)` — cerca `ipa_smmu_ap`, `ipa_smmu_eth`, `ipa_smmu_eth1` |
| 79–156 | `fix_one_ipa_smmu_pmds(slave_suffix)` — copia `pgtbl_ops[0]` dal dominio AP al dominio slave (eth/eth1) |
| 158–161 | `fix_ipa_smmu_pmds_v20()` — chiama fix per `:ipa_smmu_eth` e `:ipa_smmu_eth1` |
| 300 | `/* Keep rings page-friendly for IPA SMMU mapping */` |
| 502 | `fix_ipa_smmu_pmds_v20()` — chiamata al momento dell'init del modulo |

Nel kmsg: il fix PMDS è applicato a t=28.738–28.768 s:
```
PMDS fix apply on :ipa_smmu_eth:  efc7519f -> 4c765610
PMDS fix apply on :ipa_smmu_eth1: 8aff02dd -> 4c765610
```
Nessun log di errore PMDS fix nel kmsg.

---

## Task 6 — A/B ring RX (preparazione, NO esecuzione)

### 6.1 Stato attuale (git diff + sorgenti)

**Ring size RX attivo = 1024** — iniettato da `pcie_enabler_ipaV20.c` riga 301:
```c
prop_prepend(np, prop_build_u32("qcom,ring-size", 1024));
```
Questo vale per **entrambi** i canali (TX e RX), poiché `build_channel_node()` usa lo stesso valore per entrambe le direzioni.

Il fallback in `ioss_of.c` (ramo nuovo del git diff) definisce:
```c
#define IOSS_R8125_RX_RING_SIZE 1023
#define IOSS_R8125_TX_RING_SIZE 1024
```
Ma questo percorso viene raggiunto solo se il DT non viene trovato / le phandle falliscono. Con pcie_enabler_ipaV20 caricato, si usa il DT iniettato → 1024 per entrambi.

**git diff** (data-eth-src HEAD): i file modificati rispetto al commit HEAD sono:
- `drivers/ioss/ioss_ipa.c` — aggiunta preflight + IOVA fix + diagnostics
- `drivers/ioss/ioss_mem.c` — fix llcc_mem_pa (offset direction)
- `drivers/ioss/ioss_of.c` — fallback config R8125 + phandle cache fix
- `drivers/r8125_ioss/r8125_ioss.c` — diag gates (skip_enable_channel, skip_request_event, ecc.)

**pcie_enabler_ipaV20.c non è nel repo data-eth-src**: il file è in `/home/manu/Scrivania/linaro 5.4/pcie_enabler_ipaV20.c` fuori dal repo, quindi `git diff` non lo mostra.

### 6.2 Comando per impostare RX=1023 (NON eseguire)

Per fare il test A/B con RX=1023, modificare **riga 301** di `pcie_enabler_ipaV20.c`:

```diff
-    prop_prepend(np, prop_build_u32("qcom,ring-size", 1024));
+    prop_prepend(np, prop_build_u32("qcom,ring-size", 1023));
```

Poi ricompilare e ricaricare il modulo. I comandi esatti (da eseguire manualmente):

```bash
# 1. Modifica il sorgente (NON eseguita qui):
#    Cambia "1024" in "1023" a riga 301 di pcie_enabler_ipaV20.c

# 2. Ricompila il modulo (adattare il percorso al Makefile locale):
make -C /path/to/kernel M=$(pwd) modules

# 3. Push e carica sul device (NON eseguito qui):
adb push pcie_enabler_ipaV20.ko /data/local/tmp/
adb shell "rmmod pcie_enabler_ipaV20; insmod /data/local/tmp/pcie_enabler_ipaV20.ko"
# (oppure seguire la sequenza di boot completa del test case)
```

**Nota**: il valore 1023 evita che la dimensione del ring ring sia una potenza di 2 (1023 = 0x3FF, non pow2). Il preflight attuale emette solo un `warn++` per "transfer ring size non power-of-two" ma non fallisce con `preflight_strict=true`. Se si vuole testare con ring power-of-2, usare 512 o 1024.

---

## Appendice — File e path non disponibili

| Path | Motivo |
|------|--------|
| `/sys/kernel/debug/remoteproc/*/crash_reason` | Debugfs remoteproc non montato sul target |
| `/sys/kernel/debug/iommu/*/fault` | Debugfs IOMMU non montato |
| `/sys/kernel/debug/ipa/hw_stats` | Permission denied |
| `/sys/kernel/debug/gsi/ch_dump`, `ev_dump`, `ipc_low`, `stats` | Permission denied |
| `/data/vendor/ramdump/`, `/data/ramdump/` | Permission denied |
| `/sys/kernel/debug/qcom_socinfo/` | Non trovato |
| IPA IPC log dal boot crashato | In-memory, perso al crash |
